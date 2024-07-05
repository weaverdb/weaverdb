/*-------------------------------------------------------------------------
 *
 * rename.h
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
#ifndef RENAME_H
#define RENAME_H

PG_EXTERN void renameatt(char *relname,
		  char *oldattname,
		  char *newattname,
		  char *userName, int recurse);

PG_EXTERN void renamerel(const char *oldrelname,
		  const char *newrelname);

#endif	 /* RENAME_H */
