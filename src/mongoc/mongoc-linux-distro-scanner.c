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

/* getline() wrapper which does 2 things:
 * 1) Returns a string that does not have \0s embedded in it (just truncates
 * everything past first null), so this function is not binary-safe!
 * 2) Remove '\n' at the end of the string, if there is one.
 */
static ssize_t
_getline_wrapper (char **buffer, size_t *buffer_size, FILE *f)
{
   size_t len;
   ssize_t bytes_read;

   bytes_read = getline (buffer, buffer_size, f);
   if (bytes_read <= 0) {
      /* Error or eof. The docs for getline () don't seem to give
       * us a way to distinguish, so just return. */
      return bytes_read;
   }

   /* On some systems, getline may return a string that has '\0' embedded
    * in it. We'll ignore everything after the first '\0' */
   len = strlen (*buffer);
   /* We checked bytes_read > 0 */
   BSON_ASSERT (len > 0);
   if ((*buffer)[len - 1] == '\n') {
      (*buffer)[len - 1] = '\0';
   }
   return len;
}

static void
_process_line (const char  *name_key,
               size_t       name_key_len,
               char       **name,
               const char  *version_key,
               size_t       version_key_len,
               char       **version,
               const char  *line,
               size_t       line_len)
{
   size_t key_len;
   const char *equal_sign;
   const char *val;

   const char *delim = "=";

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
   val = equal_sign + strlen (delim);

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
                                                int          name_key_len,
                                                char       **name,
                                                const char  *version_key,
                                                int          version_key_len,
                                                char       **version)
{
   const int max_lines = 100;
   int lines_read = 0;

   char *buffer = NULL;
   size_t buffer_size = 0;
   ssize_t bytes_read;

   FILE *f;

   ENTRY;

   *name = NULL;
   *version = NULL;

   if (name_key_len < 0) {
      name_key_len = (int)strlen (name_key);
   }

   if (version_key_len < 0) {
      version_key_len = (int)strlen (version_key);
   }

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
      bytes_read = _getline_wrapper (&buffer, &buffer_size, f);
      if (bytes_read <= 0) {
         /* Error or eof */
         break;
      }

      _process_line (name_key, name_key_len, name,
                     version_key, version_key_len, version,
                     buffer, (size_t)bytes_read);
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


/*
 * Given a line of text, split it by the word "release." For example:
 * Ubuntu release 14.04 =>
 * *name = Ubuntu
 * *version = 14.04
 */
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
 * Search for a *-release file, and read its contents.
 */
void
_mongoc_linux_distro_scanner_read_generic_release_file (const char **paths,
                                                        char       **name,
                                                        char       **version)
{
   const char *path;
   ssize_t bytes_read;
   char *buffer = NULL;
   size_t buffer_size = 0;
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
   bytes_read = _getline_wrapper (&buffer, &buffer_size, f);
   if (bytes_read <= 0) {
      /* Error or eof. */
      GOTO (cleanup);
   }

   /* Try splitting the string. If we can't it'll store everything in
    * *name. */
   _mongoc_linux_distro_scanner_split_line_by_release (buffer, name, version);

cleanup:
   /* use regular free() on buffer since it's malloced by getline */
   free (buffer);
   fclose (f);

   EXIT;
}

/*
 * Some boilerplate logic that tries to set *name and *version to new_name
 * and new_version if it's not already set. Values of new_name and new_version
 * should not be used after this call.
 */
static bool
_overwrite_name_and_version (char **name,
                             char **version,
                             char *new_name,
                             char *new_version)
{
   if (new_name && !(*name)) {
      *name = new_name;
   } else {
      bson_free (new_name);
   }

   if (new_version && !(*version)) {
      *version = new_version;
   } else {
      bson_free (new_version);
   }

   return (*name) && (*version);
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
                                                   "ID", -1,
                                                   name,
                                                   "VERSION_ID", -1,
                                                   version);

   if (*name && *version) {
      RETURN (true);
   }

   _mongoc_linux_distro_scanner_read_key_val_file ("/etc/lsb-release",
                                                   "DISTRIB_ID", -1,
                                                   &new_name,
                                                   "DISTRIB_RELEASE", -1,
                                                   &new_version);

   if (_overwrite_name_and_version (name, version, new_name, new_version)) {
      RETURN (true);
   }

   /* Try to read from a generic release file */
   _mongoc_linux_distro_scanner_read_generic_release_file (
      generic_release_paths, &new_name, &new_version);

   if (_overwrite_name_and_version (name, version, new_name, new_version)) {
      RETURN (true);
   }

   /* TODO: If still no version, use uname */

   if (*name || *version) {
      RETURN (true);
   }

   bson_free (*name);
   bson_free (*version);

   RETURN (false);
}

#endif
