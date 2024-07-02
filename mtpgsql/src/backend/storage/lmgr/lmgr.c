/*-------------------------------------------------------------------------
 *
 * lmgr.c
 *	  POSTGRES lock manager code
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include "env/dbwriter.h"
#include "access/transam.h"
#include "access/xact.h"
#include "catalog/catalog.h"
#include "miscadmin.h"
#include "storage/lmgr.h"
#include "utils/inval.h"


static LOCKMASK LockConflicts[] = {
	0,

	/* AccessShareLock */
	(1 << AccessExclusiveLock),

	/* RowShareLock */
	(1 << ExclusiveLock) | (1 << AccessExclusiveLock),

	/* RowExclusiveLock */
	(1 << ShareLock) | (1 << ShareRowExclusiveLock) |
	(1 << ExclusiveLock) | (1 << AccessExclusiveLock),

	/* ShareUpdateExclusiveLock */
	(1 << ShareUpdateExclusiveLock) |
	(1 << ShareLock) | (1 << ShareRowExclusiveLock) |
	(1 << ExclusiveLock) | (1 << AccessExclusiveLock),

	/* ShareLock */
	(1 << RowExclusiveLock) | (1 << ShareUpdateExclusiveLock) |
	(1 << ShareRowExclusiveLock) |
	(1 << ExclusiveLock) | (1 << AccessExclusiveLock),

	/* ShareRowExclusiveLock */
	(1 << RowExclusiveLock) | (1 << ShareUpdateExclusiveLock) |
	(1 << ShareLock) | (1 << ShareRowExclusiveLock) |
	(1 << ExclusiveLock) | (1 << AccessExclusiveLock),

	/* ExclusiveLock */
	(1 << RowShareLock) |
	(1 << RowExclusiveLock) | (1 << ShareUpdateExclusiveLock) |
	(1 << ShareLock) | (1 << ShareRowExclusiveLock) |
	(1 << ExclusiveLock) | (1 << AccessExclusiveLock),

	/* AccessExclusiveLock */
	(1 << AccessShareLock) | (1 << RowShareLock) |
	(1 << RowExclusiveLock) | (1 << ShareUpdateExclusiveLock) |
	(1 << ShareLock) | (1 << ShareRowExclusiveLock) |
	(1 << ExclusiveLock) | (1 << AccessExclusiveLock)

};


static int	LockPrios[] = {
	0,
	1,
	2,
	3,
	4,
	5,
	6,
	7
};

LOCKMETHOD	LongTermTableId = (LOCKMETHOD) 0;

LOCKMETHOD*      PartitionedTables;

int numberOfPartitions = 1;
/*
 * Create the lock table described by LockConflicts and LockPrios.
 */
LOCKMETHOD
InitLockTable(int parts, int maxBackends)
{
        int count;
        numberOfPartitions = parts;
        
        PartitionedTables = os_malloc(sizeof(LOCKMETHOD) * numberOfPartitions);
        
        for (count=0;count<numberOfPartitions;count++) {
            char     name[255];
            snprintf(name,255,"LockTable - %d",count);
            PartitionedTables[count] = LockMethodTableInit(name,
                                         LockConflicts, LockPrios,
                                         MAX_LOCKMODES - 1, maxBackends);
        }
#ifdef USER_LOCKS
	/*
	 * Allocate another tableId for long-term locks
	 */
	LongTermTableId = LockMethodTableRename(PartitionedTables[0]);
	if (!(LongTermTableId))
		elog(ERROR, "InitLockTable: couldn't rename long-term lock table");
#endif

        return PartitionedTables[0];
}

/*
 * RelationInitLockInfo
 *		Initializes the lock information in a relation descriptor.
 *
 *		relcache.c must call this during creation of any reldesc.
 */
void
RelationInitLockInfo(Relation relation)
{
	char	   *relname;

	Assert(RelationIsValid(relation));
	Assert(OidIsValid(RelationGetRelid(relation)));

	relname = (char *) RelationGetPhysicalRelationName(relation);

	relation->rd_lockInfo.lockRelId.relId = RelationGetRelid(relation);

	if (IsSharedSystemRelationName(relname))
		relation->rd_lockInfo.lockRelId.dbId = InvalidOid;
	else {
		if (!IsSharedSystemRelationName(relname)) {
			relation->rd_lockInfo.lockRelId.dbId = GetDatabaseId();
		} else {
			elog(FATAL,"bad database");
		}
	}
}

/*
 *		NoWaitLockRelation
 */
bool
NoWaitLockRelation(Relation relation, LOCKMODE lockmode)
{
	LOCKTAG		tag;
	TransactionId	xid;
        int part = relation->rd_id % numberOfPartitions;

	if (LockingDisabled())
		return true;

	MemSet(&tag, 0, sizeof(tag));
	tag.relId = relation->rd_lockInfo.lockRelId.relId;
	tag.dbId = relation->rd_lockInfo.lockRelId.dbId;
	tag.objId.blkno = InvalidBlockNumber;

	xid = GetCurrentTransactionId();
        
        return LockAcquire(PartitionedTables[part], &tag, xid, lockmode, true);
}


/*
 *		LockRelation
 */
void
LockRelation(Relation relation, LOCKMODE lockmode)
{
	LOCKTAG		tag;
	TransactionId	xid;
        int part = relation->rd_id % numberOfPartitions;

        if (LockingDisabled())
		return;

	MemSet(&tag, 0, sizeof(tag));
	tag.relId = relation->rd_lockInfo.lockRelId.relId;
	tag.dbId = relation->rd_lockInfo.lockRelId.dbId;
	tag.objId.blkno = InvalidBlockNumber;

	xid = GetCurrentTransactionId();
        if ( !LockAcquire(PartitionedTables[part], &tag, xid, lockmode, false) ) {
            elog(ERROR, "LockRelation: LockAcquire failed");
        }        
	/*
	 * Check to see if the relcache entry has been invalidated while we
	 * were waiting to lock it.  If so, rebuild it, or elog() trying.
	 * Increment the refcount to ensure that RelationFlushRelation will
	 * rebuild it and not just delete it.
	 */
	RelationIncrementReferenceCount(relation);
	DiscardInvalid();
	RelationDecrementReferenceCount(relation);
}

/*
 *		UnlockRelation
 */
void
UnlockRelation(Relation relation, LOCKMODE lockmode)
{
	LOCKTAG		tag;
	TransactionId	xid;
        int part = relation->rd_id % numberOfPartitions;

        if (LockingDisabled())
		return;

	MemSet(&tag, 0, sizeof(tag));
	tag.relId = relation->rd_lockInfo.lockRelId.relId;
	tag.dbId = relation->rd_lockInfo.lockRelId.dbId;
	tag.objId.blkno = InvalidBlockNumber;

	xid = GetCurrentTransactionId();
        LockRelease(PartitionedTables[part], &tag, xid, lockmode);
}


#ifdef NOTUSED

/*
 *		LockRelationForSession
 *
 * This routine grabs a session-level lock on the target relation.  The
 * session lock persists across transaction boundaries.  It will be removed
 * when UnlockRelationForSession() is called, or if an elog(ERROR) occurs,
 * or if the backend exits.
 *
 * Note that one should also grab a transaction-level lock on the rel
 * in any transaction that actually uses the rel, to ensure that the
 * relcache entry is up to date.
 */
void
LockRelationForSession(LockRelId *relid, LOCKMODE lockmode)
{
	LOCKTAG		tag;
	TransactionId	xid = InvalidTransactionId;

	if (LockingDisabled())
		return;

	MemSet(&tag, 0, sizeof(tag));
	tag.relId = relid->relId;
	tag.dbId = relid->dbId;
	tag.objId.blkno = InvalidBlockNumber;

	if (!LockAcquire(LockTableId, &tag, xid, lockmode, false))
		elog(ERROR, "LockRelationForSession: LockAcquire failed");
}

/*
 *		UnlockRelationForSession
 */
void
UnlockRelationForSession(LockRelId *relid, LOCKMODE lockmode)
{
	LOCKTAG		tag;
	TransactionId  xid = InvalidTransactionId;

	if (LockingDisabled())
		return;

	MemSet(&tag, 0, sizeof(tag));
	tag.relId = relid->relId;
	tag.dbId = relid->dbId;
	tag.objId.blkno = InvalidBlockNumber;

	LockRelease(LockTableId, &tag, xid, lockmode);
}


#endif 

/*
 *		LockPage
 */
void
LockPage(Relation relation, BlockNumber blkno, LOCKMODE lockmode)
{
	LOCKTAG		tag;
	TransactionId	xid;
        int part = relation->rd_id % numberOfPartitions;

	if (LockingDisabled())
		return;

	MemSet(&tag, 0, sizeof(tag));
	tag.relId = relation->rd_lockInfo.lockRelId.relId;
	tag.dbId = relation->rd_lockInfo.lockRelId.dbId;
	tag.objId.blkno = blkno;

	xid = GetCurrentTransactionId();
        
        if (!LockAcquire(PartitionedTables[part], &tag, xid, lockmode, false))
            elog(ERROR, "LockPage: LockAcquire failed");        
}

/*
 *		UnlockPage
 */
void
UnlockPage(Relation relation, BlockNumber blkno, LOCKMODE lockmode)
{
	LOCKTAG		tag;
	TransactionId	xid;
        int part = relation->rd_id % numberOfPartitions;

        if (LockingDisabled())
		return;

	MemSet(&tag, 0, sizeof(tag));
	tag.relId = relation->rd_lockInfo.lockRelId.relId;
	tag.dbId = relation->rd_lockInfo.lockRelId.dbId;
	tag.objId.blkno = blkno;

	xid = GetCurrentTransactionId();
        LockRelease(PartitionedTables[part], &tag, xid, lockmode);
}

void
XactLockTableInsert(TransactionId xid)
{
	LOCKTAG		tag;

	if (LockingDisabled())
		return;

	MemSet(&tag, 0, sizeof(tag));
	tag.relId = XactLockTableId;
	tag.dbId = InvalidOid;		/* xids are globally unique */
	tag.objId.xid = xid;
	if (!LockAcquire(PartitionedTables[0], &tag, xid, ExclusiveLock, false))
		elog(ERROR, "XactLockTableInsert: LockAcquire failed");
}

void
XactLockTableWait(TransactionId xid)
{
	LOCKTAG		tag;
	TransactionId	sid;

	if (LockingDisabled())
		return;

	MemSet(&tag, 0, sizeof(tag));
	tag.relId = XactLockTableId;
	tag.dbId = InvalidOid;
	tag.objId.xid = xid;

	sid = GetCurrentTransactionId();
	if (!LockAcquire(PartitionedTables[0], &tag, sid, ShareLock, false)) {
		elog(ERROR, "XactLockTableWait: LockAcquire failed");
	} else {
		LockRelease(PartitionedTables[0], &tag, sid, ShareLock);
	}
}
