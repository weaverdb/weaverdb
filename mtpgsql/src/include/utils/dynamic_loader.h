/*-------------------------------------------------------------------------
 *
 * dynamic_loader.h
 *
 *
 *
 * Portions Copyright (c) 2000-2024, Myron Scott  <myron@weaverdb.org>
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 *
 *-------------------------------------------------------------------------
 */
#ifndef DYNAMIC_LOADER_H
#define DYNAMIC_LOADER_H

#include <sys/types.h>

/* we need this include because port files use them */

#ifdef MIN
#undef MIN
#undef MAX
#endif	 /* MIN */

/*
 * List of dynamically loaded files.
 */

typedef struct df_files
{
	char		filename[MAXPGPATH];	/* Full pathname of file */
	dev_t		device;			/* Device file is on */
	ino_t		inode;			/* Inode number of file */
	void	   *handle;			/* a handle for pg_dl* functions */
	struct df_files *next;
} DynamicFileList;

PG_EXTERN void *pg_dlopen(char *filename);
PG_EXTERN func_ptr pg_dlsym(void *handle, char *funcname);
PG_EXTERN void pg_dlclose(void *handle);
PG_EXTERN char *pg_dlerror(void);

#endif	 /* DYNAMIC_LOADER_H */
