/*-------------------------------------------------------------------------
 *
 * inval.h
 *	  POSTGRES cache invalidation dispatcher definitions.
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
#ifndef INVAL_H
#define INVAL_H

#include "access/htup.h"

PG_EXTERN void DiscardInvalid(void);

PG_EXTERN void RegisterInvalid(bool send);

PG_EXTERN void ImmediateLocalInvalidation(bool send);

PG_EXTERN void RelationInvalidateHeapTuple(Relation relation, HeapTuple tuple);

PG_EXTERN void RelationMark4RollbackHeapTuple(Relation relation, HeapTuple tuple);

PG_EXTERN void ImmediateInvalidateSharedHeapTuple(Relation relation, HeapTuple tuple);

PG_EXTERN void ImmediateSharedRelationCacheInvalidate(Relation relation);

PG_EXTERN void CacheIdInvalidate(Index cacheId, Index hashIndex, ItemPointer pointer);
#endif	 /* INVAL_H */
