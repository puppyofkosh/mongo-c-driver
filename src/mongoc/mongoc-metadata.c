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

static void
_get_system_info (mongoc_metadata_t *metadata)
{
   /* Dummy function to be filled in later */
   metadata->os_type = bson_strndup ("unknown", METADATA_OS_TYPE_MAX);
   metadata->os_name = bson_strndup ("unknown", METADATA_OS_NAME_MAX);
   metadata->os_version = NULL;
   metadata->os_architecture = NULL;
   /* General idea of what these are supposed to be: */
   /* metadata->os_version = bson_strndup ("123", METADATA_OS_VERSION_MAX); */
   /* metadata->os_architecture = bson_strndup ("ARM", */
   /*                                           METADATA_OS_ARCHITECTURE_MAX); */
}

static void
_free_system_info (mongoc_metadata_t *meta)
{
   bson_free (meta->os_type);
   bson_free (meta->os_name);
   bson_free (meta->os_version);
   bson_free (meta->os_architecture);
}

static void
_get_driver_info (mongoc_metadata_t *metadata)
{
   metadata->driver_name = bson_strndup ("mongoc",
                                         METADATA_DRIVER_NAME_MAX);
   metadata->driver_version = bson_strndup (MONGOC_VERSION_S,
                                            METADATA_DRIVER_VERSION_MAX);
}

static void
_free_driver_info (mongoc_metadata_t *metadata)
{
   bson_free (metadata->driver_name);
   bson_free (metadata->driver_version);
}

static void
_set_platform_string (mongoc_metadata_t *metadata)
{
   metadata->platform = NULL;
}

static void
_free_platform_string (mongoc_metadata_t *metadata)
{
   bson_free (metadata->platform);
}

void
_mongoc_metadata_init (void)
{
   _get_system_info (&gMongocMetadata);
   _get_driver_info (&gMongocMetadata);
   _set_platform_string (&gMongocMetadata);

   gMongocMetadata.frozen = false;
}

void
_mongoc_metadata_cleanup (void)
{
   _free_system_info (&gMongocMetadata);
   _free_driver_info (&gMongocMetadata);
   _free_platform_string (&gMongocMetadata);
}

static bool
_append_platform_field (bson_t     *doc,
                        const char *platform)
{
   int max_platform_str_size;
   char *platform_copy = NULL;

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

   if (max_platform_str_size < strlen (platform)) {
      platform_copy = bson_strndup (platform,
                                    max_platform_str_size - 1);
      BSON_ASSERT (strlen (platform_copy) <= max_platform_str_size);
   }

   BSON_APPEND_UTF8 (doc, METADATA_PLATFORM_FIELD,
                     platform_copy ? platform_copy : platform);
   bson_free (platform_copy);
   BSON_ASSERT (doc->len <= METADATA_MAX_SIZE);
   return true;
}

/*
 * Return true if we build the document, and it's not too big
 * false if there's no way to prevent the doc from being too big. In this
 * case, the caller shouldn't include it with isMaster
 */
bool
_mongoc_metadata_build_doc_with_application (bson_t     *doc,
                                             const char *appname)
{
   const mongoc_metadata_t *md = &gMongocMetadata;
   bson_t child;

   if (appname) {
      BSON_APPEND_DOCUMENT_BEGIN (doc, "application", &child);
      BSON_APPEND_UTF8 (&child, "name", appname);
      bson_append_document_end (doc, &child);
   }

   BSON_APPEND_DOCUMENT_BEGIN (doc, "driver", &child);
   BSON_APPEND_UTF8 (&child, "name", md->driver_name);
   BSON_APPEND_UTF8 (&child, "version", md->driver_version);
   bson_append_document_end (doc, &child);

   BSON_APPEND_DOCUMENT_BEGIN (doc, "os", &child);

   BSON_ASSERT (md->os_type);
   BSON_APPEND_UTF8 (&child, "type", md->os_type);

   /* OS Type is required */
   if (md->os_name) {
      BSON_APPEND_UTF8 (&child, "name", md->os_name);
   }

   if (md->os_version) {
      BSON_APPEND_UTF8 (&child, "version", md->os_version);
   }

   if (md->os_architecture) {
      BSON_APPEND_UTF8 (&child, "architecture", md->os_architecture);
   }

   bson_append_document_end (doc, &child);

   if (doc->len > METADATA_MAX_SIZE) {
      /* We've done all we can possibly do to ensure the current
       * document is below the maxsize, so if it overflows there is
       * nothing else we can do, so we fail */
      return false;
   }

   if (md->platform) {
      _append_platform_field (doc, md->platform);
   }

   return true;
}

void
_mongoc_metadata_freeze (void)
{
   gMongocMetadata.frozen = true;
}

static void
_append_and_truncate (char      **s,
                      const char *suffix,
                      int         max_len)
{
   char *tmp = *s;
   const int delim_len = strlen (" / ");
   int space_for_suffix;
   int base_len = strlen (*s);

   BSON_ASSERT (*s);

   if (!suffix) {
      return;
   }

   space_for_suffix = max_len - base_len - delim_len;
   BSON_ASSERT (space_for_suffix >= 0);

   *s = bson_strdup_printf ("%s / %.*s", tmp, space_for_suffix, suffix);
   BSON_ASSERT (strlen (*s) <= max_len);
   bson_free (tmp);
}

bool
mongoc_metadata_append (const char *driver_name,
                        const char *driver_version,
                        const char *platform)
{
   int max_size = 0;
   if (gMongocMetadata.frozen) {
      return false;
   }

   _append_and_truncate (&gMongocMetadata.driver_name, driver_name,
                         METADATA_DRIVER_NAME_MAX);

   _append_and_truncate (&gMongocMetadata.driver_version, driver_version,
                         METADATA_DRIVER_VERSION_MAX);

   if (platform) {
      if (gMongocMetadata.platform) {
         /* Upper bound on the max size of this string, not exact. */
         max_size = METADATA_MAX_SIZE -
            - _mongoc_strlen_or_zero (gMongocMetadata.os_type)
            - _mongoc_strlen_or_zero (gMongocMetadata.os_name)
            - _mongoc_strlen_or_zero (gMongocMetadata.os_version)
            - _mongoc_strlen_or_zero (gMongocMetadata.os_architecture)
            - _mongoc_strlen_or_zero (gMongocMetadata.driver_name)
            - _mongoc_strlen_or_zero (gMongocMetadata.driver_version);

         _append_and_truncate (&gMongocMetadata.platform, platform, max_size);
      } else {
         gMongocMetadata.platform = bson_strdup_printf (" / %s", platform);
      }
   }

   _mongoc_metadata_freeze ();
   return true;
}

mongoc_metadata_t *
_mongoc_metadata_get ()
{
   return &gMongocMetadata;
}
