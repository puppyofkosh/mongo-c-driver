#ifndef MONGOC_CLIENT_METADATA_PRIVATE_H
#define MONGOC_CLIENT_METADATA_PRIVATE_H

#if !defined (MONGOC_INSIDE) && !defined (MONGOC_COMPILATION)
# error "Only <mongoc.h> can be included directly."
#endif
#include <bson.h>
#include "mongoc-topology-private.h"

#define METADATA_FIELD "meta"
#define METADATA_APPLICATION_FIELD "application"
#define METADATA_APPLICATION_NAME_FIELD "name"

#define METADATA_PLATFORM_FIELD "platform"

#define METADATA_MAX_SIZE 512

#define METADATA_OS_NAME_MAX 32
#define METADATA_OS_VERSION_MAX 32
#define METADATA_OS_ARCHITECTURE_MAX 32
#define METADATA_DRIVER_NAME_MAX 64
#define METADATA_DRIVER_VERSION_MAX 32
/* platform has no fixed max size. It can just occupy the remaining
   available space in the document. */


typedef struct _mongoc_client_metadata_t
{
   const char *os_name;
   const char *os_version;
   const char *os_architecture;

   const char *driver_name;
   const char *driver_version;
   const char *platform;

   bool frozen;
} mongoc_client_metadata_t;

void _mongoc_client_metadata_init ();
void _mongoc_client_metadata_cleanup ();
void _build_metadata_doc_with_application (bson_t* doc,
                                           const char* application);
void _mongoc_client_metadata_freeze ();

bool _mongoc_client_metadata_set_metadata (const char *driver_name,
                                           const char *driver_version,
                                           const char *platform);

bool
_mongoc_client_metadata_set_application (mongoc_topology_t *topology,
                                         const char *application);
#endif
