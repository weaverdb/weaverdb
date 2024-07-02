/*-------------------------------------------------------------------------
 *
 * heap.h
 *	  prototypes for functions in lib/catalog/heap.c
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 *
 *-------------------------------------------------------------------------
 */
#ifndef HEAP_H
#define HEAP_H

#include "utils/rel.h"

typedef struct RawColumnDefault
{
	AttrNumber	attnum;			/* attribute to attach default to */
	Node	   *raw_default;	/* default value (untransformed parse
								 * tree) */
} RawColumnDefault;

PG_EXTERN Oid	RelnameFindRelid(const char *relname);
PG_EXTERN Relation heap_create(char *relname, TupleDesc att,
			bool isnoname, bool istemp, bool storage_create);
PG_EXTERN bool heap_storage_create(Relation rel);

PG_EXTERN Oid heap_create_with_catalog(char *relname, TupleDesc tupdesc,
						 char relkind, bool istemp);

PG_EXTERN void heap_drop_with_catalog(const char *relname);
PG_EXTERN void heap_truncate(char *relname);
PG_EXTERN void heap_drop(Relation rel);

PG_EXTERN void AddRelationRawConstraints(Relation rel,
						  List *rawColDefaults,
						  List *rawConstraints);
PG_EXTERN void AddRelationStorageDirectives(Relation rel,
						  List *rawConstraints);
PG_EXTERN void RemoveSchemaInheritance(char* name);
PG_EXTERN bool IsExternalStore(Relation rel);
PG_EXTERN void InitNoNameRelList(void);
PG_EXTERN void DropNoNameRels(void);

#endif	 /* HEAP_H */
