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


#include <stdio.h>

#include "mongoc-error.h"
#include "mongoc-log.h"
#include "mongoc-metadata-os-private.h"
#include "mongoc-trace.h"
#include "mongoc-util-private.h"
#include "mongoc-version.h"

#ifdef MONGOC_OS_IS_LINUX
static void
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
      EXIT;
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
      EXIT;
   } else if (version_key_len == key_len &&
              strncmp (line, version_key, key_len) == 0 &&
              !(*version)) {
      *version = bson_strdup (val);
      EXIT;
   }

   EXIT;
}


/*
 * Parse a file of the form:
 * KEY=VALUE
 * Looking for name_key and version_key, and storing
 * their values into *name and *version.
 * The values in *name and *version must be freed with bson_free.
 */
void
_mongoc_linux_distro_scanner_read_key_val_file (const char  *path,
                                                const char  *name_key,
                                                char       **name,
                                                const char  *version_key,
                                                char       **version)
{
   const int max_lines = 100;
   int lines_read = 0;

   char *buffer = NULL;
   size_t buffer_size = 0;
   ssize_t bytes_read;

   size_t len;

   FILE *f;

   ENTRY;

   *name = NULL;
   *version = NULL;

   if (access (path, R_OK)) {
      TRACE ("No permission to read from %s: errno: %d", path, errno);
      EXIT;
   }

   f = fopen (path, "r");

   if (!f) {
      TRACE ("fopen failed: %d", errno);
      EXIT;
   }

   while (lines_read < max_lines) {
      bytes_read = getline (&buffer, &buffer_size, f);
      if (bytes_read <= 0) {
         /* Error or eof. The docs for getline () don't seem to give
          * us a way to distinguish, so just return. */
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
   EXIT;
}

/*
 * Find the first string in a list which is a valid file. Assumes
 * passed in list is NULL terminated!
 *
 * Technically there's always a race condition when using this function
 * since immediately after it returns, the file could get removed, so
 * only use this for files which should never be removed (and check for
 * NULL when you fopen again)
 */
const char *
_get_first_existing (const char **paths)
{
   const char **p = &paths[0];

   ENTRY;

   for (; *p != NULL; p++) {
      if (access (*p, F_OK)) {
         /* Just doesn't exist */
         continue;
      }

      if (access (*p, R_OK)) {
         TRACE ("file %s exists, but cannot be read: error %d", *p, errno);
         continue;
      }

      RETURN (*p);
   }

   RETURN (NULL);
}

void
_mongoc_linux_distro_scanner_split_line_by_release (const char *line,
                                                    char       **name,
                                                    char       **version)
{
   const char *delim_loc;
   const char * const delim = " release ";
   const char *version_string;

   *name = NULL;
   *version = NULL;

   delim_loc = strstr (line, delim);
   if (!delim_loc) {
      *name = bson_strdup (line);
      return;
   } else if (delim_loc == line) {
      /* The file starts with the word " release "
       * This file is weird enough we will just abandon it. */
      return;
   }

   *name = bson_strndup (line, delim_loc - line);

   version_string = delim_loc + strlen (delim);
   if (strlen (version_string) == 0) {
      /* Weird. The file just ended with "release " */
      return;
   }

   *version = bson_strdup (version_string);
}

/*
 * It seems like most release files follow the form
 * Version name release 1.2.3
 */
void
_mongoc_linux_distro_scanner_read_generic_release_file (const char **paths,
                                                        char       **name,
                                                        char       **version)
{
   const char *path;
   enum N { bufsize = 4096 };
   char buffer[bufsize];
   char *fgets_res;
   size_t len;
   FILE *f;

   ENTRY;

   *name = NULL;
   *version = NULL;

   path = _get_first_existing (paths);

   if (!path) {
      EXIT;
   }

   f = fopen (path, "r");
   if (!f) {
      TRACE ("Found %s exists and readable but couldn't open: %d",
             path, errno);
      EXIT;
   }

   /* Read the first line of the file, look for the word "release" */
   fgets_res = fgets (buffer, bufsize, f);
   if (!fgets_res) {
      /* Didn't read anything. Empty file or error. */
      if (ferror (f)) {
         TRACE ("Could open but not read from %s, error: %d",
                path ? path : "<unkown>", errno);
      }

      GOTO (cleanup);
   }

   len = strlen (buffer);
   if (len == 0) {
      GOTO (cleanup);
   }

   if (buffer[len - 1] == '\n') {
      /* get rid of newline */
      buffer[len - 1] = '\0';
   }

   /* Try splitting the string. If we can't it'll store everything in
    * *name. */
   _mongoc_linux_distro_scanner_split_line_by_release (buffer, name, version);

cleanup:
   fclose (f);

   EXIT;
}

bool
_mongoc_linux_distro_scanner_get_distro (char **name,
                                         char **version)
{
   /* In case we decide to try looking up name/version again */
   char *new_name;
   char *new_version;
   const char *generic_release_paths [] = {
      "/etc/redhat-release",
      "/etc/novell-release",
      "/etc/gentoo-release",
      "/etc/SuSE-release",
      "/etc/SUSE-release",
      "/etc/sles-release",
      "/etc/debian_release",
      "/etc/slackware-version",
      "/etc/centos-release",
      NULL,
   };

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


   /* Try to read from a generic release file, but if it doesn't work out, just
    * keep what we have*/
   _mongoc_linux_distro_scanner_read_generic_release_file (
      generic_release_paths, &new_name, &new_version);
   if (new_name) {
      bson_free (*name);
      *name = new_name;
   }

   if (new_version) {
      bson_free (*version);
      *version = new_version;
   }

   /* TODO: If no version, use uname */

   if (*name || *version) {
      RETURN (true);
   }

   bson_free (*name);
   bson_free (*version);

   RETURN (false);
}

#endif
