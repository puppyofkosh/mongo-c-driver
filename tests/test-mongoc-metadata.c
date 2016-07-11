#include <mongoc.h>
#include "mongoc-client-private.h"
#include "mongoc-metadata.h"
#include "mongoc-metadata-private.h"

#include "TestSuite.h"
#include "test-libmongoc.h"
#include "test-conveniences.h"
#include "mock_server/mock-server.h"

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

/* Test the case where we can't prevent the metadata doc being too big
 * and so we just don't send it */
static void
test_mongoc_metadata_cannot_send ()
{
   mock_server_t *server;
   mongoc_uri_t *uri;
   mongoc_client_t *client;
   mongoc_client_pool_t *pool;
   request_t *request;
   const char *const server_reply = "{'ok': 1, 'ismaster': true}";
   const bson_t *request_doc;
   char big_string[METADATA_MAX_SIZE];

   _reset_metadata ();

   /* Mess with our global metadata struct so the metadata doc will be
    * way too big */
   memset (big_string, 'a', METADATA_MAX_SIZE - 1);
   big_string[METADATA_MAX_SIZE - 1] = '\0';
   _mongoc_metadata_override_os_name (big_string);

   server = mock_server_new ();
   mock_server_run (server);
   uri = mongoc_uri_copy (mock_server_get_uri (server));
   pool = mongoc_client_pool_new (uri);

   /* Pop a client to trigger the topology scanner */
   client = mongoc_client_pool_pop (pool);
   request = mock_server_receives_ismaster (server);

   /* Make sure the isMaster request DOESN'T have a metadata field: */
   ASSERT (request);
   request_doc = request_get_doc (request, 0);
   ASSERT (request_doc);
   ASSERT (bson_has_field (request_doc, "isMaster"));
   ASSERT (!bson_has_field (request_doc, METADATA_FIELD));

   mock_server_replies_simple (request, server_reply);
   request_destroy (request);

   /* cleanup */
   mongoc_client_pool_push (pool, client);

   mongoc_client_pool_destroy (pool);
   mongoc_uri_destroy (uri);
   mock_server_destroy (server);
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
   TestSuite_Add (suite, "/ClientMetadata/cannot_send",
                  test_mongoc_metadata_cannot_send);
   TestSuite_Add (suite, "/ClientMetadata/parse_lsb",
                  test_mongoc_metadata_linux_lsb);
   TestSuite_Add (suite, "/ClientMetadata/linux_release_file",
                  test_mongoc_metadata_linux_release_file);
}
