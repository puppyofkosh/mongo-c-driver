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

#include "mongoc-error.h"
#include "mongoc-log.h"
#include "mongoc-version.h"
#include "mongoc-util-private.h"

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
   if (name_key_len == key_len &&
       strncmp (line, name_key, key_len) == 0 &&
       !(*name)) {
      *name = bson_strdup (val);
      return true;
   } else if (version_key_len == key_len &&
              strncmp (line, version_key, key_len) == 0 &&
              !(*version)) {
      *version = bson_strdup (val);
      return true;
   }

   return false;
}


/*
 * Parse a file of the form:
 * KEY=VALUE
 * Looking for name_key and version_key, and storing
 * their values into *name and *version.
 * The values in *name and *version must be freed with bson_free.
 */
static bool
_read_key_val_file (const char  *path,
                    const char  *name_key,
                    char       **name,
                    const char  *version_key,
                    char       **version)
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

      _process_line (name_key, name, version_key, version, line);

      if (*version && *name) {
         /* No point in reading any more */
         break;
      }
   }

   fclose (f);
   return *version && *name;
}

bool
_mongoc_linux_distro_scanner_read_lsb (const char  *path,
                                       char       **name,
                                       char       **version)
{
   return _read_key_val_file (path,
                              "DISTRIB_ID",
                              name,
                              "DISTRIB_RELEASE",
                              version);
}

bool
_mongoc_linux_distro_scanner_read_etc_os_release (const char  *path,
                                                  char       **name,
                                                  char       **version)
{
   return _read_key_val_file (path,
                              "ID",
                              name,
                              "VERSION_ID",
                              version);
}

bool
_mongoc_linux_distro_scanner_get_distro (char **name,
                                         char **version)
{
   const char *lsb_path = "/etc/lsb-release";
   const char *etc_os_release_path = "/etc/os-release";

   *name = NULL;
   *version = NULL;

   _mongoc_linux_distro_scanner_read_etc_os_release (etc_os_release_path,
                                                     name, version);

   if (*name && *version) {
      return true;
   }

   _mongoc_linux_distro_scanner_read_lsb (lsb_path, name, version);

   if (*name && *version) {
      return true;
   }

   /* TODO: Otherwise get name from the "release" file like etc/fedora-release
    * and kernel version from proc/sys/kernel/osrelease */

   return (*name != NULL) && (*version != NULL);
}
