/*-------------------------------------------------------------------------
 *
 * sinval.h
 *	  POSTGRES shared cache invalidation communication definitions.
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
#ifndef SINVAL_H
#define SINVAL_H

#include "storage/itemptr.h"
#include "storage/spin.h"

extern SPINLOCK SInvalLock;

PG_EXTERN void CreateSharedInvalidationState(IPCKey key, int maxBackends);
PG_EXTERN void AttachSharedInvalidationState(IPCKey key);
PG_EXTERN void InitSharedInvalidationState(void);
PG_EXTERN void RegisterSharedInvalid(int cacheId, Index hashIndex,
					  ItemPointer pointer);
PG_EXTERN void InvalidateSharedInvalid(void);
PG_EXTERN void InvalidateAllCaches(void);

PG_EXTERN bool DatabaseHasActiveBackends(Oid databaseId);
PG_EXTERN bool TransactionIdIsInProgress(TransactionId xid);


#endif	 /* SINVAL_H */
