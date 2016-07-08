#include "mongoc-client-metadata.h"
#include "mongoc-client-metadata-private.h"
#include "mongoc-client.h"
#include "mongoc-client-private.h"
#include "mongoc-error.h"
#include "mongoc-version.h"
#include "mongoc-log.h"

#ifdef _WIN32
#include <windows.h>
#include <stdio.h>
#include <VersionHelpers.h>
#else
#include <sys/utsname.h>
#endif

#define STRING_OR_EMPTY(s) ((s) != NULL ? (s) : "")

static mongoc_client_metadata_t gMongocMetadata;

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

   return bf;
}


#ifdef _WIN32
static char *
_windows_get_version_string ()
{
   /*
    * As new versions of windows are released, we'll have to add to this
    * See:
    * https://msdn.microsoft.com/en-us/library/windows/desktop/ms724832(v=vs.85).aspx
    * For windows names -> version # mapping
    */

   if (IsWindowsVersionOrGreater (10, 0, 0)) {
      /* No IsWindows10OrGreater () function available with this version of
       * MSVC */
      return bson_strdup (">= Windows 10");
   } else if (IsWindowsVersionOrGreater (6, 3, 0)) {
      /* No IsWindows8Point10OrGreater() function available with this version
       * of MSVC */
      return bson_strdup ("Windows 8.1");
   } else if (IsWindows8OrGreater ()) {
      return bson_strdup ("Windows 8");
   } else if (IsWindows7SP1OrGreater ()) {
      return bson_strdup ("Windows 7.1");
   } else if (IsWindows7OrGreater ()) {
      return bson_strdup ("Windows 7");
   } else if (IsWindowsVistaOrGreater ()) {
      return bson_strdup ("Windows Vista");
   } else if (IsWindowsXPOrGreater ()) {
      return bson_strdup ("Windows XP");
   }

   return bson_strdup ("Pre Windows XP");
}

static char *
_windows_get_arch_string ()
{
   SYSTEM_INFO system_info;
   DWORD arch;

   /* doesn't return anything */
   GetSystemInfo (&system_info);

   arch = system_info.wProcessorArchitecture;

   if (arch == PROCESSOR_ARCHITECTURE_AMD64) {
      return bson_strdup ("x86_64");
   } else if (arch == PROCESSOR_ARCHITECTURE_ARM) {
      return bson_strdup ("ARM");
   } else if (arch == PROCESSOR_ARCHITECTURE_IA64) {
      return bson_strdup ("IA64");
   } else if (arch == PROCESSOR_ARCHITECTURE_INTEL) {
      return bson_strdup ("x86");
   } else if (arch == PROCESSOR_ARCHITECTURE_UNKNOWN) {
      return bson_strdup ("Unknown");
   }

   MONGOC_ERROR ("Processor architecture lookup failed");

   return NULL;
}

static void
_get_system_info (mongoc_client_metadata_t *metadata)
{
   const char *result_str;

   metadata->os_name = bson_strdup ("Windows");
   metadata->os_version = windows_get_version_string ();
   metadata->os_architecture = windows_get_arch_string ();
}
#else
static void
_get_system_info (mongoc_client_metadata_t *meta)
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

void
_mongoc_client_metadata_init ()
{
   const char *driver_name = "mongoc";

   /* Do OS detection here */
   _get_system_info (&gMongocMetadata);

   BSON_ASSERT (strlen (driver_name) < METADATA_DRIVER_NAME_MAX);
   gMongocMetadata.driver_name = bson_strdup (driver_name);

   BSON_ASSERT (strlen (MONGOC_VERSION_S) < METADATA_DRIVER_NAME_MAX);
   gMongocMetadata.driver_version = bson_strdup (MONGOC_VERSION_S);

   /* TODO: CFLAGS=%s, MONGOC_CFLAGS */
   gMongocMetadata.platform = bson_strdup_printf (
      "cfgbits 0x%x CC=%s CFLAGS=%s",
      get_config_bitfield (),
      MONGOC_CC,
      MONGOC_CFLAGS);

   gMongocMetadata.frozen = false;
}

void
_mongoc_client_metadata_cleanup ()
{
   bson_free ((char *) gMongocMetadata.os_version);
   bson_free ((char *) gMongocMetadata.os_name);
   bson_free ((char *) gMongocMetadata.os_architecture);

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

void
_build_metadata_doc_with_application (bson_t     *doc,
                                      const char *application)
{
   uint32_t max_platform_str_size;
   char *platform_copy = NULL;

   /* Todo: Add os.version, strip and if necessary */
   if (application) {
      BCON_APPEND (doc, "application", application);
   }

   BSON_ASSERT (doc->len < METADATA_MAX_SIZE);

   BCON_APPEND (doc,
                "driver", "{",
                "name", gMongocMetadata.driver_name,
                "version", gMongocMetadata.driver_version,
                "}");

   BCON_APPEND (doc,
                "os", "{",
                "name", BCON_UTF8 (STRING_OR_EMPTY (gMongocMetadata.os_name)),
                "architecture",
                BCON_UTF8 (STRING_OR_EMPTY (gMongocMetadata.os_architecture)),
                "version",
                BCON_UTF8 (STRING_OR_EMPTY (gMongocMetadata.os_version)),
                "}");

   if (doc->len > METADATA_MAX_SIZE) {
      /* All of the fields we've added so far have been truncated to some
       * limit, so this should never happen. */
      MONGOC_ERROR ("Metadata doc is too big!");
      abort ();
      return;
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

   if (max_platform_str_size < strlen (gMongocMetadata.platform)) {
      platform_copy = bson_strndup (gMongocMetadata.platform,
                                    max_platform_str_size - 1);
      BSON_ASSERT (strlen (platform_copy) <= max_platform_str_size);
   }

   BCON_APPEND (doc,
                METADATA_PLATFORM_FIELD,
                BCON_UTF8 ((platform_copy ? platform_copy :
                            gMongocMetadata.platform)));
   bson_free (platform_copy);
}

void
_mongoc_client_metadata_freeze ()
{
   gMongocMetadata.frozen = true;
}

bool
mongoc_set_client_metadata (const char *driver_name,
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

   _mongoc_client_metadata_freeze ();
   return true;
}
