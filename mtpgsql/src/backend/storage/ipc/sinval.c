/*-------------------------------------------------------------------------
 *
 * sinval.c
 *	  POSTGRES shared cache invalidation communication code.
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  $Header: /cvs/weaver/mtpgsql/src/backend/storage/ipc/sinval.c,v 1.2 2006/08/15 18:24:28 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */
/* #define INVALIDDEBUG 1 */

#include <sys/types.h>
#include <signal.h>

#include "postgres.h"
#include "env/env.h"
#include "env/dbwriter.h"

#include "storage/backendid.h"
#include "storage/backendid.h"
#include "storage/multithread.h"
#include "storage/sinval.h"
#include "storage/sinvaladt.h"
#include "utils/tqual.h"

#include "utils/relcache.h"
#include "utils/catcache.h"
#include "utils/inval.h"

SPINLOCK	SInvalLock = (SPINLOCK) 0;

/****************************************************************************/
/*	CreateSharedInvalidationState()		 Create a buffer segment			*/
/*																			*/
/*	should be called only by the POSTMASTER									*/
/****************************************************************************/
void
CreateSharedInvalidationState(IPCKey key, int maxBackends)
{
	int			status;

	/* SInvalLock gets set in spin.c, during spinlock init */
	status = SISegmentInit(true, IPCKeyGetSIBufferMemoryBlock(key),
						   maxBackends);

	if (status == -1)
		elog(FATAL, "CreateSharedInvalidationState: failed segment init");
}

/****************************************************************************/
/*	AttachSharedInvalidationState(key)	 Attach to existing buffer segment	*/
/*																			*/
/*	should be called by each backend during startup							*/
/****************************************************************************/
void
AttachSharedInvalidationState(IPCKey key)
{
	int			status;

	if (key == PrivateIPCKey)
	{
		CreateSharedInvalidationState(key, 16);
		return;
	}
	/* SInvalLock gets set in spin.c, during spinlock init */
	status = SISegmentInit(false, IPCKeyGetSIBufferMemoryBlock(key), 0);

	if (status == -1)
		elog(FATAL, "AttachSharedInvalidationState: failed segment init");
}

/*
 * InitSharedInvalidationState
 *		Initialize new backend's state info in buffer segment.
 *		Must be called after AttachSharedInvalidationState().
 */
void
InitSharedInvalidationState(void)
{
	SpinAcquire(SInvalLock);
	if (!SIBackendInit(shmInvalBuffer))
	{
		SpinRelease(SInvalLock);
		elog(FATAL, "Backend cache invalidation initialization failed");
	}
	SpinRelease(SInvalLock);
}

/*
 * RegisterSharedInvalid
 *	Add a shared-cache-invalidation message to the global SI message queue.
 *
 * Note:
 *	Assumes hash index is valid.
 *	Assumes item pointer is valid.
 */
void
RegisterSharedInvalid(int cacheId,		/* XXX */
					  Index hashIndex,
					  ItemPointer pointer)
{
	SharedInvalidData newInvalid;
	bool		insertOK;

	/*
	 * This code has been hacked to accept two types of messages.  This
	 * might be treated more generally in the future.
	 *
	 * (1) cacheId= system cache id hashIndex= system cache hash index for a
	 * (possibly) cached tuple pointer= pointer of (possibly) cached tuple
	 *
	 * (2) cacheId= special non-syscache id hashIndex= object id contained in
	 * (possibly) cached relation descriptor pointer= null
	 */

	newInvalid.cacheId = cacheId;
	newInvalid.hashIndex = hashIndex;

        if ( cacheId == 0 && hashIndex == 0 ) {
            elog(ERROR,"invalid message");
        }

	if (ItemPointerIsValid(pointer))
		ItemPointerCopy(pointer, &newInvalid.pointerData);
	else
		ItemPointerSetInvalid(&newInvalid.pointerData);

	SpinAcquire(SInvalLock);
	insertOK = SIInsertDataEntry(shmInvalBuffer, &newInvalid);
	SpinRelease(SInvalLock);
	if (!insertOK)
		elog(NOTICE, "RegisterSharedInvalid: SI buffer overflow");
}

void
InvalidateAllCaches()
{
	SpinAcquire(SInvalLock);
	SIResetProcState(shmInvalBuffer);
	SpinRelease(SInvalLock);    
}

/*
 * InvalidateSharedInvalid
 *		Process shared-cache-invalidation messages waiting for this backend
 */
void
InvalidateSharedInvalid(void)
{
	SharedInvalidData data;
	int			getResult;
	BackendId  bid = GetMyBackendId();

	for (;;)
	{
		SpinAcquire(SInvalLock);
		getResult = SIGetDataEntry(shmInvalBuffer, bid, &data);
		SpinRelease(SInvalLock);
		if (getResult == 0)
			break;				/* nothing more to do */
		if (getResult < 0)
		{
			/* got a reset message */
			/*elog(NOTICE, "InvalidateSharedInvalid: cache state reset");*/
			if ( getResult == -1 ) RelationCacheInvalidate();
                        else if ( getResult == -2 ) ResetSystemCache();
		}
		else
		{
			/* got a normal data message */
			CacheIdInvalidate(data.cacheId,
						  data.hashIndex,
						  &data.pointerData);
		}
	}

	/* If we got any messages, try to release dead messages */
/*  let the dbwriter do this  */
	if (IsDBWriter())
	{
		SpinAcquire(SInvalLock);
		SIDelExpiredDataEntries(shmInvalBuffer);
		SpinRelease(SInvalLock);
	}

}


/****************************************************************************/
/* Functions that need to scan the PROC structures of all running backends. */
/* It's a bit strange to keep these in sinval.c, since they don't have any	*/
/* direct relationship to shared-cache invalidation.  But the procState		*/
/* array in the SI segment is the only place in the system where we have	*/
/* an array of per-backend data, so it is the most convenient place to keep */
/* pointers to the backends' PROC structures.  We used to implement these	*/
/* functions with a slow, ugly search through the ShmemIndex hash table --- */
/* now they are simple loops over the SI ProcState array.					*/
/****************************************************************************/


/*
 * DatabaseHasActiveBackends -- are there any backends running in the given DB
 *
 * This function is used to interlock DROP DATABASE against there being
 * any active backends in the target DB --- dropping the DB while active
 * backends remain would be a Bad Thing.  Note that we cannot detect here
 * the possibility of a newly-started backend that is trying to connect
 * to the doomed database, so additional interlocking is needed during
 * backend startup.
 */

bool
DatabaseHasActiveBackends(Oid databaseId)
{
	bool		result = false;
	SISeg	   *segP = shmInvalBuffer;
	ProcState  *stateP = segP->procState;
	int			index;

	SpinAcquire(SInvalLock);

	for (index = 0; index < segP->maxBackends; index++)
	{
		SHMEM_OFFSET pOffset = stateP[index].procStruct;

		if (pOffset != INVALID_OFFSET)
		{
			THREAD	   *proc = (THREAD *) MAKE_PTR(pOffset);

			if (proc->databaseId == databaseId)
			{
				result = true;
				break;
			}
		}
	}

	SpinRelease(SInvalLock);

	return result;
}

/*
 * TransactionIdIsInProgress -- is given transaction running by some backend
 */
bool
TransactionIdIsInProgress(TransactionId xid)
{
	bool		result = false;
	SISeg	   *segP = shmInvalBuffer;
	ProcState  *stateP = segP->procState;
	int			index;

	SpinAcquire(SInvalLock);

	for (index = 0; index < segP->maxBackends; index++)
	{
		SHMEM_OFFSET pOffset = stateP[index].procStruct;

		if (pOffset != INVALID_OFFSET)
		{
			THREAD	   *proc = (THREAD *) MAKE_PTR(pOffset);

			if (proc->xid == xid)
			{
				result = true;
				break;
			}
		}
	}

	SpinRelease(SInvalLock);

	return result;
}

/*
 * GetSnapshotData -- returns information about running transactions.
 */
Snapshot
GetSnapshotData(bool serializable)
{
	Snapshot		snapshot;
	SISeg	   *		segP = shmInvalBuffer;
	ProcState  *		stateP = segP->procState;
	int			index;
	int			count = 0;
	MemoryContext		old;
	TransactionId 		checkpoint;
        MemoryContext       query = MemoryContextGetEnv()->TopTransactionContext;
        
        THREAD*              my_thread = GetMyThread();
	
    old = MemoryContextSwitchTo(query);
	snapshot = (Snapshot) palloc(sizeof(SnapshotData));
	/*
	 * There can be no more than maxBackends active transactions, so this
	 * is enough space:
	 */
	snapshot->xip = (TransactionId *)
		palloc(segP->maxBackends * sizeof(TransactionId));
	snapshot->xmin = GetCurrentTransactionId();


	checkpoint = snapshot->xmin;

	/*
	 * Unfortunately, we have to call ReadNewTransactionId() after
	 * acquiring SInvalLock above. It's not good because
	 * ReadNewTransactionId() does SpinAcquire(OidGenLockId) but
	 * _necessary_.
	 */
	SpinAcquire(SInvalLock);  

	snapshot->xmax = ReadNewTransactionId();

	for (index = 0; index < segP->maxBackends; index++)
	{
		SHMEM_OFFSET pOffset = stateP[index].procStruct;

		if (pOffset != INVALID_OFFSET)
		{
			THREAD	   *proc = (THREAD *) MAKE_PTR(pOffset);
			TransactionId xid;
			
			pthread_mutex_lock(&proc->gate);

			/*
			 * We don't use spin-locking when changing proc->xid in
			 * GetNewTransactionId() and in AbortTransaction() !..
			 */
			if ( TransactionIdIsValid(proc->xmin) && proc->xmin < checkpoint ) 
				checkpoint = proc->xmin;

			xid = proc->xid;
/* only a normal will have a new transaction id that needs to be recorded
    all other threads do not take transaction id's or have clones 
    of a normal thread, whoops Poolsweeps ned to be recorded !!!  08/10/2006 MKS
*/
                        
			if (proc->state == TRANS_DEFAULT 
                            || proc->ttype == DBWRITER_THREAD 
                            || proc->ttype == DOL_THREAD 
                            || proc->ttype == DAEMON_THREAD 
                            || proc == my_thread)
			{
				pthread_mutex_unlock(&proc->gate);
				/*
				 * Seems that there is no sense to store xid >=
				 * snapshot->xmax (what we got from ReadNewTransactionId
				 * above) in snapshot->xip - we just assume that all xacts
				 * with such xid-s are running and may be ignored.
				 */
				continue;
			}


			if (xid < snapshot->xmin)
				snapshot->xmin = xid;

			snapshot->xip[count] = xid;
			switch ( proc->state ) {
				case TRANS_COMMIT:
					break;
				case TRANS_START:
					break;
				case TRANS_DEFAULT:
					elog(ERROR,"transaction in default state %d",proc->state);
					break;
				default:
					elog(ERROR,"unknown commit state");
					break;
			}
			
			pthread_mutex_unlock(&proc->gate);
			count++;
		}
	}


	if (serializable) {
		GetMyThread()->xmin = snapshot->xmin;
		if (snapshot->xmin < checkpoint ) checkpoint = snapshot->xmin;
	}
	/* Serializable snapshot must be computed before any other... */
	Assert(GetMyThread()->xmin != InvalidTransactionId);
	SetCheckpointId(checkpoint);
	SpinRelease(SInvalLock);

	snapshot->xcnt = count;
	snapshot->isUser = false;
	snapshot->nowait = false;

	MemoryContextSwitchTo(old);
    
    return snapshot;
}

