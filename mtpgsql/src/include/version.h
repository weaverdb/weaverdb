/*
 * Copyright (c) 2024 Myron Scott <myron@weaverdb.org> All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 */

/*------------------------------------------------------------------------- 
 *
 * version.h.in
 *	  this file contains the interface to version.c.
 *	  Also some parameters.
 *
 *
 *
 *-------------------------------------------------------------------------
 */
#ifndef VERSION_H
#define VERSION_H
#ifdef __cplusplus
extern "C" {
#endif

void ValidatePgVersion(const char *path, char **reason_p);
void SetPgVersion(const char *path, char **reason_p);
#ifdef __cplusplus
}
#endif

#define PG_RELEASE		"7"
#define PG_VERSION		"0"
#define PG_SUBVERSION		"5"
#define PG_DEVVERSION		"20070222-01"
#define PG_VERFILE		"PG_VERSION"

#define PG_VERSION_STR  "PostgreSQL " PG_RELEASE "." PG_VERSION "." PG_SUBVERSION " multithreaded variant "

#endif
