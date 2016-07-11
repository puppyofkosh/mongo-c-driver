/*
 * Copyright 2016 MongoDB, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */


#include "mongoc-metadata.h"
#include "mongoc-metadata-private.h"
#include "mongoc-client.h"
#include "mongoc-client-private.h"
#include "mongoc-error.h"
#include "mongoc-log.h"
#include "mongoc-version.h"
#include "mongoc-util-private.h"

static bool
_get_linux_distro (char **name,
                   char **version);

#ifdef _WIN32
#include <windows.h>
#include <stdio.h>
#include <VersionHelpers.h>
#else
#include <sys/utsname.h>
#endif

/*
 * Global metadata instance. Initialized at startup from mongoc_init ()
 *
 * Can be modified by calls to mongoc_set_metadata ()
 */
static mongoc_metadata_t gMongocMetadata;

static uint32_t
get_config_bitfield ()
{
   uint32_t bf = 0;

#ifdef MONGOC_ENABLE_SSL_SECURE_CHANNEL
   bf |= MONGOC_MD_FLAG_ENABLE_SSL_SECURE_CHANNEL;
#endif

#ifdef MONGOC_ENABLE_CRYPTO_CNG
   bf |= MONGOC_MD_FLAG_ENABLE_CRYPTO_CNG;
#endif

#ifdef MONGOC_ENABLE_SSL_SECURE_TRANSPORT
   bf |= MONGOC_MD_FLAG_ENABLE_SSL_SECURE_TRANSPORT;
#endif

#ifdef MONGOC_ENABLE_CRYPTO_COMMON_CRYPTO
   bf |= MONGOC_MD_FLAG_ENABLE_CRYPTO_COMMON_CRYPTO;
#endif

#ifdef MONGOC_ENABLE_SSL_OPENSSL
   bf |= MONGOC_MD_FLAG_ENABLE_SSL_OPENSSL;
#endif

#ifdef MONGOC_ENABLE_CRYPTO_LIBCRYPTO
   bf |= MONGOC_MD_FLAG_ENABLE_CRYPTO_LIBCRYPTO;
#endif

#ifdef MONGOC_ENABLE_SSL
   bf |= MONGOC_MD_FLAG_ENABLE_SSL;
#endif

#ifdef MONGOC_ENABLE_CRYPTO
   bf |= MONGOC_MD_FLAG_ENABLE_CRYPTO;
#endif

#ifdef MONGOC_ENABLE_CRYPTO_SYSTEM_PROFILE
   bf |= MONGOC_MD_FLAG_ENABLE_CRYPTO_SYSTEM_PROFILE;
#endif

#ifdef MONGOC_ENABLE_SASL
   bf |= MONGOC_MD_FLAG_ENABLE_SASL;
#endif

#ifdef MONGOC_HAVE_SASL_CLIENT_DONE
   bf |= MONGOC_MD_FLAG_HAVE_SASL_CLIENT_DONE;
#endif

#ifdef MONGOC_HAVE_WEAK_SYMBOLS
   bf |= MONGOC_MD_FLAG_HAVE_WEAK_SYMBOLS;
#endif

#ifdef MONGOC_NO_AUTOMATIC_GLOBALS
   bf |= MONGOC_MD_FLAG_NO_AUTOMATIC_GLOBALS;
#endif

#ifdef MONGOC_BSON_BUNDLED
   bf |= MONGOC_MD_FLAG_BSON_BUNDLED;
#endif

   return bf;
}


#ifdef _WIN32
static char *
_windows_get_version_string ()
{
   const char *ret;

   /*
    * As new versions of windows are released, we'll have to add to this
    * See:
    * https://msdn.microsoft.com/en-us/library/windows/desktop/ms724832(v=vs.85).aspx
    * For windows names -> version # mapping
    */

   if (IsWindowsVersionOrGreater (10, 0, 0)) {
      /* No IsWindows10OrGreater () function available with this version of
       * MSVC */
      ret = ">= 10";
   } else if (IsWindowsVersionOrGreater (6, 3, 0)) {
      /* No IsWindows8Point10OrGreater() function available with this version
       * of MSVC */
      ret = "8.1";
   } else if (IsWindows8OrGreater ()) {
      ret = "8";
   } else if (IsWindows7SP1OrGreater ()) {
      ret = "7.1";
   } else if (IsWindows7OrGreater ()) {
      ret = "7";
   } else if (IsWindowsVistaOrGreater ()) {
      ret = "Vista";
   } else if (IsWindowsXPOrGreater ()) {
      ret = "XP";
   } else {
      ret = "Pre XP";
   }

   return bson_strndup (ret, METADATA_OS_VERSION_MAX);
}

static char *
_windows_get_arch_string ()
{
   SYSTEM_INFO system_info;
   DWORD arch;
   const char *ret;

   /* doesn't return anything */
   GetSystemInfo (&system_info);

   arch = system_info.wProcessorArchitecture;

   if (arch == PROCESSOR_ARCHITECTURE_AMD64) {
      ret = "x86_64";
   } else if (arch == PROCESSOR_ARCHITECTURE_ARM) {
      ret = "ARM";
   } else if (arch == PROCESSOR_ARCHITECTURE_IA64) {
      ret = "IA64";
   } else if (arch == PROCESSOR_ARCHITECTURE_INTEL) {
      ret = "x86";
   } else if (arch == PROCESSOR_ARCHITECTURE_UNKNOWN) {
      ret = "Unknown";
   }

   if (ret) {
      return bson_strndup (ret, METADATA_OS_ARCHITECTURE_MAX);
   }

   MONGOC_ERROR ("Processor architecture lookup failed");
   return NULL;
}

static void
_get_system_info (mongoc_metadata_t *metadata)
{
   metadata->os_name = bson_strndup ("Windows", METADATA_OS_NAME_MAX);
   metadata->os_version = _windows_get_version_string ();
   metadata->os_architecture = _windows_get_arch_string ();
}
#else
static void
_get_system_info (mongoc_metadata_t *meta)
{
   struct utsname system_info;
   int res;

   res = uname (&system_info);

   if (res != 0) {
      MONGOC_ERROR ("Uname failed with error %d", errno);
      return;
   }

   meta->os_name = bson_strndup (system_info.sysname, METADATA_OS_NAME_MAX);
   meta->os_architecture = bson_strndup (system_info.machine,
                                         METADATA_OS_ARCHITECTURE_MAX);
   meta->os_version = bson_strndup (system_info.release,
                                    METADATA_OS_VERSION_MAX);
}
#endif

static void
_free_system_info (mongoc_metadata_t *meta)
{
   bson_free ((char *) meta->os_version);
   bson_free ((char *) meta->os_name);
   bson_free ((char *) meta->os_architecture);
}

void
_mongoc_metadata_init ()
{
   const char *driver_name = "mongoc";

   /* TODO: FIXME: Put this in ifdef linux blocks someday */
   char *name;
   char *version;

   _get_linux_distro (&name, &version);

   /* TODO: FIXME: Actually do something with these */
   bson_free (name);
   bson_free (version);

   /* Do OS detection here */
   _get_system_info (&gMongocMetadata);

   gMongocMetadata.driver_name = bson_strndup (driver_name,
                                               METADATA_DRIVER_NAME_MAX);

   gMongocMetadata.driver_version = bson_strndup (MONGOC_VERSION_S,
                                                  METADATA_DRIVER_VERSION_MAX);

   gMongocMetadata.platform = bson_strdup_printf (
      "cfgbits 0x%x CC=%s CFLAGS=%s SSL_CFLAGS=%s SSL_LIBS=%s",
      get_config_bitfield (),
      MONGOC_CC,
      MONGOC_CFLAGS,
      MONGOC_SSL_CFLAGS,
      MONGOC_SSL_LIBS);

   gMongocMetadata.frozen = false;
}

void
_mongoc_metadata_cleanup ()
{
   _free_system_info (&gMongocMetadata);
   bson_free ((char *) gMongocMetadata.driver_name);
   bson_free ((char *) gMongocMetadata.driver_version);
   bson_free ((char *) gMongocMetadata.platform);
}

static void
_append_and_free (const char **s,
                  const char  *suffix)
{
   const char *tmp = *s;

   if (suffix) {
      *s = bson_strdup_printf ("%s / %s", tmp, suffix);
      bson_free ((char *) tmp);
   }
}

/*
 * Return true if we build the document, and it's not too big
 * False if there's no way to prevent the doc from being too big. In this
 * case, the caller shouldn't include it with isMaster
 */
bool
_mongoc_metadata_build_doc_with_application (bson_t     *doc,
                                             const char *application)
{
   uint32_t max_platform_str_size;
   char *platform_copy = NULL;
   const mongoc_metadata_t *md = &gMongocMetadata;
   bson_t child;

   /* Todo: Add os.version, strip and if necessary */
   if (application) {
      BSON_APPEND_UTF8 (doc, "application", application);
   }

   BSON_APPEND_DOCUMENT_BEGIN (doc, "driver", &child);
   BSON_APPEND_UTF8 (&child, "name", md->driver_name);
   BSON_APPEND_UTF8 (&child, "version", md->driver_version);
   bson_append_document_end (doc, &child);

   BSON_APPEND_DOCUMENT_BEGIN (doc, "os", &child);
   BSON_APPEND_UTF8 (&child, "name",
                     _string_or_empty (md->os_name));
   BSON_APPEND_UTF8 (&child, "architecture",
                     _string_or_empty (md->os_architecture));
   BSON_APPEND_UTF8 (&child, "version", _string_or_empty (md->os_version));
   bson_append_document_end (doc, &child);

   if (doc->len > METADATA_MAX_SIZE) {
      /* All of the fields we've added so far have been truncated to some
       * limit, so there's no preventing us from going over the limit. */
      return false;
   }

   /* Try to add platform */
   max_platform_str_size = METADATA_MAX_SIZE -
                           (doc->len +
                            /* 1 byte for utf8 tag */
                            1 +

                            /* key size */
                            (uint32_t) strlen (METADATA_PLATFORM_FIELD) + 1 +

                            /* 4 bytes for length of string */
                            4);

   /* need at least 1 byte for that null terminator, and all of the fields
    * above shouldn't add up to nearly 500 bytes */
   BSON_ASSERT (max_platform_str_size > 0);

   if (max_platform_str_size < strlen (md->platform)) {
      platform_copy = bson_strndup (md->platform,
                                    max_platform_str_size - 1);
      BSON_ASSERT (strlen (platform_copy) <= max_platform_str_size);
   }

   BSON_APPEND_UTF8 (doc, METADATA_PLATFORM_FIELD,
                     platform_copy ? platform_copy : md->platform);
   bson_free (platform_copy);

   BSON_ASSERT (doc->len <= METADATA_MAX_SIZE);
   return true;
}

void
_mongoc_metadata_freeze ()
{
   gMongocMetadata.frozen = true;
}

static void
_truncate_if_needed (const char **s,
                     uint32_t     max_len)
{
   const char *tmp = *s;

   if (strlen (*s) > max_len) {
      *s = bson_strndup (*s, max_len);
      bson_free ((char *) tmp);
   }
}

bool
mongoc_metadata_append (const char *driver_name,
                        const char *driver_version,
                        const char *platform)
{
   if (gMongocMetadata.frozen) {
      return false;
   }

   _append_and_free (&gMongocMetadata.driver_name, driver_name);
   _truncate_if_needed (&gMongocMetadata.driver_name, METADATA_DRIVER_NAME_MAX);

   _append_and_free (&gMongocMetadata.driver_version, driver_version);
   _truncate_if_needed (&gMongocMetadata.driver_version,
                        METADATA_DRIVER_VERSION_MAX);

   _append_and_free (&gMongocMetadata.platform, platform);

   _mongoc_metadata_freeze ();
   return true;
}

static void
_lsb_copy_val_if_need (const char *val,
                       char      **buffer)
{
   size_t sz;

   if (*buffer) {
      /* We've encountered this key more than once. This means the file is
       * weird, so just keep first copy */
      return;
   }

   sz = strlen (val) + 1;

   /* Technically we could compute val_len, but these strings are tiny */
   *buffer = bson_malloc (sz);
   bson_strncpy (*buffer, val, sz);
}

static bool
_lsb_process_line (char      **name,
                   char      **version,
                   const char *line)
{
   size_t key_len;
   const char *equal_sign;
   const char *val;

   const size_t line_len = strlen (line);

   const char *delim = "=";
   const size_t delim_len = strlen (delim);

   /* Figure out where = is. Everything before is the key, and after is val */
   equal_sign = strstr (line, delim);

   if (equal_sign == NULL) {
      /* This line is malformed/incomplete, so skip it */
      return false;
   }

   /* Should never happen since we null terminated this line */
   BSON_ASSERT (equal_sign < line + line_len);

   key_len = equal_sign - line;
   val = equal_sign + delim_len;

   /* If we find two copies of either key, the *name == NULL check will fail
    * so we will just keep the first value encountered. */
   if (strncmp (line, "DISTRIB_ID", key_len) == 0) {
      _lsb_copy_val_if_need (val, name);
      return true;
   } else if (strncmp (line, "DISTRIB_RELEASE", key_len) == 0) {
      _lsb_copy_val_if_need (val, version);
      return true;
   }

   return false;
}


bool
_mongoc_metadata_parse_lsb (const char *path,
                            char      **name,
                            char      **version)
{
   enum N { bufsize = 4096 };
   char buffer [bufsize];
   size_t buflen;

   char *line;
   char *line_end;

   size_t cnt = 0;
   FILE *f;

   f = fopen (path, "r");

   if (!f) {
      return false;
   }

   /* Read first 4k bytes into buffer. Want to bound amount of time spent
    * reading this file. If the file is super long, we may read an incomplete
    * or unfinished line, but we're ok with that */
   buflen = fread (buffer, sizeof (char), bufsize - 1, f);
   buffer [buflen] = '\0';

   while (cnt < buflen) {
      line = buffer + cnt;

      /* Find end of this line */
      line_end = strstr (buffer + cnt, "\n");

      if (line_end) {
         *line_end = '\0';
      } else {
         line_end = &buffer[buflen];
         BSON_ASSERT (*line_end == '\0');
      }

      cnt += (line_end - line + 1);

      _lsb_process_line (name, version, line);

      if (*version && *name) {
         /* No point in reading any more */
         break;
      }
   }

   fclose (f);
   return *version && *name;
}

/*
 * Read whole first line or first 256 bytes, whichever is smaller
 * -path is optional, only used for logging errors
 * -It's your job to free the return value of this function
 */
static char *
_read_first_line_up_to_limit (const char *path)
{
   enum N { bufsize = 256 };
   char buffer[bufsize];
   char *ret = NULL;
   char *fgets_res;
   size_t len;
   FILE *f;

   f = fopen (path, "r");

   if (!f) {
      MONGOC_WARNING ("Couldn't open %s: error %d", path, errno);
      return NULL;
   }

   fgets_res = fgets (buffer, bufsize, f);

   if (fgets_res) {
      len = strlen (buffer);

      if (buffer[len - 1] == '\n') {
         /* get rid of newline */
         buffer[len - 1] = '\0';
         len--;
      }

      ret = bson_malloc (len + 1);
      bson_strncpy (ret, buffer, len + 1);
   } else if (ferror (f)) {
      MONGOC_WARNING ("Could open but not read from %s, error: %d",
                      path ? path : "<unkown>", errno);
   } else {
      /* The file is empty. Weird. Return null */
   }

   fclose (f);

   return ret;
}

/*
 * Find the first string in a list which is a valid file.
 * Technically there's always a race condition when using this function
 * since immediately after it returns, the file could get removed, so
 * only use this for files which should never be removed (and check for
 * NULL when you fopen again)
 */
static const char *
_get_first_existing (const char **paths)
{
   const char **p = &paths[0];
   FILE *f;

   for (; *p != NULL; p++) {
      f = fopen (*p, "r");

      if (f) {
         fclose (f);
         return *p;
      }
   }

   return NULL;
}

char *
_mongoc_metadata_get_version_from_osrelease (const char *path)
{
   /* Read from something like /proc/sys/kernel/osrelease */
   BSON_ASSERT (path);
   return _read_first_line_up_to_limit (path);
}

char *
_mongoc_metadata_get_osname_from_release_file (const char *path)
{
   BSON_ASSERT (path);
   return _read_first_line_up_to_limit (path);
}

static char *
_find_and_read_release_file ()
{
   const char *path;
   const char *paths [] = {
      "/etc/system-release",
      "/etc/redhat-release",
      "/etc/novell-release",
      "/etc/gentoo-release",
      "/etc/SuSE-release",
      "/etc/SUSE-release",
      "/etc/sles-release",
      "/etc/debian_release",
      "/etc/slackware-version",
      "/etc/centos-release",
      "/etc/os-release",
      NULL,
   };

   path = _get_first_existing (paths);

   if (!path) {
      return NULL;
   }

   return _mongoc_metadata_get_osname_from_release_file (path);
}


static bool
_get_linux_distro (char **name,
                   char **version)
{
   const char *lsb_path = "/etc/lsb-release";
   const char *osrelease_path = "/proc/sys/kernel/osrelease";

   *name = NULL;
   *version = NULL;

   if (_mongoc_metadata_parse_lsb (lsb_path, name, version)) {
      return true;
   }

   /* Otherwise get the name from the "release" file and version from
    * /proc/sys/kernel/osrelease */
   *name = _find_and_read_release_file ();
   *version = _mongoc_metadata_get_version_from_osrelease (osrelease_path);

   return (*name != NULL) && (*version != NULL);
}
