/*-------------------------------------------------------------------------
 *
 * lmgr.h
 *	  POSTGRES lock manager definitions.
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * $Id: lmgr.h,v 1.1.1.1 2006/08/12 00:22:24 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */
#ifndef LMGR_H
#define LMGR_H

#include "storage/lock.h"
#include "utils/rel.h"

/* These are the valid values of type LOCKMODE: */

/* NoLock is not a lock mode, but a flag value meaning "don't get a lock" */
#define NoLock				0
#define AccessShareLock			1		/* SELECT */
#define RowShareLock			2		/* SELECT FOR UPDATE */
#define RowExclusiveLock		3		/* INSERT, UPDATE, DELETE */
#define ShareUpdateExclusiveLock        4		/* VACUUM (non-FULL) */
#define ShareLock			5		/* CREATE INDEX */
#define ShareRowExclusiveLock           6		/* like EXCLUSIVE MODE, but allows ROW SHARE */
#define ExclusiveLock			7		/* blocks ROW SHARE/SELECT...FOR * UPDATE */
#define AccessExclusiveLock		8		/* ALTER TABLE, DROP TABLE, VACUUM FULL, and 
                                                            unqualified LOCK TABLE */


extern LOCKMETHOD LockTableId;


PG_EXTERN LOCKMETHOD InitLockTable(int partitions, int maxBackends);
PG_EXTERN void RelationInitLockInfo(Relation relation);

/* Lock a relation */
PG_EXTERN void LockRelation(Relation relation, LOCKMODE lockmode);
PG_EXTERN void UnlockRelation(Relation relation, LOCKMODE lockmode);
PG_EXTERN bool NoWaitLockRelation(Relation relation, LOCKMODE lockmode);

PG_EXTERN void LockRelationForSession(LockRelId *relid, LOCKMODE lockmode);
PG_EXTERN void UnlockRelationForSession(LockRelId *relid, LOCKMODE lockmode);

/* Lock a page (mainly used for indices) */
PG_EXTERN void LockPage(Relation relation, BlockNumber blkno, LOCKMODE lockmode);
PG_EXTERN void UnlockPage(Relation relation, BlockNumber blkno, LOCKMODE lockmode);

/* Lock an XID (used to wait for a transaction to finish) */
PG_EXTERN void XactLockTableInsert(TransactionId xid);
PG_EXTERN void XactLockTableWait(TransactionId xid);

#endif	 /* LMGR_H */
