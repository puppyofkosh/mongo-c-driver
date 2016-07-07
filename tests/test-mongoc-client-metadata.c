#include <mongoc.h>
#include "mongoc-client-private.h"
#include "mongoc-client-metadata.h"
#include "mongoc-client-metadata-private.h"

#include "TestSuite.h"
#include "test-libmongoc.h"
#include "test-conveniences.h"

/*
 * Call this before any test which uses mongoc_set_client_metadata, to
 * reset the global state and unfreeze the metadata struct.
 *
 * This is not safe to call while we have any clients or client pools running!
 */
static void
_reset_metadata ()
{
   _mongoc_client_metadata_cleanup ();
   _mongoc_client_metadata_init ();
}

static void
test_mongoc_client_global_metadata_success ()
{
   char big_string [METADATA_MAX_SIZE];

   memset (big_string, 'a', METADATA_MAX_SIZE - 1);
   big_string [METADATA_MAX_SIZE - 1] = '\0';

   _reset_metadata ();

   /* Make sure setting the metadata works */
   ASSERT (mongoc_set_client_metadata ("php driver", "version abc",
                                       "./configure -nottoomanyflags"));

   _reset_metadata ();
   /* Set each field to some really long string, which should
    * get truncated. We shouldn't fail or crash */
   ASSERT (mongoc_set_client_metadata (big_string, big_string, big_string));
}

static void
test_mongoc_client_global_metadata_after_cmd ()
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

   ASSERT (!mongoc_set_client_metadata ("a", "a", "a"));

   mongoc_client_pool_push (pool, client);

   mongoc_uri_destroy (uri);
   mongoc_client_pool_destroy (pool);
}

/*
 * Append to the platform field a huge string
 * Make sure that it gets truncated
 */
static void
test_mongoc_client_global_metadata_too_big ()
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

   ASSERT (mongoc_set_client_metadata (NULL, NULL, big_string));
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

void
test_client_metadata_install (TestSuite *suite)
{
   TestSuite_Add (suite, "/ClientMetadata/success",
                  test_mongoc_client_global_metadata_success);
   TestSuite_Add (suite, "/ClientMetadata/failure",
                  test_mongoc_client_global_metadata_after_cmd);
   TestSuite_Add (suite, "/ClientMetadata/too_big",
                  test_mongoc_client_global_metadata_too_big);
}
