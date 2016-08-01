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

#ifdef MONGOC_OS_IS_LINUX
#include <stdio.h>

#include "mongoc-error.h"
#include "mongoc-log.h"
#include "mongoc-trace.h"
#include "mongoc-util-private.h"
#include "mongoc-version.h"


static bool
_process_line (const char  *name_key,
               char       **name,
               const char  *version_key,
               char       **version,
               const char  *line)
{
   size_t key_len;
   const char *equal_sign;
   const char *val;

   const size_t line_len = strlen (line);

   const char *delim = "=";
   const size_t delim_len = strlen (delim);
   const size_t name_key_len = strlen (name_key);
   const size_t version_key_len = strlen (version_key);

   ENTRY;

   /* Figure out where = is. Everything before is the key, and after is val */
   equal_sign = strstr (line, delim);

   if (equal_sign == NULL) {
      TRACE ("Encountered malformed line: %s", line);
      /* This line is malformed/incomplete, so skip it */
      RETURN (false);
   }

   /* Should never happen since we null terminated this line */
   BSON_ASSERT (equal_sign < line + line_len);

   key_len = equal_sign - line;
   val = equal_sign + delim_len;

   /* If we find two copies of either key, the *name == NULL check will fail
    * so we will just keep the first value encountered. */
   if (name_key_len == key_len &&
       strncmp (line, name_key, key_len) == 0 &&
       !(*name)) {
      *name = bson_strdup (val);
      RETURN (true);
   } else if (version_key_len == key_len &&
              strncmp (line, version_key, key_len) == 0 &&
              !(*version)) {
      *version = bson_strdup (val);
      RETURN (true);
   }

   RETURN (false);
}


/*
 * Parse a file of the form:
 * KEY=VALUE
 * Looking for name_key and version_key, and storing
 * their values into *name and *version.
 * The values in *name and *version must be freed with bson_free.
 */
bool
_mongoc_linux_distro_scanner_read_key_val_file (const char  *path,
                                                const char  *name_key,
                                                char       **name,
                                                const char  *version_key,
                                                char       **version)
{
   const int max_lines = 100;
   int lines_read = 0;

   char *buffer = NULL;
   size_t buffer_size;
   ssize_t bytes_read;

   size_t len;

   FILE *f;

   ENTRY;

   *name = NULL;
   *version = NULL;

   if (access (path, R_OK)) {
      TRACE ("No permission to read from %s: errno: %d", path, errno);
      RETURN (false);
   }

   f = fopen (path, "r");

   if (!f) {
      TRACE ("fopen failed: %d", errno);
      RETURN (false);
   }

   while (lines_read < max_lines) {
      bytes_read = getline (&buffer, &buffer_size, f);
      if (bytes_read < 0) {
         /* Error or eof. The docs for getline () don't seem to give
          * us a way to distinguish, so just return. */
         break;
      } else if (bytes_read == 0) {
         /* Didn't read any characters. Again, leave */
         break;
      }

      len = strlen (buffer);
      /* We checked bytes_read > 0 */
      BSON_ASSERT (len > 0);
      if (buffer[len - 1] == '\n') {
         buffer[len - 1] = '\0';
      }

      _process_line (name_key, name, version_key, version, buffer);
      if (*version && *name) {
         /* No point in reading any more */
         break;
      }

      lines_read ++;
   }

   /* Must use standard free () here since buffer was malloced in getline */
   free (buffer);

   fclose (f);
   RETURN (*version && *name);
}

bool
_mongoc_linux_distro_scanner_get_distro (char **name,
                                         char **version)
{
   ENTRY;

   *name = NULL;
   *version = NULL;

   _mongoc_linux_distro_scanner_read_key_val_file ("/etc/os-release",
                                                   "ID",
                                                   name,
                                                   "VERSION_ID",
                                                   version);

   if (*name && *version) {
      RETURN (true);
   }

   bson_free (*name);
   bson_free (*version);

   _mongoc_linux_distro_scanner_read_key_val_file ("/etc/lsb-release",
                                                   "DISTRIB_ID",
                                                   name,
                                                   "DISTRIB_RELEASE",
                                                   version);

   if (*name && *version) {
      RETURN (true);
   }

   /* TODO: Otherwise get name from the "release" file like etc/fedora-release
    * and kernel version from uname */

   if (*name || *version) {
      RETURN (true);
   }

   bson_free (*name);
   bson_free (*version);

   RETURN ((*name != NULL) && (*version != NULL));
}

#endif
