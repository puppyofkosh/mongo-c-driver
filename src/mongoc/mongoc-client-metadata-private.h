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

typedef enum
{
   MONGOC_MD_FLAG_ENABLE_SSL_SECURE_CHANNEL    = 1 << 0,
   MONGOC_MD_FLAG_ENABLE_CRYPTO_CNG            = 1 << 1,
   MONGOC_MD_FLAG_ENABLE_SSL_SECURE_TRANSPORT  = 1 << 2,
   MONGOC_MD_FLAG_ENABLE_CRYPTO_COMMON_CRYPTO  = 1 << 3,
   MONGOC_MD_FLAG_ENABLE_SSL_OPENSSL           = 1 << 4,
   MONGOC_MD_FLAG_ENABLE_CRYPTO_LIBCRYPTO      = 1 << 5,
   MONGOC_MD_FLAG_ENABLE_SSL                   = 1 << 6,
   MONGOC_MD_FLAG_ENABLE_CRYPTO                = 1 << 7,
   MONGOC_MD_FLAG_ENABLE_CRYPTO_SYSTEM_PROFILE = 1 << 8,
   MONGOC_MD_FLAG_ENABLE_SASL                  = 1 << 9,
   MONGOC_MD_FLAG_HAVE_SASL_CLIENT_DONE        = 1 << 10,
   MONGOC_MD_FLAG_HAVE_WEAK_SYMBOLS            = 1 << 11,
   MONGOC_MD_FLAG_NO_AUTOMATIC_GLOBALS         = 1 << 12,
} mongoc_metadata_config_flags_t;



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

#endif
