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
#include "mock_server/mock-server.h"

/*
 * Call this before any test which uses mongoc_metadata_append, to
 * reset the global state and unfreeze the metadata struct. Call it
 * after a test so later tests don't have a weird metadata document
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

   _reset_metadata ();
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

   _reset_metadata ();
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

   mongoc_client_set_application (client, "my app");

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

   /* So later tests don't have "aaaaa..." as the md platform string */
   _reset_metadata ();
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

   /* Reset again so the next tests don't have a metadata doc which
    * is too big */
   _reset_metadata ();
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
}
