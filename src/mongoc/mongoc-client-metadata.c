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


#ifdef _WIN32
static char*
_windows_get_version_string ()
{
   /*
      As new versions of windows are released, we'll have to add to this
      See:
      https://msdn.microsoft.com/en-us/library/windows/desktop/ms724832(v=vs.85).aspx
      For windows names -> version # mapping
   */

   if (IsWindowsVersionOrGreater (10, 0, 0)) {
      /* No IsWindows10OrGreater () function available with this version of
         MSVC */
      return bson_strdup (">= Windows 10");
   } else if (IsWindowsVersionOrGreater (6, 3, 0)) {
      /* No IsWindows8Point10OrGreater() function available with this version
         of MSVC */
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

static char*
_windows_get_arch_string ()
{
   SYSTEM_INFO system_info;
   DWORD arch;

   /* doesn't return anything */
   GetSystemInfo(&system_info);

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
   const char* result_str;

   metadata->os_name = bson_strdup ("Windows");
   metadata->os_version = windows_get_version_string ();
   metadata->os_architecture = windows_get_arch_string ();
}
#else
static void
_get_system_info (mongoc_client_metadata_t *metadata)
{
   struct utsname system_info;
   int res;

   res = uname (&system_info);

   if (res != 0) {
      MONGOC_ERROR ("Uname failed with error %d", errno);
      return;
   }

   metadata->os_name = bson_strdup (system_info.sysname);
   metadata->os_architecture = bson_strdup (system_info.machine);
   metadata->os_version = bson_strdup (system_info.release);
}
#endif

void
_mongoc_client_metadata_init ()
{
   /* Do OS detection here */
   _get_system_info (&gMongocMetadata);

   gMongocMetadata.driver_name = bson_strdup ("mongoc");
   gMongocMetadata.driver_version = bson_strdup (MONGOC_VERSION_S);

   /* TODO: CFLAGS=%s, MONGOC_CFLAGS */
   gMongocMetadata.platform = bson_strdup_printf ("CC=%s ./configure %s",
                                             MONGOC_CC,
                                             MONGOC_CONFIGURE_ARGS);
   gMongocMetadata.frozen = false;
}

void
_mongoc_client_metadata_cleanup ()
{
   bson_free ((char*)gMongocMetadata.os_version);
   bson_free ((char*)gMongocMetadata.os_name);
   bson_free ((char*)gMongocMetadata.os_architecture);

   bson_free ((char*)gMongocMetadata.driver_name);
   bson_free ((char*)gMongocMetadata.driver_version);
   bson_free ((char*)gMongocMetadata.platform);
}

static void
_append_and_free (const char **s, const char *suffix)
{
   const char* tmp = *s;
   if (suffix) {
      *s = bson_strdup_printf ("%s / %s", tmp, suffix);
      bson_free ((char*)tmp);
   }
}


void
_build_metadata_doc_with_application (bson_t *doc,
                                      const char *application)
{

   uint32_t max_platform_str_size;
   uint32_t platform_len;
   char *platform_copy = NULL;

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
                "version", BCON_UTF8 (STRING_OR_EMPTY (gMongocMetadata.os_version)),
                "}");


   if (application) {
      BCON_APPEND (doc, "application", application);
   }

   if (doc->len > METADATA_MAX_SIZE) {
      /* FIXME: What to do here?*/
      MONGOC_ERROR ("Metadata is too big!");
      return;
   }

   /* Try to add platform */
   max_platform_str_size = METADATA_MAX_SIZE -
      (doc->len +

       /* 1 byte for utf8 tag */
       1 +

       /* key size */
       (uint32_t)strlen (METADATA_PLATFORM_FIELD) + 1 +

       /* 4 bytes for length of string */
       4);

   platform_len = strlen (gMongocMetadata.platform);
   if (max_platform_str_size < platform_len) {
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

bool
_mongoc_client_metadata_set_application (mongoc_topology_t *topology,
                                         const char *application)
{
   if (_mongoc_topology_is_scanner_active (topology)) {
      return false;
   }

   if (strlen (application) > MONGOC_METADATA_APPLICATION_NAME_MAX) {
      return false;
   }

   if (topology->scanner->metadata_application != NULL) {
      /* We've already set it */
      return false;
   }

   topology->scanner->metadata_application = bson_strdup (application);

   return true;
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
   _append_and_free (&gMongocMetadata.driver_version, driver_version);
   _append_and_free (&gMongocMetadata.platform, platform);

   _mongoc_client_metadata_freeze ();
   return true;
}
