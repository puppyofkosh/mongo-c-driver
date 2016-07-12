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
   /* FIXME TODO Dummy function to be filled in later */
   metadata->os_name = bson_strndup ("Mac OSX Something", METADATA_OS_NAME_MAX);
   metadata->os_version = bson_strndup ("123", METADATA_OS_VERSION_MAX);
   metadata->os_architecture = bson_strndup ("ARM",
                                             METADATA_OS_ARCHITECTURE_MAX);
}

static void
_free_system_info (mongoc_metadata_t *meta)
{
   bson_free ((char *) meta->os_version);
   bson_free ((char *) meta->os_name);
   bson_free ((char *) meta->os_architecture);
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
   bson_free ((char *) metadata->driver_name);
   bson_free ((char *) metadata->driver_version);
}

static void
_set_platform_string (mongoc_metadata_t *metadata)
{
   const char *v = "CC=gcc CFLAGS=-Werror SSL_CFLAGS=whatever";
   gMongocMetadata.platform = bson_strdup (v);
}

static void
_free_platform_string (mongoc_metadata_t *metadata)
{
   bson_free ((char*) metadata->platform);
}

void
_mongoc_metadata_init ()
{
   /* Do OS detection here */
   _get_system_info (&gMongocMetadata);
   _get_driver_info (&gMongocMetadata);
   _set_platform_string (&gMongocMetadata);

   gMongocMetadata.frozen = false;
}

void
_mongoc_metadata_cleanup ()
{
   _free_system_info (&gMongocMetadata);
   _free_driver_info (&gMongocMetadata);
   _free_platform_string (&gMongocMetadata);
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
                     _mongoc_string_or_empty (md->os_name));
   BSON_APPEND_UTF8 (&child, "architecture",
                     _mongoc_string_or_empty (md->os_architecture));
   BSON_APPEND_UTF8 (&child, "version", _mongoc_string_or_empty (md->os_version));
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

/* Used for testing if we want to set the OS name to some specific string */
void
_mongoc_metadata_override_os_name (const char *name)
{
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
