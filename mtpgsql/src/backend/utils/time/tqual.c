/*-------------------------------------------------------------------------
 *
 * tqual.c
 *	  POSTGRES "time" qualification code.
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  $Header: /cvs/weaver/mtpgsql/src/backend/utils/time/tqual.c,v 1.2 2006/08/15 18:24:28 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */

/* #define TQUALDEBUG	1 */

#include "postgres.h"
#include "env/env.h"
#include "utils/tqual.h"
#include "storage/sinval.h"
#include "utils/memutils.h"
#include "utils/mcxt.h"


static bool
TransactionIdActiveDuringSnapshot(Snapshot shot,TransactionId id );


static SectionId snapshot_id = SECTIONID("SNAP");

static SnapshotHolder* InitializeSnapshotHolder(void);
#ifdef TLS
TLS SnapshotHolder* snapshot_holder = NULL;
#else
#define  snapshot_holder GetEnv()->snapshot_holder
#endif

/*
 * HeapTupleSatisfiesItself
 *		True iff heap tuple is valid for "itself."
 *		"{it}self" means valid as of everything that's happened
 *		in the current transaction, _including_ the current command.
 *
 * Note:
 *		Assumes heap tuple is valid.
 */
/*
 * The satisfaction of "itself" requires the following:
 *
 * ((Xmin == my-transaction &&				the row was updated by the current transaction, and
 *		(Xmax is null						it was not deleted
 *		 [|| Xmax != my-transaction)])			[or it was deleted by another transaction]
 * ||
 *
 * (Xmin is committed &&					the row was modified by a committed transaction, and
 *		(Xmax is null ||					the row has not been deleted, or
 *			(Xmax != my-transaction &&			the row was deleted by another transaction
 *			 Xmax is not committed)))			that has not been committed
 */
bool
HeapTupleSatisfiesItself(HeapTupleHeader tuple)
{

	if (!(tuple->t_infomask & HEAP_XMIN_COMMITTED))
	{
		if ((tuple->t_infomask & HEAP_XMIN_INVALID))
			return false;
 		else if (TransactionIdIsCurrentTransactionId(tuple->t_xmin))
		{
			if (tuple->t_infomask & HEAP_XMAX_INVALID)	/* xid invalid */
				return true;
			if (tuple->t_infomask & HEAP_MARKED_FOR_UPDATE)
				return true;
			return false;
		}
		else if (!TransactionIdDidCommit(tuple->t_xmin))
		{
			if (TransactionIdDidAbort(tuple->t_xmin) ) {
				tuple->t_infomask |= HEAP_XMIN_INVALID; /* aborted */
			} else if (TransactionIdDidCrash(tuple->t_xmin)) {
				tuple->t_infomask |= HEAP_XMIN_INVALID; /* aborted */
				/*elog(DEBUG,"1. tuple xmin %lld crashed",tuple->t_xmin);*/
			} 

			return false;
		}
		if ( TransactionIdDidHardCommit(tuple->t_xmin))
			tuple->t_infomask |= HEAP_XMIN_COMMITTED;
	}
	/* the tuple was inserted validly */

	if (tuple->t_infomask & HEAP_XMAX_INVALID)	/* xid invalid or aborted */
		return true;
        
        if (!TransactionIdIsValid(tuple->t_xmax)) {
            tuple->t_infomask |= HEAP_XMAX_INVALID; 
            elog(DEBUG,"testing invalid xmax");
            return true;
        }
        
	if (tuple->t_infomask & HEAP_XMAX_COMMITTED)
	{
		if (tuple->t_infomask & HEAP_MARKED_FOR_UPDATE)
			return true;
		return false;			/* updated by other */
	}

	if (TransactionIdIsCurrentTransactionId(tuple->t_xmax))
	{
		if (tuple->t_infomask & HEAP_MARKED_FOR_UPDATE)
			return true;
		return false;
	}

	if (!TransactionIdDidCommit(tuple->t_xmax))
	{
		if (TransactionIdDidAbort(tuple->t_xmax) )
			tuple->t_infomask |= HEAP_XMAX_INVALID;		/* aborted */
		else if ( TransactionIdDidCrash(tuple->t_xmax)) {
			tuple->t_infomask |= HEAP_XMAX_INVALID;		/* aborted */
			/*elog(DEBUG,"2. tuple xmin %lld crashed",tuple->t_xmin);*/
		}
		return true;
	}

	/* by here, deleting transaction has committed */
	if ( TransactionIdDidHardCommit(tuple->t_xmax) )
		tuple->t_infomask |= HEAP_XMAX_COMMITTED;

	if (tuple->t_infomask & HEAP_MARKED_FOR_UPDATE)
		return true;

	return false;
}

/*
 * HeapTupleSatisfiesNow
 *		True iff heap tuple is valid "now."
 *		"now" means valid including everything that's happened
 *		 in the current transaction _up to, but not including,_
 *		 the current command.
 *
 * Note:
 *		Assumes heap tuple is valid.
 */
/*
 * The satisfaction of "now" requires the following:
 *
 * ((Xmin == my-transaction &&				changed by the current transaction
 *	 Cmin != my-command &&					but not by this command, and
 *		(Xmax is null ||						the row has not been deleted, or
 *			(Xmax == my-transaction &&			it was deleted by the current transaction
 *			 Cmax != my-command)))				but not by this command,
 * ||										or
 *
 *	(Xmin is committed &&					the row was modified by a committed transaction, and
 *		(Xmax is null ||					the row has not been deleted, or
 *			(Xmax == my-transaction &&			the row is being deleted by this command, or
 *			 Cmax == my-command) ||
 *			(Xmax is not committed &&			the row was deleted by another transaction
 *			 Xmax != my-transaction))))			that has not been committed
 *
 *		mao says 17 march 1993:  the tests in this routine are correct;
 *		if you think they're not, you're wrong, and you should think
 *		about it again.  i know, it happened to me.  we don't need to
 *		check commit time against the start time of this transaction
 *		because 2ph locking protects us from doing the wrong thing.
 *		if you mess around here, you'll break serializability.  the only
 *		problem with this code is that it does the wrong thing for system
 *		catalog updates, because the catalogs aren't subject to 2ph, so
 *		the serializability guarantees we provide don't extend to xacts
 *		that do catalog accesses.  this is unfortunate, but not critical.
 */
bool
HeapTupleSatisfiesNow(void* e ,HeapTupleHeader tuple)
{
	if (IsBootstrapProcessingMode())
		return true;

	if (!(tuple->t_infomask & HEAP_XMIN_COMMITTED))
	{
		if ((tuple->t_infomask & HEAP_XMIN_INVALID))
			return false;
		else if (TransactionIdIsCurrentTransactionId(tuple->t_xmin))
		{
			if (CommandIdGEScanCommandId(tuple->progress.cmd.t_cmin))
				return false;	/* inserted after scan started */

			if (tuple->t_infomask & HEAP_XMAX_INVALID)	/* xid invalid */
				return true;

			Assert(TransactionIdIsCurrentTransactionId(tuple->t_xmax));

			if (tuple->t_infomask & HEAP_MARKED_FOR_UPDATE)
				return true;

			if (CommandIdGEScanCommandId(tuple->progress.cmd.t_cmax))
				return true;	/* deleted after scan started */
			else
				return false;	/* deleted before scan started */
		}
		else if (!TransactionIdDidCommit(tuple->t_xmin))
		{
			if (TransactionIdDidAbort(tuple->t_xmin) )
				tuple->t_infomask |= HEAP_XMIN_INVALID; /* aborted */
			else if (TransactionIdDidCrash(tuple->t_xmin)) {
				tuple->t_infomask |= HEAP_XMIN_INVALID; /* aborted */
				/*elog(DEBUG,"3. tuple xmin %lld crashed",tuple->t_xmin);*/
			}
			return false;
		}
		if ( TransactionIdDidHardCommit(tuple->t_xmin) ) 
			tuple->t_infomask |= HEAP_XMIN_COMMITTED;
	}

	/* by here, the inserting transaction has committed */

	if (tuple->t_infomask & HEAP_XMAX_INVALID)	/* xid invalid or aborted */
		return true;
        
        if (!TransactionIdIsValid(tuple->t_xmax)) {
            tuple->t_infomask |= HEAP_XMAX_INVALID; 
            elog(DEBUG,"testing invalid xmax");
            return true;
        }
        
	if (tuple->t_infomask & HEAP_XMAX_COMMITTED)
	{
		if (tuple->t_infomask & HEAP_MARKED_FOR_UPDATE)
			return true;
		return false;
	}

	if (TransactionIdIsCurrentTransactionId(tuple->t_xmax))
	{
		if (tuple->t_infomask & HEAP_MARKED_FOR_UPDATE)
			return true;
		if (CommandIdGEScanCommandId(tuple->progress.cmd.t_cmax))
			return true;		/* deleted after scan started */
		else
			return false;		/* deleted before scan started */
	}
	
	if (!TransactionIdDidCommit(tuple->t_xmax))
	{
		if (TransactionIdDidAbort(tuple->t_xmax)) 
			tuple->t_infomask |= HEAP_XMAX_INVALID;		/* aborted */
		else if ( TransactionIdDidCrash(tuple->t_xmax)) {
			tuple->t_infomask |= HEAP_XMAX_INVALID;		/* aborted */
			/*elog(DEBUG,"4. tuple xmax %lld crashed",tuple->t_xmax);*/
		}
		return true;
	}

	/* xmax transaction committed */
	if ( TransactionIdDidHardCommit(tuple->t_xmax) )
		tuple->t_infomask |= HEAP_XMAX_COMMITTED;

	if (tuple->t_infomask & HEAP_MARKED_FOR_UPDATE)
		return true;

	return false;
}

int
HeapTupleSatisfiesUpdate(void* env,HeapTuple tuple, Snapshot snapshot)
{
	HeapTupleHeader th = tuple->t_data;

	if (IsBootstrapProcessingMode())
		return HeapTupleMayBeUpdated;

	if (!(th->t_infomask & HEAP_XMIN_COMMITTED))
	{
		if ((th->t_infomask & HEAP_XMIN_INVALID)){ /* xid invalid or aborted */
                    return HeapTupleInvisible;
		}
		else if (TransactionIdIsCurrentTransactionId(th->t_xmin))
		{
			if (CommandIdGEScanCommandId(th->progress.cmd.t_cmin))
				return HeapTupleInvisible;		/* inserted after scan
												 * started */

			if (th->t_infomask & HEAP_XMAX_INVALID)		/* xid invalid */
				return HeapTupleMayBeUpdated;

			Assert(TransactionIdIsCurrentTransactionId(th->t_xmax));

			if (th->t_infomask & HEAP_MARKED_FOR_UPDATE)
				return HeapTupleMayBeUpdated;

			if (CommandIdGEScanCommandId(th->progress.cmd.t_cmax))
				return HeapTupleSelfUpdated;	/* updated after scan
												 * started */
			else
				return HeapTupleInvisible;		/* updated before scan
												 * started */
		}
		else if (!TransactionIdDidCommit(th->t_xmin))
		{
			if (TransactionIdDidAbort(th->t_xmin)) 
				th->t_infomask |= HEAP_XMIN_INVALID;	/* aborted */
			else if (TransactionIdDidCrash(th->t_xmin)) {
				th->t_infomask |= HEAP_XMIN_INVALID;	/* aborted */
			}
			return HeapTupleInvisible;
		}
		if ( TransactionIdDidHardCommit(th->t_xmin) )
			th->t_infomask |= HEAP_XMIN_COMMITTED;
	}

	/* by here, the inserting transaction has committed */

	if (th->t_infomask & HEAP_XMAX_INVALID)		/* xid invalid or aborted */
		return HeapTupleMayBeUpdated;
        
        if (!TransactionIdIsValid(th->t_xmax)) {
            th->t_infomask |= HEAP_XMAX_INVALID; 
            elog(DEBUG,"testing invalid xmax");
            return HeapTupleMayBeUpdated;
        }
        
	if (th->t_infomask & HEAP_XMAX_COMMITTED)
	{
		if (th->t_infomask & HEAP_MARKED_FOR_UPDATE)
			return HeapTupleMayBeUpdated;
		return HeapTupleUpdated;/* updated by other */
	}

	if (TransactionIdIsCurrentTransactionId(th->t_xmax))
	{
		if (th->t_infomask & HEAP_MARKED_FOR_UPDATE)
			return HeapTupleMayBeUpdated;
		if (CommandIdGEScanCommandId(th->progress.cmd.t_cmax))
			return HeapTupleSelfUpdated;		/* updated after scan started */
		else
			return HeapTupleInvisible;	/* updated before scan started */
	}
	else if (!TransactionIdDidCommit(th->t_xmax))
	{
		if (TransactionIdDidAbort(th->t_xmax))
		{
			th->t_infomask |= HEAP_XMAX_INVALID;		/* aborted */
			return HeapTupleMayBeUpdated;
		} else if (TransactionIdDidCrash(th->t_xmax)) {
			th->t_infomask |= HEAP_XMAX_INVALID;		/* aborted */
			return HeapTupleMayBeUpdated;
		}
		/* running xact */
		return HeapTupleBeingUpdated;	/* in updation by other */
	}

	/* xmax transaction committed */
	if ( TransactionIdDidHardCommit(th->t_xmax) )
		th->t_infomask |= HEAP_XMAX_COMMITTED;

	if (th->t_infomask & HEAP_MARKED_FOR_UPDATE)
		return HeapTupleMayBeUpdated;

	return HeapTupleUpdated;	/* updated by other */
}

bool
HeapTupleSatisfiesDirty(void* e,HeapTupleHeader tuple, Snapshot snapshot)
{	
	SnapshotHolder* env = (SnapshotHolder*)e;
	snapshot->xmin = snapshot->xmax = InvalidTransactionId;
	ItemPointerSetInvalid(&(env->SnapshotDirty->tid));

	if (IsBootstrapProcessingMode())
		return true;

	if (!(tuple->t_infomask & HEAP_XMIN_COMMITTED))
	{
		if ((tuple->t_infomask & HEAP_XMIN_INVALID))
			return false;

		if (TransactionIdIsCurrentTransactionId(tuple->t_xmin))
		{
			if (tuple->t_infomask & HEAP_XMAX_INVALID)	/* xid invalid */
				return true;

			Assert(TransactionIdIsCurrentTransactionId(tuple->t_xmax));

			if (tuple->t_infomask & HEAP_MARKED_FOR_UPDATE)
				return true;

			return false;
		}
		else if (!TransactionIdDidCommit(tuple->t_xmin))
		{
			if (TransactionIdDidAbort(tuple->t_xmin))
			{
				tuple->t_infomask |= HEAP_XMIN_INVALID; /* aborted */
				return false;
			}
			if (TransactionIdDidCrash(tuple->t_xmin)) {
				/*elog(DEBUG,"7. tuple xmin %lld crashed",tuple->t_xmin);*/
				tuple->t_infomask |= HEAP_XMIN_INVALID; /* aborted */
				return false;
			} else {
				env->SnapshotDirty->xmin = tuple->t_xmin;
				return true;		/* in insertion by other */
			}

		}
		
		if ( TransactionIdDidHardCommit(tuple->t_xmin) ) 
		{
			tuple->t_infomask |= HEAP_XMIN_COMMITTED;
		}
	}

	/* by here, the inserting transaction has committed */

	if (tuple->t_infomask & HEAP_XMAX_INVALID)	/* xid invalid or aborted */
		return true;

        if (!TransactionIdIsValid(tuple->t_xmax)) {
            tuple->t_infomask |= HEAP_XMAX_INVALID; /* aborted */
            elog(DEBUG,"testing invalid xmax");
            return true;
        }
        
        if (tuple->t_infomask & HEAP_XMAX_COMMITTED)
	{
		if (tuple->t_infomask & HEAP_MARKED_FOR_UPDATE)
			return true;
		env->SnapshotDirty->tid = tuple->t_ctid;
		return false;			/* updated by other */
	}

	if (TransactionIdIsCurrentTransactionId(tuple->t_xmax))
		return false;
        
	if (!TransactionIdDidCommit(tuple->t_xmax))
	{
		if (TransactionIdDidAbort(tuple->t_xmax))
		{
			tuple->t_infomask |= HEAP_XMAX_INVALID;		/* aborted */
			return true;
		}
		if (TransactionIdDidCrash(tuple->t_xmax)) {
			/*elog(DEBUG,"8. tuple xmax %lld crashed",tuple->t_xmax);*/
			tuple->t_infomask |= HEAP_XMAX_INVALID; /* aborted */
			return true;
		} else {
			/* running xact */
			env->SnapshotDirty->xmax = tuple->t_xmax;
			return true;			/* in updation by other */
		}

	}

	/* xmax transaction committed */
	if ( TransactionIdDidHardCommit(tuple->t_xmax) )
		tuple->t_infomask |= HEAP_XMAX_COMMITTED;

	if (tuple->t_infomask & HEAP_MARKED_FOR_UPDATE)
		return true;

	env->SnapshotDirty->tid = tuple->t_ctid;
	return false;				/* updated by other */
}

bool
HeapTupleSatisfiesSnapshot(void* e,HeapTupleHeader tuple, Snapshot snapshot)
{
	SnapshotHolder* env = (SnapshotHolder*)e;

	if (IsBootstrapProcessingMode())
		return true;

	if ( env && env->ReferentialIntegritySnapshotOverride )
		return HeapTupleSatisfiesNow(env->QuerySnapshot,tuple);

	if (!(tuple->t_infomask & HEAP_XMIN_COMMITTED))
	{
		if ((tuple->t_infomask & HEAP_XMIN_INVALID))
			return false;

		if (TransactionIdIsCurrentTransactionId(tuple->t_xmin))
		{
			if (CommandIdGEScanCommandId(tuple->progress.cmd.t_cmin))
				return false;	/* inserted after scan started */

			if (tuple->t_infomask & HEAP_XMAX_INVALID)	/* xid invalid */
				return true;

			Assert(TransactionIdIsCurrentTransactionId(tuple->t_xmax));

			if (tuple->t_infomask & HEAP_MARKED_FOR_UPDATE)
				return true;

			if (CommandIdGEScanCommandId(tuple->progress.cmd.t_cmax))
				return true;	/* deleted after scan started */
			else
				return false;	/* deleted before scan started */
		}
		else if (!TransactionIdDidCommit(tuple->t_xmin))
		{
			if (TransactionIdDidAbort(tuple->t_xmin))
				tuple->t_infomask |= HEAP_XMIN_INVALID; /* aborted */
			else if (TransactionIdDidCrash(tuple->t_xmin) ) {
				/*elog(DEBUG,"9. tuple xmin %lld crashed",tuple->t_xmin);*/
				tuple->t_infomask |= HEAP_XMIN_INVALID; /* aborted */
			}
			return false;
		}
		if ( TransactionIdDidHardCommit(tuple->t_xmin) )
			tuple->t_infomask |= HEAP_XMIN_COMMITTED;
	}

	/*
	 * By here, the inserting transaction has committed - have to check
	 * when...
	 */

	if ( tuple->t_infomask & HEAP_MOVED_IN ) {
		if ( TransactionIdActiveDuringSnapshot(snapshot,tuple->progress.t_vtran) ) {
			return false;
		}
	} else if ( TransactionIdActiveDuringSnapshot(snapshot,tuple->t_xmin) ) {
		return false;
	}
	
	if (tuple->t_infomask & HEAP_XMAX_INVALID)	/* xid invalid or aborted */
		return true;
        
        if (!TransactionIdIsValid(tuple->t_xmax)) {
            tuple->t_infomask |= HEAP_XMAX_INVALID; 
            elog(DEBUG,"testing invalid xmax");
            return true;
        }
        
	if (tuple->t_infomask & HEAP_MARKED_FOR_UPDATE)
		return true;

	if (!(tuple->t_infomask & HEAP_XMAX_COMMITTED))
	{
		if (TransactionIdIsCurrentTransactionId(tuple->t_xmax))
		{
			if (CommandIdGEScanCommandId(tuple->progress.cmd.t_cmax))
				return true;	/* deleted after scan started */
			else
				return false;	/* deleted before scan started */
		}

		if (!TransactionIdDidCommit(tuple->t_xmax))
		{
			if (TransactionIdDidAbort(tuple->t_xmax))
				tuple->t_infomask |= HEAP_XMAX_INVALID; /* aborted */
			else if (TransactionIdDidCrash(tuple->t_xmax)) {
				/*elog(DEBUG,"10. tuple xmax %lld crashed",tuple->t_xmax);*/
				tuple->t_infomask |= HEAP_XMAX_INVALID;
			}
			return true;
		}

		/* xmax transaction committed */
		if ( TransactionIdDidHardCommit(tuple->t_xmax) )
			tuple->t_infomask |= HEAP_XMAX_COMMITTED;
	}

	if ( tuple->t_infomask & HEAP_MOVED_OUT ) {
		return false;
	}

	return TransactionIdActiveDuringSnapshot(snapshot,tuple->t_xmax);
}

/*
 * HeapTupleSatisfiesVacuum - determine tuple status for VACUUM and related
 *		operations
 *
 * OldestXmin is a cutoff XID (obtained from GetOldestXmin()).	Tuples
 * deleted by XIDs >= OldestXmin are deemed "recently dead"; they might
 * still be visible to some open transaction, so we can't remove them,
 * even if we see that the deleting transaction has committed.
 *
 * As with the other HeapTupleSatisfies routines, we may update the tuple's
 * "hint" status bits if we see that the inserting or deleting transaction
 * has now committed or aborted.  The caller is responsible for noticing any
 * change in t_infomask and scheduling a disk write if so.
 */
HTSV_Result
HeapTupleSatisfiesVacuum(HeapTupleHeader tuple, TransactionId OldestXmin)
{
	 bool xmin_committed = false;
	 bool xmax_committed = false;
	 
	if (!(tuple->t_infomask & HEAP_XMIN_COMMITTED))
	{
		if ((tuple->t_infomask & HEAP_XMIN_INVALID))
			return HEAPTUPLE_STILLBORN;
		else if (TransactionIdDidCommit(tuple->t_xmin))  {
			xmin_committed = true;
			if ( TransactionIdDidHardCommit(tuple->t_xmin))
				tuple->t_infomask |= HEAP_XMIN_COMMITTED;
		}
		else if (TransactionIdDidAbort(tuple->t_xmin))
		{
			tuple->t_infomask |= HEAP_XMIN_INVALID;
			return HEAPTUPLE_STILLBORN;
		}
		else if (TransactionIdDidCrash(tuple->t_xmin)) 
		{
			/*elog(DEBUG,"11. tuple xmin %lld crashed",tuple->t_xmin);*/
			tuple->t_infomask |= HEAP_XMIN_INVALID;
			return HEAPTUPLE_STILLBORN;
		}
		else
		{
			return HEAPTUPLE_INSERT_IN_PROGRESS;
		}
		/* Should only get here if we set XMIN_COMMITTED */
		Assert(xmin_committed);
	}

	/*
	 * Okay, the inserter committed, so it was good at some point.	Now
	 * what about the deleting transaction?
	 */
	if (tuple->t_infomask & HEAP_XMAX_INVALID) {
		return HEAPTUPLE_LIVE;
        }
         
        if (!TransactionIdIsValid(tuple->t_xmax)) {
            tuple->t_infomask |= HEAP_XMAX_INVALID; 
            elog(DEBUG,"testing invalid xmax %lld - %ld/%d",tuple->t_xmin,ItemPointerGetBlockNumber(&tuple->t_ctid),
                    ItemPointerGetOffsetNumber(&tuple->t_ctid));
            return HEAPTUPLE_LIVE;
        }
         
	if (!(tuple->t_infomask & HEAP_XMAX_COMMITTED))
	{
		if (TransactionIdDidCommit(tuple->t_xmax)) {
			xmax_committed = true;
			if ( TransactionIdDidHardCommit(tuple->t_xmax))
				tuple->t_infomask |= HEAP_XMAX_COMMITTED;
		}
		else if (TransactionIdDidAbort(tuple->t_xmax))
		{
			tuple->t_infomask |= HEAP_XMAX_INVALID;
                        return HEAPTUPLE_LIVE;
		}
		else if (TransactionIdDidCrash(tuple->t_xmax)) 
		{
			/*elog(DEBUG,"12. tuple xmax %lld crashed",tuple->t_xmax);*/
			tuple->t_infomask |= HEAP_XMAX_INVALID;
                        return HEAPTUPLE_LIVE;
		}
		else 
		{
			return HEAPTUPLE_DELETE_IN_PROGRESS;
		}
		/* Should only get here if we set XMAX_COMMITTED */
		Assert(xmax_committed);
	}

	/*
	 * Deleter committed, but check special cases.
	 */

	if (tuple->t_infomask & HEAP_MARKED_FOR_UPDATE)
	{
		/* "deleting" xact really only marked it for update */
                return HEAPTUPLE_LIVE;
	}
#ifdef NOTUSED
	if (TransactionIdEquals(tuple->t_xmin, tuple->t_xmax))
	{
		/*
		 * inserter also deleted it, so it was never visible to anyone
		 * else
		 */
		return HEAPTUPLE_DEAD;
	}
#endif
	if (tuple->t_xmax >= OldestXmin)
	{
		/* deleting xact is too recent, tuple could still be visible */
		return HEAPTUPLE_RECENTLY_DEAD;
	}

	/* Otherwise, it's dead and removable */
	return HEAPTUPLE_DEAD;
}

void 
TakeUserSnapshot(void) 
{
	SnapshotHolder* holder = GetSnapshotHolder();
        if (holder->UserSnapshot != NULL) {
            elog(ERROR, "already holding a user snapshot");
        }
	holder->UserSnapshot = GetSnapshotData(false);
	holder->UserSnapshot->isUser = true;
	if ( holder->QuerySnapshot != NULL && 
	holder->QuerySnapshot != holder->SerializableSnapshot &&
	holder->QuerySnapshot != SnapshotAny ) {
		pfree(holder->QuerySnapshot->xip);
		pfree(holder->QuerySnapshot);
	}
	holder->QuerySnapshot = holder->UserSnapshot;
}

bool 
TransactionIdActiveDuringSnapshot(Snapshot snapshot,TransactionId id) {

	if (id >= snapshot->xmax)
		return true;

	if (id >= snapshot->xmin)
	{
		uint32		i;

		for (i = 0; i < snapshot->xcnt; i++)
		{
			if (id == snapshot->xip[i] ) {
				return true;
			}
		}
	}
	
	return false;
}

void 
DropUserSnapshot(void) 
{
	SnapshotHolder* holder = GetSnapshotHolder();
	holder->UserSnapshot = NULL;
}


void
SetQuerySnapshot(void)
{
	SnapshotHolder* holder = GetSnapshotHolder();

	/* Initialize snapshot overriding to false */
	GetSnapshotHolder()->ReferentialIntegritySnapshotOverride = false;

	/* 1st call in xaction */
	if (holder->SerializableSnapshot == NULL)
	{
		holder->SerializableSnapshot = GetSnapshotData(true);
		holder->QuerySnapshot = holder->SerializableSnapshot;
		holder->UserSnapshot = NULL;
		Assert(holder->QuerySnapshot != NULL);
		return;
	}

	if (holder->QuerySnapshot != holder->SerializableSnapshot &&
		holder->QuerySnapshot != holder->UserSnapshot &&
		holder->QuerySnapshot != SnapshotAny )
	{
		pfree(holder->QuerySnapshot->xip);  
		holder->QuerySnapshot->xip = NULL;
		pfree(holder->QuerySnapshot);      
		holder->QuerySnapshot = NULL;
	}

	switch ( GetTransactionInfo()->XactIsoLevel ) {
		case XACT_SERIALIZABLE:
			holder->QuerySnapshot = holder->SerializableSnapshot;
			break;
		case XACT_ALL:
			holder->QuerySnapshot = SnapshotAny;
			break;
		case XACT_USER:
			if ( holder->UserSnapshot != NULL ) {
				holder->QuerySnapshot = holder->UserSnapshot;
				break;
			} else {
				/*  fall through  */
			}
		case XACT_READ_COMMITTED:
			holder->QuerySnapshot = GetSnapshotData(false);
			break;
	}

	Assert(holder->QuerySnapshot != NULL);
}

void
FreeXactSnapshot(void)
{
	SnapshotHolder* env = GetSnapshotHolder();
	env->QuerySnapshot = NULL;
	env->SerializableSnapshot = NULL;	
	env->UserSnapshot = NULL;
}

void
CopySnapshot(Snapshot source,Snapshot dest) {
	dest->xmin = source->xmin;
	dest->xmax = source->xmax;
	dest->xcnt = source->xcnt;
	dest->isUser = source->isUser;
	dest->xip = MemoryContextAlloc(GetMemoryContext(dest),sizeof(TransactionId) * dest->xcnt);
        memmove(dest->xip,source->xip,sizeof(TransactionId) * dest->xcnt);
	dest->tid = source->tid;
}

SnapshotHolder*
GetSnapshotHolder(void)
{	
    SnapshotHolder* holder = snapshot_holder;

    if ( holder == NULL ) {
        holder = InitializeSnapshotHolder();
    }

    return holder;
}

SnapshotHolder*
InitializeSnapshotHolder(void)
{	
    SnapshotHolder* env = AllocateEnvSpace(snapshot_id,sizeof(SnapshotHolder));
    env->SnapshotDirty = (Snapshot)MemoryContextAlloc(MemoryContextGetTopContext(),
        sizeof(SnapshotData));

    env->QuerySnapshot = NULL;
    env->SerializableSnapshot = NULL;	
    env->UserSnapshot = NULL;
    snapshot_holder = env;

    return env;
}
