#ifndef MONGOC_CLIENT_METADATA_H
#define MONGOC_CLIENT_METADATA_H

#if !defined (MONGOC_INSIDE) && !defined (MONGOC_COMPILATION)
# error "Only <mongoc.h> can be included directly."
#endif

#include "mongoc-client.h"

bool mongoc_set_client_metadata (const char *driver_name,
                                 const char *driver_version,
                                 const char *platform);

#endif
