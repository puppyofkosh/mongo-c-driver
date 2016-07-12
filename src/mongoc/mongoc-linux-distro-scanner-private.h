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


#ifndef MONGOC_LINUX_SCANNER_PRIVATE_H
#define MONGOC_LINUX_SCANNER_PRIVATE_H

BSON_BEGIN_DECLS

bool
_mongoc_linux_distro_scanner_get_distro (char **name,
                                         char **version);

bool
_mongoc_linux_distro_scanner_parse_lsb (const char *path,
                                        char      **name,
                                        char      **version);

char *
_mongoc_linux_distro_scanner_get_osname_from_release_file (const char *path);

char *
_mongoc_linux_distro_scanner_get_version_from_osrelease (const char *path);

BSON_END_DECLS

#endif
