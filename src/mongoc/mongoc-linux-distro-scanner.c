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

static void
_lsb_copy_val_if_need (const char  *val,
                       char       **buffer)
{
   size_t sz;

   if (*buffer) {
      /* We've encountered this key more than once. This means the file is
       * weird, so just keep first copy */
      return;
   }

   sz = strlen (val) + 1;
   *buffer = bson_malloc (sz);
   bson_strncpy (*buffer, val, sz);
}

static bool
_lsb_process_line (char       **name,
                   char       **version,
                   const char  *line)
{
   size_t key_len;
   const char *equal_sign;
   const char *val;

   const size_t line_len = strlen (line);

   const char *delim = "=";
   const size_t delim_len = strlen (delim);

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
   if (strncmp (line, "DISTRIB_ID", key_len) == 0) {
      _lsb_copy_val_if_need (val, name);
      return true;
   } else if (strncmp (line, "DISTRIB_RELEASE", key_len) == 0) {
      _lsb_copy_val_if_need (val, version);
      return true;
   }

   return false;
}


bool
_mongoc_linux_distro_scanner_parse_lsb (const char  *path,
                                        char       **name,
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

      _lsb_process_line (name, version, line);

      if (*version && *name) {
         /* No point in reading any more */
         break;
      }
   }

   fclose (f);
   return *version && *name;
}

/*
 * Read whole first line or first 256 bytes, whichever is smaller
 * It's your job to free the return value of this function
 */
static bool
_read_first_line_up_to_limit (const char *path,
                              char       *buffer,
                              size_t      bufsize)
{
   bool ret = true;
   char *fgets_res;
   size_t len;
   FILE *f;

   BSON_ASSERT (bufsize > 0);

   f = fopen (path, "r");

   if (!f) {
      MONGOC_WARNING ("Couldn't open %s: error %d", path, errno);
      return false;
   }

   fgets_res = fgets (buffer, bufsize, f);

   if (fgets_res) {
      len = strlen (buffer);

      if (len > 0 && buffer[len - 1] == '\n') {
         /* get rid of newline */
         buffer[len - 1] = '\0';
      }
   } else {
      /* Didn't read anything. Just make sure the buffer is null terminated. */
      buffer[0] = '\0';
      if (ferror (f)) {
         MONGOC_WARNING ("Could open but not read from %s, error: %d",
                         path ? path : "<unkown>", errno);
         ret = false;
      }
   }

   fclose (f);
   return ret;
}

/*
 * Find the first string in a list which is a valid file.
 * Technically there's always a race condition when using this function
 * since immediately after it returns, the file could get removed, so
 * only use this for files which should never be removed (and check for
 * NULL when you fopen again)
 */
static const char *
_get_first_existing (const char **paths)
{
   const char **p = &paths[0];
   FILE *f;

   for (; *p != NULL; p++) {
      f = fopen (*p, "r");

      if (f) {
         fclose (f);
         return *p;
      }
   }

   return NULL;
}

static char *
_read_64_bytes_or_first_line (const char *path)
{
   enum N { bufsize = 64 };
   char *buffer;

   buffer = bson_malloc (bufsize);

   /* Read from something like /proc/sys/kernel/osrelease */
   BSON_ASSERT (path);
   _read_first_line_up_to_limit (path, buffer, bufsize);
   return buffer;
}

char *
_mongoc_linux_distro_scanner_read_osrelease (const char *path)
{
   return _read_64_bytes_or_first_line (path);
}

char *
_mongoc_linux_distro_scanner_read_release_file (const char *path)
{
   return _read_64_bytes_or_first_line (path);
}

static char *
_find_and_read_release_file ()
{
   const char *path;
   const char *paths [] = {
      "/etc/system-release",
      "/etc/redhat-release",
      "/etc/novell-release",
      "/etc/gentoo-release",
      "/etc/SuSE-release",
      "/etc/SUSE-release",
      "/etc/sles-release",
      "/etc/debian_release",
      "/etc/slackware-version",
      "/etc/centos-release",
      "/etc/os-release",
      NULL,
   };

   path = _get_first_existing (paths);

   if (!path) {
      return NULL;
   }

   return _mongoc_linux_distro_scanner_read_release_file (path);
}

bool
_mongoc_linux_distro_scanner_get_distro (char **name,
                                         char **version)
{
   const char *lsb_path = "/etc/lsb-release";
   const char *osrelease_path = "/proc/sys/kernel/osrelease";

   *name = NULL;
   *version = NULL;

   if (_mongoc_linux_distro_scanner_parse_lsb (lsb_path, name, version)) {
      return true;
   }

   /* Otherwise get the name from the "release" file and version from
    * /proc/sys/kernel/osrelease */
   *name = _find_and_read_release_file ();
   *version = _mongoc_linux_distro_scanner_read_osrelease (osrelease_path);

   return (*name != NULL) && (*version != NULL);
}
