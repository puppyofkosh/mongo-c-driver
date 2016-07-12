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
   int max_platform_str_size;
   char *platform_copy = NULL;
   const mongoc_metadata_t *md = &gMongocMetadata;
   bson_t child;

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
                            strlen (METADATA_PLATFORM_FIELD) + 1 +

                            /* 4 bytes for length of string */
                            4);

   /* need at least 1 byte for that null terminator, and all of the fields
    * above shouldn't add up to nearly 500 bytes */
   if (max_platform_str_size <= 0) {
      return false;
   }

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

/* Used for testing if we want to set the OS name to some specific string */
void
_mongoc_metadata_override_os_name (const char *name) {
   bson_free ((char*)gMongocMetadata.os_name);
   gMongocMetadata.os_name = bson_strdup (name);
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
