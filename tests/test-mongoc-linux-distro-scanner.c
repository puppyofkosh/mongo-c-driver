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
#include "mongoc-linux-distro-scanner-private.h"

#include "TestSuite.h"
#include "test-libmongoc.h"
#include "test-conveniences.h"

#ifdef MONGOC_OS_IS_LINUX
static void
test_read_key_value_file (void)
{
   char *name = NULL;
   char *version = NULL;

   _mongoc_linux_distro_scanner_read_key_val_file (
      OS_RELEASE_FILE_DIR "/example-lsb-file.txt",
      "DISTRIB_ID", &name,
      "DISTRIB_RELEASE", &version);

   ASSERT (ret);

   ASSERT (name);
   ASSERT_CMPSTR (name, "Ubuntu");

   ASSERT (version);
   ASSERT_CMPSTR (version, "12.04");

   bson_free (name);
   bson_free (version);

   _mongoc_linux_distro_scanner_read_key_val_file (
      OS_RELEASE_FILE_DIR "/example-etc-os-release.txt",
      "ID", &name,
      "VERSION_ID", &version);
   ASSERT (ret);

   ASSERT (name);
   ASSERT_CMPSTR (name, "fedora");

   ASSERT (version);
   ASSERT_CMPSTR (version, "17");

   bson_free (name);
   bson_free (version);

   /* Now try some weird inputs */
   _mongoc_linux_distro_scanner_read_key_val_file (
      OS_RELEASE_FILE_DIR "/example-etc-os-release.txt",
      "ID=", &name,
      "VERSION_ID=", &version);

   ASSERT (name == NULL);
   ASSERT (version == NULL);

   _mongoc_linux_distro_scanner_read_key_val_file (
      OS_RELEASE_FILE_DIR "/example-etc-os-release.txt",
      "", &name,
      "", &version);

   ASSERT (name == NULL);
   ASSERT (version == NULL);


   /* Test case where we get one but not the other */
   _mongoc_linux_distro_scanner_read_key_val_file (
      OS_RELEASE_FILE_DIR "/example-etc-os-release.txt",
      "ID", &name,
      "VERSION_", &version);

   ASSERT_CMPSTR (name, "fedora");
   ASSERT (version == NULL);
   bson_free (name);

   /* Case where we say the key is the whole line */
   _mongoc_linux_distro_scanner_read_key_val_file (
      OS_RELEASE_FILE_DIR "/example-etc-os-release.txt",
      "ID", &name,
      "VERSION_ID=17", &version);
   ASSERT_CMPSTR (name, "fedora");
   ASSERT (version == NULL);
   bson_free (name);

   /* Case where the key is duplicated, make sure we keep first version */
   _mongoc_linux_distro_scanner_read_key_val_file (
      OS_RELEASE_FILE_DIR "/example-key-val-file.txt",
      "key", &name,
      "normalkey", &version);
   ASSERT_CMPSTR (name, "first value");
   ASSERT_CMPSTR (version, "normalval");
   bson_free (name);
   bson_free (version);

   /* Case where the key is duplicated, make sure we keep first version */
   _mongoc_linux_distro_scanner_read_key_val_file (
      OS_RELEASE_FILE_DIR "/example-key-val-file.txt",
      "a-key-without-a-value", &name,
      "normalkey", &version);
   ASSERT_CMPSTR (name, "");
   ASSERT_CMPSTR (version, "normalval");
   bson_free (name);
   bson_free (version);

   /* Try to get value from a line like:
    * just-a-key
    * (No equals, no value)
    */
   _mongoc_linux_distro_scanner_read_key_val_file (
      OS_RELEASE_FILE_DIR "/example-key-val-file.txt",
      "just-a-key", &name,
      "normalkey", &version);
   ASSERT (name == NULL);
   ASSERT_CMPSTR (version, "normalval");
   bson_free (name);
   bson_free (version);

   /* Try to get a key which is on line 101 of the file
    * (we stop reading at line 100) */
   _mongoc_linux_distro_scanner_read_key_val_file (
      OS_RELEASE_FILE_DIR "/example-key-val-file.txt",
      "lastkey", &name,
      "normalkey", &version);
   ASSERT (name == NULL);
   ASSERT_CMPSTR (version, "normalval");
   bson_free (version);
}

/* We only expect this function to actually read anything on linux platforms.
 * We run this test on all platforms to be sure the get_distro function doesn't
 * crash on a platform with some of the files it looks for missing */
static void
test_distro_scanner_reads (void)
{
   char *name;
   char *version;

   _mongoc_linux_distro_scanner_get_distro (&name, &version);

   /*
    * TODO: Remove this. Just for fun on the evergreen build
    */
   fprintf (stderr, "name: %s version: %s\n", name, version);
   /* Remove it! */

   ASSERT (name);
   ASSERT (strlen (name) > 0);
   ASSERT (version);
   ASSERT (strlen (version) > 0);
}
#endif

void
test_linux_distro_scanner_install (TestSuite *suite)
{
#ifdef MONGOC_OS_IS_LINUX
   TestSuite_Add (suite, "/LinuxDistroScanner/test_read_key_value_file",
                  test_read_key_value_file);
   TestSuite_Add (suite, "/LinuxDistroScanner/test_distro_scanner_reads",
                  test_distro_scanner_reads);
#endif
}
