/*
 * Copyright 2013 MongoDB, Inc.
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


#include "mongoc.h"
#include "mongoc-apm-private.h"
#include "mongoc-counters-private.h"
#include "mongoc-client-pool-private.h"
#include "mongoc-client-pool.h"
#include "mongoc-client-private.h"
#include "mongoc-queue-private.h"
#include "mongoc-thread-private.h"
#include "mongoc-topology-private.h"
#include "mongoc-trace.h"

#ifdef MONGOC_ENABLE_SSL
#include "mongoc-ssl-private.h"
#endif

struct _mongoc_client_pool_t
{
   mongoc_mutex_t          mutex;
   mongoc_cond_t           cond;
   mongoc_queue_t          queue;
   mongoc_topology_t      *topology;
   mongoc_uri_t           *uri;
   uint32_t                min_pool_size;
   uint32_t                max_pool_size;
   uint32_t                size;
   bool                    topology_scanner_started;
#ifdef MONGOC_ENABLE_SSL
   bool                    ssl_opts_set;
   mongoc_ssl_opt_t        ssl_opts;
#endif
   mongoc_apm_callbacks_t  apm_callbacks;
   void                   *apm_context;

   int32_t                 error_api_version;

   bool                    metadata_set;
};


#ifdef MONGOC_ENABLE_SSL
void
mongoc_client_pool_set_ssl_opts (mongoc_client_pool_t   *pool,
                                 const mongoc_ssl_opt_t *opts)
{
   BSON_ASSERT (pool);

   mongoc_mutex_lock (&pool->mutex);

   _mongoc_ssl_opts_cleanup (&pool->ssl_opts);

   memset (&pool->ssl_opts, 0, sizeof pool->ssl_opts);
   pool->ssl_opts_set = false;

   if (opts) {
      _mongoc_ssl_opts_copy_to (opts, &pool->ssl_opts);
      pool->ssl_opts_set = true;
   }

   mongoc_topology_scanner_set_ssl_opts (pool->topology->scanner, &pool->ssl_opts);

   mongoc_mutex_unlock (&pool->mutex);
}
#endif


mongoc_client_pool_t *
mongoc_client_pool_new (const mongoc_uri_t *uri)
{
   mongoc_topology_t *topology;
   mongoc_client_pool_t *pool;
   const bson_t *b;
   bson_iter_t iter;

   ENTRY;

   BSON_ASSERT (uri);

#ifndef MONGOC_ENABLE_SSL
   if (mongoc_uri_get_ssl (uri)) {
      MONGOC_ERROR ("Can't create SSL client pool,"
                    " SSL not enabled in this build.");
      return NULL;
   }
#endif

   pool = (mongoc_client_pool_t *)bson_malloc0(sizeof *pool);
   mongoc_mutex_init(&pool->mutex);
   _mongoc_queue_init(&pool->queue);
   pool->uri = mongoc_uri_copy(uri);
   pool->min_pool_size = 0;
   pool->max_pool_size = 100;
   pool->size = 0;
   pool->topology_scanner_started = false;

   topology = mongoc_topology_new(uri, false);
   pool->topology = topology;

   mongoc_client_metadata_init (&pool->topology->scanner->ismaster_metadata);

   pool->error_api_version = MONGOC_ERROR_API_VERSION_LEGACY;

   b = mongoc_uri_get_options(pool->uri);

   if (bson_iter_init_find_case(&iter, b, "minpoolsize")) {
      if (BSON_ITER_HOLDS_INT32(&iter)) {
         pool->min_pool_size = BSON_MAX(0, bson_iter_int32(&iter));
      }
   }

   if (bson_iter_init_find_case(&iter, b, "maxpoolsize")) {
      if (BSON_ITER_HOLDS_INT32(&iter)) {
         pool->max_pool_size = BSON_MAX(1, bson_iter_int32(&iter));
      }
   }

   mongoc_counter_client_pools_active_inc();

   RETURN(pool);
}


void
mongoc_client_pool_destroy (mongoc_client_pool_t *pool)
{
   mongoc_client_t *client;

   ENTRY;

   BSON_ASSERT (pool);

   while ((client = (mongoc_client_t *)_mongoc_queue_pop_head(&pool->queue))) {
      mongoc_client_destroy(client);
   }

   mongoc_topology_destroy (pool->topology);

   mongoc_uri_destroy(pool->uri);
   mongoc_mutex_destroy(&pool->mutex);
   mongoc_cond_destroy(&pool->cond);

#ifdef MONGOC_ENABLE_SSL
   _mongoc_ssl_opts_cleanup (&pool->ssl_opts);
#endif

   bson_free(pool);

   mongoc_counter_client_pools_active_dec();
   mongoc_counter_client_pools_disposed_inc();

   EXIT;
}

static void
start_scanner_if_needed (mongoc_client_pool_t *pool) {
   bool r;

   if (!pool->topology_scanner_started) {
      r = mongoc_topology_start_background_scanner (pool->topology);

      if (r) {
         pool->topology_scanner_started = true;
      } else {
         MONGOC_ERROR ("Background scanner did not start!");
      }
   }
}

mongoc_client_t *
mongoc_client_pool_pop (mongoc_client_pool_t *pool)
{
   mongoc_client_t *client;

   ENTRY;

   BSON_ASSERT (pool);

   mongoc_mutex_lock(&pool->mutex);

again:
   if (!(client = (mongoc_client_t *)_mongoc_queue_pop_head(&pool->queue))) {
      if (pool->size < pool->max_pool_size) {
         client = _mongoc_client_new_from_uri(pool->uri, pool->topology);
         client->error_api_version = pool->error_api_version;
         _mongoc_client_set_apm_callbacks_private (client,
                                                   &pool->apm_callbacks,
                                                   pool->apm_context);
#ifdef MONGOC_ENABLE_SSL
         if (pool->ssl_opts_set) {
            mongoc_client_set_ssl_opts (client, &pool->ssl_opts);
         }
#endif
         pool->size++;
      } else {
         mongoc_cond_wait(&pool->cond, &pool->mutex);
         GOTO(again);
      }
   }

   start_scanner_if_needed (pool);
   mongoc_mutex_unlock(&pool->mutex);

   RETURN(client);
}


mongoc_client_t *
mongoc_client_pool_try_pop (mongoc_client_pool_t *pool)
{
   mongoc_client_t *client;

   ENTRY;

   BSON_ASSERT (pool);

   mongoc_mutex_lock(&pool->mutex);

   if (!(client = (mongoc_client_t *)_mongoc_queue_pop_head(&pool->queue))) {
      if (pool->size < pool->max_pool_size) {
         client = _mongoc_client_new_from_uri(pool->uri, pool->topology);
#ifdef MONGOC_ENABLE_SSL
         if (pool->ssl_opts_set) {
            mongoc_client_set_ssl_opts (client, &pool->ssl_opts);
         }
#endif
         pool->size++;
      }
   }

   if (client) {
      start_scanner_if_needed (pool);
   }
   mongoc_mutex_unlock(&pool->mutex);

   RETURN(client);
}


void
mongoc_client_pool_push (mongoc_client_pool_t *pool,
                         mongoc_client_t      *client)
{
   ENTRY;

   BSON_ASSERT (pool);
   BSON_ASSERT (client);

   mongoc_mutex_lock(&pool->mutex);
   if (pool->min_pool_size && pool->size > pool->min_pool_size) {
      mongoc_client_t *old_client;
      old_client = (mongoc_client_t *)_mongoc_queue_pop_head (&pool->queue);
      if (old_client) {
          mongoc_client_destroy (old_client);
          pool->size--;
      }
   }

   _mongoc_queue_push_head (&pool->queue, client);

   mongoc_cond_signal(&pool->cond);
   mongoc_mutex_unlock(&pool->mutex);

   EXIT;
}

size_t
mongoc_client_pool_get_size (mongoc_client_pool_t *pool)
{
   size_t size = 0;

   ENTRY;

   mongoc_mutex_lock (&pool->mutex);
   size = pool->size;
   mongoc_mutex_unlock (&pool->mutex);

   RETURN (size);
}

void
mongoc_client_pool_max_size(mongoc_client_pool_t *pool,
                            uint32_t              max_pool_size)
{
   ENTRY;

   mongoc_mutex_lock (&pool->mutex);
   pool->max_pool_size = max_pool_size;
   mongoc_mutex_unlock (&pool->mutex);

   EXIT;
}

void
mongoc_client_pool_min_size(mongoc_client_pool_t *pool,
                            uint32_t              min_pool_size)
{
   ENTRY;

   mongoc_mutex_lock (&pool->mutex);
   pool->min_pool_size = min_pool_size;
   mongoc_mutex_unlock (&pool->mutex);

   EXIT;
}

void
mongoc_client_pool_get_metadata (mongoc_client_pool_t *pool,
                                 bson_t *buf) {
   ENTRY;

   BSON_ASSERT (buf);

   mongoc_mutex_lock (&pool->mutex);
   bson_copy_to (&pool->topology->scanner->ismaster_metadata, buf);
   mongoc_mutex_unlock (&pool->mutex);

   EXIT;
}

bool
mongoc_client_pool_set_apm_callbacks (mongoc_client_pool_t   *pool,
                                      mongoc_apm_callbacks_t *callbacks,
                                      void                   *context)
{

   if (pool->apm_callbacks.started ||
       pool->apm_callbacks.succeeded ||
       pool->apm_callbacks.failed ||
       pool->apm_context) {
      MONGOC_ERROR ("Can only set callbacks once");
      return false;
   }

   if (callbacks) {
      memcpy (&pool->apm_callbacks, callbacks, sizeof pool->apm_callbacks);
   }

   pool->apm_context = context;

   return true;
}

bool
mongoc_client_pool_set_error_api (mongoc_client_pool_t *pool,
                                  int32_t               version)
{
   if (version != MONGOC_ERROR_API_VERSION_LEGACY &&
       version != MONGOC_ERROR_API_VERSION_2) {
      MONGOC_ERROR ("Unsupported Error API Version: %" PRId32, version);
      return false;
   }

   pool->error_api_version = version;

   return true;
}

bool
mongoc_client_pool_set_application (mongoc_client_pool_t   *pool,
                                    const char             *application_name)
{
   bool ret;
   bson_t* metadata;
   /* Locking mutex even though this function can only get called once because
      we don't want to write to the metadata bson_t if someone else is reading
      from it at the same time */
   mongoc_mutex_lock (&pool->mutex);

   if (mongoc_topology_is_scanner_active (pool->topology)) {
      /* Once the scanner is active we cannot tell it to send
         different metadata */
      ret = false;
      goto done;
   }

   metadata = &pool->topology->scanner->ismaster_metadata;
   ret = mongoc_client_metadata_set_application (metadata, application_name);
done:
   mongoc_mutex_unlock (&pool->mutex);

   return ret;
}

bool
mongoc_client_pool_set_metadata (mongoc_client_pool_t   *pool,
                                 const char             *driver_name,
                                 const char             *version,
                                 const char             *platform)
{
   bool ret = false;
   bson_t* metadata;

   mongoc_mutex_lock (&pool->mutex);

   if (pool->metadata_set) {
      goto done;
   }

   if (mongoc_topology_is_scanner_active (pool->topology)) {
      /* Once the scanner is active we cannot tell it to send
         different metadata */
      goto done;
   }

   metadata = &pool->topology->scanner->ismaster_metadata;
   ret = mongoc_client_metadata_set_data (metadata,
                                          driver_name,
                                          version,
                                          platform);

   if (ret) {
      pool->metadata_set = true;
   }
done:
   mongoc_mutex_unlock (&pool->mutex);

   return ret;
}
