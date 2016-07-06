#include "mongoc-client-metadata.h"
#include "mongoc-client-metadata-private.h"
#include "mongoc-client.h"
#include "mongoc-client-private.h"
#include "mongoc-error.h"
#include "mongoc-version.h"
#include "mongoc-log.h"

#ifndef _WIN32
#include <sys/utsname.h>
#else
#include <windows.h>
#include <stdio.h>
#include <VersionHelpers.h>
#endif

#define STRING_OR_EMPTY(s) ((s) != NULL ? (s) : "")

static mongoc_client_metadata_t g_metadata;


#ifndef _WIN32
static void _get_system_info (const char** name, const char** architecture,
                              const char** version)
{
   struct utsname system_info;
   int res;

   res = uname (&system_info);

   if (res != 0) {
      MONGOC_ERROR ("Uname failed with error %d", errno);
      return;
   }

   *name = bson_strdup (system_info.sysname);
   *architecture = bson_strdup (system_info.machine);
   *version = bson_strdup (system_info.release);
}
#else
static char* _windows_get_version_string ()
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

static char* _windows_get_arch_string ()
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
      return bson_strdup ("Unkown");
   }

   MONGOC_ERROR ("Processor architecture lookup failed");

   return NULL;
}

static void _get_system_info (const char** name, const char** architecture,
                              const char** version)
{
   const char* result_str;

   *name = bson_strdup ("Windows");
   *version = windows_get_version_string ();
   *architecture = windows_get_arch_string ();
}
#endif

void _mongoc_client_metadata_init () {
   /* Do OS detection here */
   _get_system_info (&g_metadata.os_version,
                     &g_metadata.os_name,
                     &g_metadata.architecture);

   g_metadata.driver_name = bson_strdup ("mongoc");
   g_metadata.driver_version = bson_strdup (MONGOC_VERSION_S);

   /* TODO: CFLAGS=%s, MONGOC_CFLAGS */
   g_metadata.platform = bson_strdup_printf ("CC=%s ./configure %s",
                                             MONGOC_CC,
                                             MONGOC_CONFIGURE_ARGS);
   g_metadata.frozen = false;
}

void _mongoc_client_metadata_cleanup () {
   bson_free ((char*)g_metadata.os_version);
   bson_free ((char*)g_metadata.os_name);
   bson_free ((char*)g_metadata.architecture);

   bson_free ((char*)g_metadata.driver_name);
   bson_free ((char*)g_metadata.driver_version);
   bson_free ((char*)g_metadata.platform);
}

static void _append_and_free (const char** s, const char* suffix) {
   const char* tmp = *s;
   if (suffix) {
      *s = bson_strdup_printf ("%s / %s", tmp, suffix);
      bson_free ((char*)tmp);
   }
}


void _build_metadata_doc_with_application (bson_t* doc,
                                           const char* application) {

   uint32_t max_platform_str_size;
   uint32_t platform_len;
   char *platform_copy = NULL;

   BCON_APPEND (doc,
                "driver", "{",
                "name", g_metadata.driver_name,
                "version", g_metadata.driver_version,
                "}");

   BCON_APPEND (doc,
                "os", "{",
                "name", BCON_UTF8 (STRING_OR_EMPTY (g_metadata.os_name)),
                "architecture",
                BCON_UTF8 (STRING_OR_EMPTY (g_metadata.architecture)),
                "version", BCON_UTF8 (STRING_OR_EMPTY (g_metadata.os_version)),
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

   platform_len = strlen (g_metadata.platform);
   if (max_platform_str_size < platform_len) {
      platform_copy = bson_strndup (g_metadata.platform,
                                    max_platform_str_size - 1);
      BSON_ASSERT (strlen (platform_copy) <= max_platform_str_size);
   }

   BCON_APPEND (doc,
                METADATA_PLATFORM_FIELD,
                BCON_UTF8 ((platform_copy ? platform_copy :
                            g_metadata.platform)));
   bson_free (platform_copy);
}

bool
_mongoc_client_metadata_set_application (mongoc_topology_t *topology,
                                         const char *application) {
   if (_mongoc_topology_is_scanner_active (topology)) {
      return false;
   }

   if (strlen (application) > MONGOC_METADATA_APPLICATION_NAME_MAX_LENGTH) {
      return false;
   }

   if (topology->scanner->metadata_application != NULL) {
      /* We've already set it */
      return false;
   }

   topology->scanner->metadata_application = bson_strdup (application);

   return true;
}

void _mongoc_client_metadata_freeze () {
   g_metadata.frozen = true;
}

bool mongoc_set_client_metadata (const char *driver_name,
                                 const char *driver_version,
                                 const char *platform) {
   if (g_metadata.frozen) {
      return false;
   }

   _append_and_free (&g_metadata.driver_name, driver_name);
   _append_and_free (&g_metadata.driver_version, driver_version);
   _append_and_free (&g_metadata.platform, platform);

   g_metadata.frozen = true;
   return true;
}
