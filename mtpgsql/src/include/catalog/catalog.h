/*-------------------------------------------------------------------------
 *
 * catalog.h
 *	  prototypes for functions in lib/catalog/catalog.c
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 *
 *-------------------------------------------------------------------------
 */
#ifndef CATALOG_H
#define CATALOG_H

#include "access/tupdesc.h"

PG_EXTERN char *relpath(const char *relname);
PG_EXTERN char *relpath_blind(const char *dbname, const char *relname,
			  Oid dbid, Oid relid);
PG_EXTERN bool IsSystemRelationName(const char *relname);
PG_EXTERN bool IsSharedSystemRelationName(const char *relname);
PG_EXTERN Oid	newoid(void);
PG_EXTERN void fillatt(TupleDesc att);

#endif	 /* CATALOG_H */
