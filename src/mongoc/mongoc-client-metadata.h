#ifndef MONGOC_CLIENT_METADATA_H
#define MONGOC_CLIENT_METADATA_H

#if !defined (MONGOC_INSIDE) && !defined (MONGOC_COMPILATION)
# error "Only <mongoc.h> can be included directly."
#endif

#include "mongoc-client.h"

bool mongoc_client_set_application (mongoc_client_t *client,
                                    const char      *application_name);

bool mongoc_client_set_metadata (mongoc_client_t *client,
                                 const char      *driver_name,
                                 const char      *version,
                                 const char      *platform);

#define MONGOC_METADATA_APPLICATION_NAME_MAX_LENGTH 128

#endif
