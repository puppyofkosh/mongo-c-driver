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

#include <mongoc.h>
#include "mongoc-client-private.h"
#include "mongoc-metadata.h"
#include "mongoc-metadata-private.h"

#include "TestSuite.h"
#include "test-libmongoc.h"
#include "test-conveniences.h"


static void
test_parse_lsb ()
{
   char *name = NULL;
   char *version = NULL;
   bool ret;

   ret = _mongoc_metadata_parse_lsb (
      OS_RELEASE_FILE_DIR "/example-lsb-file.txt",
      &name, &version);

   ASSERT (ret);

   ASSERT (name);
   ASSERT (strcmp (name, "Ubuntu") == 0);

   ASSERT (version);
   ASSERT (strcmp (version, "12.04") == 0);

   bson_free (name);
   bson_free (version);
}

static void
test_release_file ()
{
   char *name;
   char *version;
   const char *release_path = OS_RELEASE_FILE_DIR "/example-release-file.txt";
   const char *osversion_path = OS_RELEASE_FILE_DIR "/example-os-version.txt";

   name = _mongoc_metadata_get_osname_from_release_file (release_path);

   /* We expect to get the first line of the file (it's NOT parsing the file
    * because we're not sure what format it is */
   ASSERT (strcmp (name, "NAME=\"Ubuntu\"") == 0);

   /* Normally would read from "/proc/sys/kernel/osrelease" */
   version = _mongoc_metadata_get_version_from_osrelease (osversion_path);
   ASSERT (strcmp (version, "2.2.14-5.0") == 0);

   bson_free (name);
   bson_free (version);
}

void
test_linux_distro_scanner_install (TestSuite *suite)
{
   TestSuite_Add (suite, "/LinuxDistroScanner/parse_lsb",
                  test_parse_lsb);
   TestSuite_Add (suite, "/LinuxDistroScanner/read_release_file",
                  test_release_file);
}
