#include <mongoc.h>
#include "mongoc-client-private.h"
#include "mongoc-metadata.h"
#include "mongoc-metadata-private.h"

#include "TestSuite.h"
#include "test-libmongoc.h"
#include "test-conveniences.h"

/*
 * Call this before any test which uses mongoc_metadata_append, to
 * reset the global state and unfreeze the metadata struct.
 *
 * This is not safe to call while we have any clients or client pools running!
 */
static void
_reset_metadata ()
{
   _mongoc_metadata_cleanup ();
   _mongoc_metadata_init ();
}

static void
test_mongoc_metadata_append_success ()
{
   char big_string [METADATA_MAX_SIZE];

   memset (big_string, 'a', METADATA_MAX_SIZE - 1);
   big_string [METADATA_MAX_SIZE - 1] = '\0';

   _reset_metadata ();

   /* Make sure setting the metadata works */
   ASSERT (mongoc_metadata_append ("php driver", "version abc",
                                   "./configure -nottoomanyflags"));

   _reset_metadata ();
   /* Set each field to some really long string, which should
    * get truncated. We shouldn't fail or crash */
   ASSERT (mongoc_metadata_append (big_string, big_string, big_string));
}

static void
test_mongoc_metadata_append_after_cmd ()
{
   mongoc_client_pool_t *pool;
   mongoc_client_t *client;
   mongoc_uri_t *uri;

   _reset_metadata ();

   uri = mongoc_uri_new ("mongodb://127.0.0.1?maxpoolsize=1&minpoolsize=1");
   pool = mongoc_client_pool_new (uri);

   /* Make sure that after we pop a client we can't set global metadata */
   pool = mongoc_client_pool_new (uri);

   client = mongoc_client_pool_pop (pool);

   ASSERT (!mongoc_metadata_append ("a", "a", "a"));

   mongoc_client_pool_push (pool, client);

   mongoc_uri_destroy (uri);
   mongoc_client_pool_destroy (pool);
}

/*
 * Append to the platform field a huge string
 * Make sure that it gets truncated
 */
static void
test_mongoc_metadata_too_big ()
{
   mongoc_client_t *client;
   bson_t *ismaster_doc;
   bson_iter_t iter;
   bson_error_t error;

   enum { BUFFER_SIZE = METADATA_MAX_SIZE };
   char big_string[BUFFER_SIZE];
   bool ret;
   uint32_t len;
   const uint8_t *dummy;

   _reset_metadata ();

   memset (big_string, 'a', BUFFER_SIZE - 1);
   big_string[BUFFER_SIZE - 1] = '\0';

   ASSERT (mongoc_metadata_append (NULL, NULL, big_string));
   client = test_framework_client_new ();

   /* Send a ping */
   ret = mongoc_client_command_simple (client, "admin",
                                       tmp_bson ("{'ping': 1}"), NULL,
                                       NULL, &error);
   ASSERT (ret);

   /* Make sure the client's isMaster with metadata isn't too big */
   ismaster_doc = &client->topology->scanner->ismaster_cmd_with_metadata,
   bson_iter_init_find (&iter,
                        ismaster_doc,
                        METADATA_FIELD);
   ASSERT (BSON_ITER_HOLDS_DOCUMENT (&iter));
   bson_iter_document (&iter, &len, &dummy);

   /* Should truncate the platform field so we fit exactly */
   ASSERT (len == METADATA_MAX_SIZE);

   mongoc_client_destroy (client);
}

static void
test_mongoc_metadata_linux_lsb ()
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
test_mongoc_metadata_linux_release_file ()
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
test_metadata_install (TestSuite *suite)
{
   TestSuite_Add (suite, "/ClientMetadata/success",
                  test_mongoc_metadata_append_success);
   TestSuite_Add (suite, "/ClientMetadata/failure",
                  test_mongoc_metadata_append_after_cmd);
   TestSuite_Add (suite, "/ClientMetadata/too_big",
                  test_mongoc_metadata_too_big);
   TestSuite_Add (suite, "/ClientMetadata/parse_lsb",
                  test_mongoc_metadata_linux_lsb);
   TestSuite_Add (suite, "/ClientMetadata/linux_release_file",
                  test_mongoc_metadata_linux_release_file);
}
