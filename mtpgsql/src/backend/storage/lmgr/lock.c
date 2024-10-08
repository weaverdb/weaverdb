 /*-------------------------------------------------------------------------
 *
 * lock.c
 *	  POSTGRES low-level lock mechanism
 *
 * Portions Copyright (c) 2000-2024, Myron Scott  <myron@weaverdb.org>
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *
 *
 * NOTES
 *	  Outside modules can create a lock table and acquire/release
 *	  locks.  A lock table is a shared memory hash table.  When
 *	  a process tries to acquire a lock of a type that conflicts
 *	  with existing locks, it is put to sleep using the routines
 *	  in storage/lmgr/proc.c.
 *
 *	  For the most part, this code should be invoked via lmgr.c
 *	  or another lock-management module, not directly.
 *
 *	Interface:
 *
 *	LockAcquire(), LockRelease(), LockMethodTableInit(),
 *	LockMethodTableRename(), LockReleaseAll,
 *	LockResolveConflicts(), GrantLock()
 *
 *-------------------------------------------------------------------------
 */

#include <unistd.h>
#include <signal.h>

#include "postgres.h"
#include "env/env.h"
#include "env/properties.h"

#include "access/xact.h"
#include "miscadmin.h"
#include "storage/multithread.h"
#include "utils/memutils.h"
#include "utils/ps_status.h"

static int	WaitOnLock(LOCKMETHOD lockmethod, LOCKMODE lockmode,
					   LOCK *lock, HOLDER *holder);
static void LockCountMyLocks(SHMEM_OFFSET lockOffset, THREAD *proc,
							 int *myHolders);
static int LockGetMyHoldLocks(SHMEM_OFFSET lockOffset, THREAD *proc);

static LOCK* SearchLockTable(LOCKMETHOD tid,LOCKTAG* lid, HASHACTION action);
static void ReleaseLockProtection(LOCK* lock);
static HOLDER* SearchHolderTable(LOCKMETHOD tid,HOLDERTAG* lid, HASHACTION action);

static char *lock_mode_names[] =
{
	"INVALID",
	"AccessShareLock",
	"RowShareLock",
	"RowExclusiveLock",
	"ShareUpdateExclusiveLock",
	"ShareLock",
	"ShareRowExclusiveLock",
	"ExclusiveLock",
	"AccessExclusiveLock"
};

#ifdef UNUSED
static char *DeadLockMessage = "Deadlock detected.\n\tSee the lock(l) manual page for a possible cause.";
#endif

#ifdef LOCK_DEBUG

/*------
 * The following configuration options are available for lock debugging:
 *
 *     trace_locks      -- give a bunch of output what's going on in this file
 *     trace_userlocks  -- same but for user locks
 *     trace_lock_oidmin-- do not trace locks for tables below this oid
 *                         (use to avoid output on system tables)
 *     trace_lock_table -- trace locks on this table (oid) unconditionally
 *     debug_deadlocks  -- currently dumps locks at untimely occasions ;)
 * Furthermore, but in storage/ipc/spin.c:
 *     trace_spinlocks  -- trace spinlocks (pretty useless)
 *
 * Define LOCK_DEBUG at compile time to get all this enabled.
 */

int  Trace_lock_oidmin  = BootstrapObjectIdData;
bool Trace_locks        = false;
bool Trace_userlocks    = false;
int  Trace_lock_table   = 0;
bool Debug_deadlocks    = false;


inline static bool
LOCK_DEBUG_ENABLED(const LOCK * lock)
{
    return
        (((LOCK_LOCKMETHOD(*lock) == DEFAULT_LOCKMETHOD && Trace_locks)
          || (LOCK_LOCKMETHOD(*lock) == USER_LOCKMETHOD && Trace_userlocks))
         && (lock->tag.relId >= Trace_lock_oidmin))
        || (Trace_lock_table && (lock->tag.relId == Trace_lock_table));
}


inline static void
LOCK_PRINT(const char * where, const LOCK * lock, LOCKMODE type)
{
	if (LOCK_DEBUG_ENABLED(lock))
        elog(DEBUG,
             "%s: lock(%lx) tbl(%d) rel(%u) db(%u) obj(%u) mask(%x) "
             "hold(%d,%d,%d,%d,%d,%d,%d,%d)=%d "
             "act(%d,%d,%d,%d,%d,%d,%d,%d)=%d wait(%d) type(%s)",
             where, MAKE_OFFSET(lock),
             lock->tag.lockmethod, lock->tag.relId, lock->tag.dbId,
             lock->tag.objId.blkno, lock->mask,
             lock->holders[1], lock->holders[2], lock->holders[3], lock->holders[4],
             lock->holders[5], lock->holders[6], lock->holders[7],lock->holders[8], lock->nHolding,
             lock->activeHolders[1], lock->activeHolders[2], lock->activeHolders[3],
             lock->activeHolders[4], lock->activeHolders[5], lock->activeHolders[6],
             lock->activeHolders[7],lock->activeHolders[8], lock->nActive,
             lock->waitProcs.size, lock_mode_names[type]);
}


inline static void
HOLDER_PRINT(const char * where, const HOLDER * holderP)
{
	if (
        (((HOLDER_LOCKMETHOD(*holderP) == DEFAULT_LOCKMETHOD && Trace_locks)
          || (HOLDER_LOCKMETHOD(*holderP) == USER_LOCKMETHOD && Trace_userlocks))
         && (((LOCK *)MAKE_PTR(holderP->tag.lock))->tag.relId >= Trace_lock_oidmin))
		|| (Trace_lock_table && (((LOCK *)MAKE_PTR(holderP->tag.lock))->tag.relId == Trace_lock_table))
        )
        elog(DEBUG,
             "%s: holder(%lx) lock(%lx) tbl(%d) pid(%d) xid(%u) hold(%d,%d,%d,%d,%d,%d,%d,%d)=%d",
             where, MAKE_OFFSET(holderP), holderP->tag.lock,
			 HOLDER_LOCKMETHOD(*(holderP)),
             holderP->tag.pid, holderP->tag.xid,
             holderP->holders[1], holderP->holders[2], holderP->holders[3], holderP->holders[4],
             holderP->holders[5], holderP->holders[6], holderP->holders[7],holderP->holders[8] holderP->nHolding);
}

#else  /* not LOCK_DEBUG */

#define LOCK_PRINT(where, lock, type)
#define HOLDER_PRINT(where, holderP)

#endif /* not LOCK_DEBUG */



SPINLOCK	LockMgrLock;		/* in Shmem or created in
								 * CreateSpinlocks() */

/* This is to simplify/speed up some bit arithmetic */

static LOCKMASK BITS_OFF[MAX_LOCKMODES];
static LOCKMASK BITS_ON[MAX_LOCKMODES];

/* -----------------
 * Disable flag
 * -----------------
 */

static bool  LockingIsDisabled = true;

/* -------------------
 * map from lockmethod to the lock table structure
 * -------------------
 */
static LOCKMETHODTABLE *LockMethodTable[MAX_LOCK_METHODS];

static int	NumLockMethods;

/* -------------------
 * InitLocks -- Init the lock module.  Create a private data
 *		structure for constructing conflict masks.
 * -------------------
 */
void
InitLocks(void)
{
	int			i;
	int			bit;

	bit = 1;
	for (i = 0; i < MAX_LOCKMODES; i++, bit <<= 1)
	{
		BITS_ON[i] = bit;
		BITS_OFF[i] = ~bit;
	}
}

/* -------------------
 * LockDisable -- sets LockingIsDisabled flag to TRUE or FALSE.
 * ------------------
 */
void
LockDisable(bool status)
{
	LockingIsDisabled = status;
}

/* -----------------
 * Boolean function to determine current locking status
 * -----------------
 */
bool
LockingDisabled(void)
{
	return LockingIsDisabled;
}


/*
 * LockMethodInit -- initialize the lock table's lock type
 *		structures
 *
 * Notes: just copying.  Should only be called once.
 */
static void
LockMethodInit(LOCKMETHODTABLE *lockMethodTable,
			   LOCKMASK *conflictsP,
			   int *prioP,
			   int numModes)
{
	int			i;

	lockMethodTable->ctl->numLockModes = numModes;
	numModes++;
	for (i = 0; i < numModes; i++, prioP++, conflictsP++)
	{
		lockMethodTable->ctl->conflictTab[i] = *conflictsP;
		lockMethodTable->ctl->prio[i] = *prioP;
	}
}

/*
 * LockMethodTableInit -- initialize a lock table structure
 *
 * Notes:
 *		(a) a lock table has four separate entries in the shmem index
 *		table.	This is because every shared hash table and spinlock
 *		has its name stored in the shmem index at its creation.  It
 *		is wasteful, in this case, but not much space is involved.
 *
 * NOTE: data structures allocated here are allocated permanently, using
 * TopMemoryContext and shared memory.  We don't ever release them anyway,
 * and in normal multi-backend operation the lock table structures set up
 * by the postmaster are inherited by each backend, so they must be in
 * TopMemoryContext.
 */
LOCKMETHOD
LockMethodTableInit(char *tabName,
					LOCKMASK *conflictsP,
					int *prioP,
					int numModes,
					int maxBackends)
{
	LOCKMETHODTABLE *lockMethodTable;
	char	   *shmemName;
	HASHCTL		info;
	int			hash_flags;
	bool		found;
	long		init_table_size,
				max_table_size;

	if (numModes > MAX_LOCKMODES)
	{
		elog(NOTICE, "LockMethodTableInit: too many lock types %d greater than %d",
			 numModes, MAX_LOCKMODES);
		return INVALID_LOCKMETHOD;
	}

	/* Compute init/max size to request for lock hashtables */
	max_table_size = NLOCKENTS(maxBackends);
	init_table_size = max_table_size / 10;

	/* Allocate a string for the shmem index table lookups. */
	/* This is just temp space in this routine, so palloc is OK. */
	shmemName = (char *) palloc(strlen(tabName) + 32);

	/* each lock table has a non-shared, permanent header */
	lockMethodTable = (LOCKMETHODTABLE *)
		MemoryContextAlloc(MemoryContextGetTopContext(), sizeof(LOCKMETHODTABLE));


	/* -----------------------
	 * allocate a control structure from shared memory or attach to it
	 * if it already exists.
	 * -----------------------
	 */
	sprintf(shmemName, "%s (ctl)", tabName);
	lockMethodTable->ctl = (LOCKMETHODCTL *)
		ShmemInitStruct(shmemName, sizeof(LOCKMETHODCTL), &found);

	if (!lockMethodTable->ctl)
		elog(FATAL, "LockMethodTableInit: couldn't initialize %s", tabName);

	/* -------------------
	 * no zero-th table
	 * -------------------
	 */
	NumLockMethods = 1;

	/* ----------------
	 * we're first - initialize
	 * ----------------
	 */
	if (!found)
	{
		MemSet(lockMethodTable->ctl, 0, sizeof(LOCKMETHODCTL));
		pthread_mutex_init(&lockMethodTable->ctl->lock_guard, &process_mutex_attr);
		pthread_mutex_init(&lockMethodTable->ctl->holder_guard, &process_mutex_attr);
		lockMethodTable->ctl->lockmethod = NumLockMethods;
	}

	/* --------------------
	 * other modules refer to the lock table by a lockmethod ID
	 * --------------------
	 */
	LockMethodTable[NumLockMethods] = lockMethodTable;
	NumLockMethods++;
	Assert(NumLockMethods <= MAX_LOCK_METHODS);

	/* ----------------------
	 * allocate a hash table for LOCK structs.  This is used
	 * to store per-locked-object information.
	 * ----------------------
	 */
	info.keysize = sizeof(LOCKTAG);
	info.entrysize = sizeof(LOCK);
	info.hash = tag_hash;
	hash_flags = (HASH_ELEM | HASH_FUNCTION);

	sprintf(shmemName, "%s (lock hash)", tabName);
	lockMethodTable->lockHash = ShmemInitHash(shmemName,
											  init_table_size,
											  max_table_size,
											  &info,
											  hash_flags);

	if (!lockMethodTable->lockHash)
		elog(FATAL, "LockMethodTableInit: couldn't initialize %s", tabName);
	Assert(lockMethodTable->lockHash->hash == tag_hash);

	/* -------------------------
	 * allocate a hash table for HOLDER structs.  This is used
	 * to store per-lock-holder information.
	 * -------------------------
	 */
	info.keysize = SHMEM_HOLDERTAB_KEYSIZE;
	info.entrysize = SHMEM_HOLDERTAB_ENTRYSIZE;
	info.hash = tag_hash;
	hash_flags = (HASH_ELEM | HASH_FUNCTION);

	sprintf(shmemName, "%s (holder hash)", tabName);
	lockMethodTable->holderHash = ShmemInitHash(shmemName,
												init_table_size,
												max_table_size,
												&info,
												hash_flags);

	if (!lockMethodTable->holderHash)
		elog(FATAL, "LockMethodTableInit: couldn't initialize %s", tabName);

	/* init ctl data structures */
	LockMethodInit(lockMethodTable, conflictsP, prioP, numModes);

	pfree(shmemName);

	return lockMethodTable->ctl->lockmethod;
}

/*
 * LockMethodTableRename -- allocate another lockmethod ID to the same
 *		lock table.
 *
 * NOTES: Both the lock module and the lock chain (lchain.c)
 *		module use table id's to distinguish between different
 *		kinds of locks.  Short term and long term locks look
 *		the same to the lock table, but are handled differently
 *		by the lock chain manager.	This function allows the
 *		client to use different lockmethods when acquiring/releasing
 *		short term and long term locks, yet store them all in one hashtable.
 */

LOCKMETHOD
LockMethodTableRename(LOCKMETHOD lockmethod)
{
	LOCKMETHOD	newLockMethod;

	if (NumLockMethods >= MAX_LOCK_METHODS)
		return INVALID_LOCKMETHOD;
	if (LockMethodTable[lockmethod] == INVALID_LOCKMETHOD)
		return INVALID_LOCKMETHOD;

	/* other modules refer to the lock table by a lockmethod ID */
	newLockMethod = NumLockMethods;
	NumLockMethods++;

	LockMethodTable[newLockMethod] = LockMethodTable[lockmethod];
	return newLockMethod;
}

/*
 * LockAcquire -- Check for lock conflicts, sleep if conflict found,
 *		set lock if/when no conflicts.
 *
 * Returns: TRUE if parameters are correct, FALSE otherwise.
 *
 * Side Effects: The lock is always acquired.  No way to abort
 *		a lock acquisition other than aborting the transaction.
 *		Lock is recorded in the lkchain.
 *
 *
 * Note on User Locks:
 *
 *		User locks are handled totally on the application side as
 *		long term cooperative locks which extend beyond the normal
 *		transaction boundaries.  Their purpose is to indicate to an
 *		application that someone is `working' on an item.  So it is
 *		possible to put an user lock on a tuple's oid, retrieve the
 *		tuple, work on it for an hour and then update it and remove
 *		the lock.  While the lock is active other clients can still
 *		read and write the tuple but they can be aware that it has
 *		been locked at the application level by someone.
 *		User locks use lock tags made of an uint16 and an uint32, for
 *		example 0 and a tuple oid, or any other arbitrary pair of
 *		numbers following a convention established by the application.
 *		In this sense tags don't refer to tuples or database entities.
 *		User locks and normal locks are completely orthogonal and
 *		they don't interfere with each other, so it is possible
 *		to acquire a normal lock on an user-locked tuple or user-lock
 *		a tuple for which a normal write lock already exists.
 *		User locks are always non blocking, therefore they are never
 *		acquired if already held by another process.  They must be
 *		released explicitly by the application but they are released
 *		automatically when a backend terminates.
 *		They are indicated by a lockmethod 2 which is an alias for the
 *		normal lock table, and are distinguished from normal locks
 *		by the following differences:
 *
 *										normal lock		user lock
 *
 *		lockmethod						1				2
 *		tag.dbId						database oid	database oid
 *		tag.relId						rel oid or 0	0
 *		tag.objId						block id		lock id2
 *										or xact id
 *		tag.offnum						0				lock id1
 *		xid.pid							backend pid		backend pid
 *		xid.xid							xid or 0		0
 *		persistence						transaction		user or backend
 *										or backend
 *
 *		The lockmode parameter can have the same values for normal locks
 *		although probably only WRITE_LOCK can have some practical use.
 *
 *														DZ - 22 Nov 1997
 */

bool
LockAcquire(LOCKMETHOD lockmethod, LOCKTAG *locktag,
			TransactionId xid, LOCKMODE lockmode,bool failfast)
{
	HOLDER	   *holder = NULL;
	HOLDERTAG	holdertag;
	LOCK	   *lock = NULL;
	int			status;
	int			myHolders[MAX_LOCKMODES];
	int			i;
	THREAD*                    thread = GetMyThread();


	/* ???????? This must be changed when short term locks will be used */
	locktag->lockmethod = lockmethod;

	Assert(lockmethod < NumLockMethods);

	if (LockingIsDisabled)
		return TRUE;

	/*
	 * Find or create a lock with this tag
	 */

        lock = (LOCK *) SearchLockTable(lockmethod, locktag, HASH_ENTER);

	/* ------------------
	 * Create the hash key for the holder table.
	 * ------------------
	 */
	MemSet(&holdertag, 0, sizeof(HOLDERTAG)); /* must clear padding, needed */
	holdertag.lock = MAKE_OFFSET(lock);
	holdertag.pid = thread->tid;
	holdertag.xid = xid;

        
        holder = (HOLDER *) SearchHolderTable(lockmethod, &holdertag, HASH_ENTER);

	/* ----------------
	 * lock->nHolding and lock->holders count the total number of holders
	 * either holding or waiting for the lock, so increment those immediately.
	 * The other counts don't increment till we get the lock.
	 * ----------------
	 */
	lock->nHolding++;
	lock->holders[lockmode]++;
	Assert((lock->nHolding > 0) && (lock->holders[lockmode] > 0));

	/* --------------------
	 * If I'm the only one holding any lock on this object, then there
	 * cannot be a conflict. The same is true if I already hold this lock.
	 * --------------------
	 */
	if (holder->nHolding == lock->nActive || holder->holders[lockmode] != 0)
	{
		GrantLock(lock, holder, lockmode);
		HOLDER_PRINT("LockAcquire: owning", holder);
		ReleaseLockProtection(lock);
		return TRUE;
	}

	/* --------------------
	 * If this process (under any XID) is a holder of the lock,
	 * then there is no conflict, either.
	 * --------------------
	 */
	LockCountMyLocks(holder->tag.lock, thread, myHolders);
	if (myHolders[lockmode] != 0)
	{
		GrantLock(lock, holder, lockmode);
		HOLDER_PRINT("LockAcquire: my other XID owning", holder);
		ReleaseLockProtection(lock);
		return TRUE;
	}

	/*
	 * If lock requested conflicts with locks requested by waiters...
	 */
	if (LockMethodTable[lockmethod]->ctl->conflictTab[lockmode] & lock->waitMask)
	{
		/*
		 * If my process doesn't hold any locks that conflict with waiters
		 * then force to sleep, so that prior waiters get first chance.
		 */
		for (i = 1; i <= LockMethodTable[lockmethod]->ctl->numLockModes; i++)
		{
			if (myHolders[i] > 0 &&
				LockMethodTable[lockmethod]->ctl->conflictTab[i] & lock->waitMask)
				break;			/* yes, there is a conflict */
		}

		if (i > LockMethodTable[lockmethod]->ctl->numLockModes)
		{
			HOLDER_PRINT("LockAcquire: another proc already waiting",
						 holder);
			status = STATUS_FOUND;
		}
		else
			status = LockResolveConflicts(lockmethod, lockmode,
							lock, holder,
							thread, myHolders);
										  
	}
	else
		status = LockResolveConflicts(lockmethod, lockmode,
									  lock, holder,
									   thread, myHolders);

	if (status == STATUS_OK)
		GrantLock(lock, holder, lockmode);
	else if (status == STATUS_FOUND)
	{
		/*
		 * User locks are non blocking. If we can't acquire a lock we must
		 * remove the holder entry and return FALSE without waiting. as well as no wait locks
		 */
		if (failfast || lockmethod == USER_LOCKMETHOD)
		{
			if (holder->nHolding == 0)
			{
                            SHMQueueLock(&holder->queue);
                            SHMQueueDelete(&holder->queue);
                            SHMQueueRelease(&holder->queue);
                            SearchHolderTable(lockmethod, &holder->tag,HASH_REMOVE);
			} else {
                            HOLDER_PRINT("LockAcquire: NHOLDING", holder);
                        }
			lock->nHolding--;
			lock->holders[lockmode]--;
			LOCK_PRINT("LockAcquire: user lock failed", lock, lockmode);
			Assert((lock->nHolding > 0) && (lock->holders[lockmode] >= 0));
			Assert(lock->nActive <= lock->nHolding);
                        ReleaseLockProtection(lock);
			return FALSE;
		}

		/*
		 * Construct bitmask of locks this process holds on this object.
		 */
		{
			int			holdLock = 0;
			int			tmpMask;

			for (i = 1, tmpMask = 2;
				 i <= LockMethodTable[lockmethod]->ctl->numLockModes;
				 i++, tmpMask <<= 1)
			{
				if (myHolders[i] > 0)
					holdLock |= tmpMask;
			}
			 thread->holdLock = holdLock;
		}

		/*
		 * Sleep till someone wakes me up.
		 */
		status = WaitOnLock(lockmethod, lockmode, lock, holder);

		/*
		 * Check the holder entry status, in case something in the ipc
		 * communication doesn't work correctly.
		 */
		if (!((holder->nHolding > 0) && (holder->holders[lockmode] > 0)))
		{
			HOLDER_PRINT("LockAcquire: INCONSISTENT", holder);
			LOCK_PRINT("LockAcquire: INCONSISTENT", lock, lockmode);
			elog(DEBUG,"LockAcquire: INCONSISTENT");
			/* Should we retry ? */
                        ReleaseLockProtection(lock);
			return FALSE;
		}
		HOLDER_PRINT("LockAcquire: granted", holder);
		LOCK_PRINT("LockAcquire: granted", lock, lockmode);
	}

	ReleaseLockProtection(lock);

	return status == STATUS_OK;
}

/* ----------------------------
 * LockResolveConflicts -- test for lock conflicts
 *
 * NOTES:
 *		Here's what makes this complicated: one transaction's
 * locks don't conflict with one another.  When many processes
 * hold locks, each has to subtract off the other's locks when
 * determining whether or not any new lock acquired conflicts with
 * the old ones.
 *
 * The caller can optionally pass the process's total holders counts, if
 * known.  If NULL is passed then these values will be computed internally.
 * ----------------------------
 */
int
LockResolveConflicts(LOCKMETHOD lockmethod,
					 LOCKMODE lockmode,
					 LOCK *lock,
					 HOLDER *holder,
					 THREAD *proc,
					 int *myHolders)		/* myHolders[] array or NULL */
{
	LOCKMETHODCTL *lockctl = LockMethodTable[lockmethod]->ctl;
	int			numLockModes = lockctl->numLockModes;
	int			bitmask;
	int			i,
				tmpMask;
	int			localHolders[MAX_LOCKMODES];

	Assert((holder->nHolding >= 0) && (holder->holders[lockmode] >= 0));

	/* ----------------------------
	 * first check for global conflicts: If no locks conflict
	 * with mine, then I get the lock.
	 *
	 * Checking for conflict: lock->mask represents the types of
	 * currently held locks.  conflictTable[lockmode] has a bit
	 * set for each type of lock that conflicts with mine.	Bitwise
	 * compare tells if there is a conflict.
	 * ----------------------------
	 */
	if (!(lockctl->conflictTab[lockmode] & lock->mask))
	{
		HOLDER_PRINT("LockResolveConflicts: no conflict", holder);
		return STATUS_OK;
	}

	/* ------------------------
	 * Rats.  Something conflicts. But it could still be my own
	 * lock.  We have to construct a conflict mask
	 * that does not reflect our own locks.  Locks held by the current
	 * process under another XID also count as "our own locks".
	 * ------------------------
	 */
	if (myHolders == NULL)
	{
		/* Caller didn't do calculation of total holding for me */
		LockCountMyLocks(holder->tag.lock, proc, localHolders);
		myHolders = localHolders;
	}

	/* Compute mask of lock types held by other processes */
	bitmask = 0;
	tmpMask = 2;
	for (i = 1; i <= numLockModes; i++, tmpMask <<= 1)
	{
		if (lock->activeHolders[i] != myHolders[i])
			bitmask |= tmpMask;
	}

	/* ------------------------
	 * now check again for conflicts.  'bitmask' describes the types
	 * of locks held by other processes.  If one of these
	 * conflicts with the kind of lock that I want, there is a
	 * conflict and I have to sleep.
	 * ------------------------
	 */
	if (!(lockctl->conflictTab[lockmode] & bitmask))
	{
		/* no conflict. OK to get the lock */
		HOLDER_PRINT("LockResolveConflicts: resolved", holder);
		return STATUS_OK;
	}

	HOLDER_PRINT("LockResolveConflicts: conflicting", holder);
	return STATUS_FOUND;
}

/*
 * LockCountMyLocks --- Count total number of locks held on a given lockable
 *		object by a given process (under any transaction ID).
 *
 * XXX This could be rather slow if the process holds a large number of locks.
 * Perhaps it could be sped up if we kept yet a third hashtable of per-
 * process lock information.  However, for the normal case where a transaction
 * doesn't hold a large number of locks, keeping such a table would probably
 * be a net slowdown.
 */
static void
LockCountMyLocks(SHMEM_OFFSET lockOffset, THREAD *proc, int *myHolders)
{
	HOLDER	   *holder = NULL;
	HOLDER	   *nextHolder = NULL;
	SHM_QUEUE  *lockQueue = &(proc->lockQueue);
	SHMEM_OFFSET end = MAKE_OFFSET(lockQueue);
	int			i;

	MemSet(myHolders, 0, MAX_LOCKMODES * sizeof(int));

        SHMQueueLock(lockQueue);
	if (SHMQueueEmpty(lockQueue)) {
                SHMQueueRelease(lockQueue);
		return;
        }

	SHMQueueFirst(lockQueue, (Pointer *) &holder, &holder->queue);

	do
	{
		/* ---------------------------
		 * XXX Here we assume the shared memory queue is circular and
		 * that we know its internal structure.  Should have some sort of
		 * macros to allow one to walk it.	mer 20 July 1991
		 * ---------------------------
		 */
		if (holder->queue.next == end)
			nextHolder = NULL;
		else
			SHMQueueFirst(&holder->queue,
						  (Pointer *) &nextHolder, &nextHolder->queue);

		if (lockOffset == holder->tag.lock)
		{
			for (i = 1; i < MAX_LOCKMODES; i++)
			{
				myHolders[i] += holder->holders[i];
			}
		}

		holder = nextHolder;
	} while (holder);

        SHMQueueRelease(lockQueue);
}

/*
 * LockGetMyHoldLocks -- compute bitmask of lock types held by a process
 *		for a given lockable object.
 */
static int
LockGetMyHoldLocks(SHMEM_OFFSET lockOffset, THREAD *proc)
{
	int			myHolders[MAX_LOCKMODES];
	int			holdLock = 0;
	int			i,
				tmpMask;

	LockCountMyLocks(lockOffset, proc, myHolders);

	for (i = 1, tmpMask = 2;
		 i < MAX_LOCKMODES;
		 i++, tmpMask <<= 1)
	{
		if (myHolders[i] > 0)
			holdLock |= tmpMask;
	}
	return holdLock;
}

/*
 * GrantLock -- update the lock and holder data structures to show
 *		the new lock has been granted.
 */
void
GrantLock(LOCK *lock, HOLDER *holder, LOCKMODE lockmode)
{
	lock->nActive++;
	lock->activeHolders[lockmode]++;
	lock->mask |= BITS_ON[lockmode];
	LOCK_PRINT("GrantLock", lock, lockmode);
	holder->holders[lockmode]++;
	holder->nHolding++;
	Assert((lock->nActive > 0) && (lock->activeHolders[lockmode] > 0));
	Assert(lock->nActive <= lock->nHolding);
        Assert((holder->nHolding > 0) && (holder->holders[lockmode] > 0));
}

/*
 * WaitOnLock -- wait to acquire a lock
 *
 * The locktable spinlock must be held at entry.
 */
static int
WaitOnLock(LOCKMETHOD lockmethod, LOCKMODE lockmode,
		   LOCK *lock, HOLDER *holder)
{
	LOCKMETHODTABLE *lockMethodTable = LockMethodTable[lockmethod];

	Assert(lockmethod < NumLockMethods);

	/*
	 * the waitqueue is ordered by priority. I insert myself according to
	 * the priority of the lock I am acquiring.
	 *
	 * SYNC NOTE: I am assuming that the lock table spinlock is sufficient
	 * synchronization for this queue.	That will not be true if/when
	 * people can be deleted from the queue by a SIGINT or something.
	 */
	LOCK_PRINT("WaitOnLock: sleeping on lock", lock, lockmode);


	if (ThreadSleep(lockMethodTable->ctl,
				  lockmode,
				  lock,
				  holder) != NO_ERROR)
	{
		/* -------------------
		 * We failed as a result of a deadlock, see HandleDeadLock().
		 * Decrement the lock nHolding and holders fields as
		 * we are no longer waiting on this lock.  Removal of the holder and
		 * lock objects, if no longer needed, will happen in xact cleanup.
		 * -------------------
		 */
		lock->nHolding--;
		lock->holders[lockmode]--;
		LOCK_PRINT("WaitOnLock: aborting on lock", lock, lockmode);
		Assert((lock->nHolding >= 0) && (lock->holders[lockmode] >= 0));
		if (lock->activeHolders[lockmode] == lock->holders[lockmode])
			lock->waitMask &= BITS_OFF[lockmode];
                ReleaseLockProtection(lock);
		elog(ERROR, "Lock Failed or Cancelled");
		/* not reached */
	}

	if (lock->activeHolders[lockmode] == lock->holders[lockmode])
		lock->waitMask &= BITS_OFF[lockmode];


	LOCK_PRINT("WaitOnLock: wakeup on lock", lock, lockmode);
	return STATUS_OK;
}

/*
 * LockRelease -- look up 'locktag' in lock table 'lockmethod' and
 *		release it.
 *
 * Side Effects: if the lock no longer conflicts with the highest
 *		priority waiting process, that process is granted the lock
 *		and awoken. (We have to grant the lock here to avoid a
 *		race between the waking process and any new process to
 *		come along and request the lock.)
 */
bool
LockRelease(LOCKMETHOD lockmethod, LOCKTAG *locktag,
			TransactionId xid, LOCKMODE lockmode)
{
	LOCK	   *lock;
	LOCKMETHODTABLE *lockMethodTable;
	HOLDER	   *holder;
	HOLDERTAG	holdertag;
	bool		wakeupNeeded = true;
        THREAD*          thread = GetMyThread();

#ifdef LOCK_DEBUG
	if (lockmethod == USER_LOCKMETHOD && Trace_userlocks)
        elog(DEBUG, "LockRelease: user lock tag [%u] %d", locktag->objId.blkno, lockmode);
#endif

	/* ???????? This must be changed when short term locks will be used */
	locktag->lockmethod = lockmethod;

	Assert(lockmethod < NumLockMethods);
	lockMethodTable = LockMethodTable[lockmethod];
	if (!lockMethodTable)
	{
		elog(NOTICE, "lockMethodTable is null in LockRelease");
		return FALSE;
	}

	if (LockingIsDisabled)
		return TRUE;


	/*
	 * Find a lock with this tag
	 */
	Assert(lockMethodTable->lockHash->hash == tag_hash);
	lock = (LOCK *) SearchLockTable(lockmethod, locktag,HASH_FIND);

	/*
	 * Find the holder entry for this holder.
	 */
	MemSet(&holdertag, 0, sizeof(HOLDERTAG)); /* must clear padding, needed */
	holdertag.lock = MAKE_OFFSET(lock);
	holdertag.pid = thread->tid;
	holdertag.xid = xid;

        holder = (HOLDER *) SearchHolderTable(lockmethod, &holdertag,HASH_FIND);
	if (!holder)
	{
		ReleaseLockProtection(lock);
#ifdef USER_LOCKS
		if (lockmethod == USER_LOCKMETHOD)
                    elog(NOTICE, "LockRelease: no lock with this tag");
		else
#endif
                    elog(NOTICE, "LockRelease: holder table corrupted");
		return FALSE;
	}
	HOLDER_PRINT("LockRelease: found", holder);
	Assert(holder->tag.lock == MAKE_OFFSET(lock));

	/*
	 * Check that we are actually holding a lock of the type we want to
	 * release.
	 */
	if (!(holder->holders[lockmode] > 0))
	{
		ReleaseLockProtection(lock);
		HOLDER_PRINT("LockRelease: WRONGTYPE", holder);
		elog(NOTICE, "LockRelease: you don't own a lock of type %s",
			 lock_mode_names[lockmode]);
		Assert(holder->holders[lockmode] >= 0);
		return FALSE;
	}
	Assert(holder->nHolding > 0);

	/*
	 * Now fix the per-holder lock stats.
	 */
	holder->holders[lockmode]--;
	holder->nHolding--;
	HOLDER_PRINT("LockRelease: updated", holder);
	Assert((holder->nHolding >= 0) && (holder->holders[lockmode] >= 0));

	/*
	 * If this was my last hold on this lock, delete my entry in the holder
	 * table.
	 */
	if (!holder->nHolding)
	{
                SHMQueueLock(&holder->queue);
		if (holder->queue.prev == INVALID_OFFSET)
			elog(NOTICE, "LockRelease: holder.prev == INVALID_OFFSET");
		if (holder->queue.next == INVALID_OFFSET)
			elog(NOTICE, "LockRelease: holder.next == INVALID_OFFSET");
		if (holder->queue.next != INVALID_OFFSET)
			SHMQueueDelete(&holder->queue);
                
                SHMQueueRelease(&holder->queue);
		HOLDER_PRINT("LockRelease: deleting", holder);
		SearchHolderTable(lockmethod, &holder->tag, HASH_REMOVE);
	}

	/*
	 * fix the general lock stats
	 */
	lock->nHolding--;
	lock->holders[lockmode]--;
	lock->nActive--;
	lock->activeHolders[lockmode]--;

	if (!(lock->activeHolders[lockmode]))
	{
		/* change the conflict mask.  No more of this lock type. */
		lock->mask &= BITS_OFF[lockmode];
	}

	if (lockMethodTable->ctl->conflictTab[lockmode] & lock->waitMask)
		wakeupNeeded = true;

	if (!lock->nHolding)
	{
		/* ------------------
		 * if there's no one waiting in the queue,
		 * we just released the last lock on this object.
		 * Delete it from the lock table.
		 * ------------------
		 */
		Assert(lockMethodTable->lockHash->hash == tag_hash);
	} else {
            if (wakeupNeeded)
		ThreadLockWakeup(lockmethod, lock);


       }

        SearchLockTable(lockmethod, &lock->tag,HASH_REMOVE);

       return TRUE;
}

/*
 * LockReleaseAll -- Release all locks in a process's lock queue.
 *
 * Well, not really *all* locks.
 *
 * If 'allxids' is TRUE, all locks of the specified lock method are
 * released, regardless of transaction affiliation.
 *
 * If 'allxids' is FALSE, all locks of the specified lock method and
 * specified XID are released.
 */
bool
LockReleaseAll(LOCKMETHOD lockmethod, THREAD *proc,
			   bool allxids, TransactionId xid)
{
	HOLDER	   *holder = NULL;
	HOLDER	   *nextHolder = NULL;
	SHM_QUEUE  *lockQueue = &(proc->lockQueue);
	SHMEM_OFFSET end = MAKE_OFFSET(lockQueue);
	LOCKMETHODTABLE *lockMethodTable;
	int			i,
				numLockModes;
	LOCK	   *lock;
	int			nleft;

	Assert(lockmethod < NumLockMethods);
	lockMethodTable = LockMethodTable[lockmethod];
	if (!lockMethodTable)
	{
		elog(NOTICE, "LockReleaseAll: bad lockmethod %d", lockmethod);
		return FALSE;
	}
        
        SHMQueueLock(lockQueue);
	if (SHMQueueEmpty(lockQueue)) {
            SHMQueueRelease(lockQueue);
		return TRUE;
        }

	numLockModes = lockMethodTable->ctl->numLockModes;

	SHMQueueFirst(lockQueue, (Pointer *) &holder, &holder->queue);
        SHMQueueRelease(lockQueue);

	nleft = 0;

	do
	{
		bool		wakeupNeeded = false;

		/* ---------------------------
		 * XXX Here we assume the shared memory queue is circular and
		 * that we know its internal structure.  Should have some sort of
		 * macros to allow one to walk it.	mer 20 July 1991
		 * ---------------------------
		 */
                SHMQueueLock(&holder->queue);
		if (holder->queue.next == end)
			nextHolder = NULL;
		else
			SHMQueueFirst(&holder->queue,
						  (Pointer *) &nextHolder, &nextHolder->queue);

		lock = (LOCK *) MAKE_PTR(holder->tag.lock);

                SHMQueueRelease(&holder->queue);

		/* Ignore items that are not of the lockmethod to be removed */
		if (LOCK_LOCKMETHOD(*lock) != lockmethod)
		{
			nleft++;
			goto next_item;
		}

		/* If not allxids, ignore items that are of the wrong xid */
		if (!allxids && xid != holder->tag.xid)
		{
			nleft++;
			goto next_item;
		}

                lock = SearchLockTable(lockmethod,&lock->tag,HASH_FIND);

		/* ------------------
		 * fix the general lock stats
		 * ------------------
		 */
		if (lock->nHolding != holder->nHolding)
		{
			for (i = 1; i <= numLockModes; i++)
			{
				Assert(holder->holders[i] >= 0);
				lock->holders[i] -= holder->holders[i];
				lock->activeHolders[i] -= holder->holders[i];
				Assert((lock->holders[i] >= 0) \
					   &&(lock->activeHolders[i] >= 0));
				if (!lock->activeHolders[i])
					lock->mask &= BITS_OFF[i];

				/*
				 * Read comments in LockRelease
				 */
				if (!wakeupNeeded && holder->holders[i] > 0 &&
					lockMethodTable->ctl->conflictTab[i] & lock->waitMask)
					wakeupNeeded = true;
			}
			lock->nHolding -= holder->nHolding;
			lock->nActive -= holder->nHolding;
			Assert((lock->nHolding >= 0) && (lock->nActive >= 0));
			Assert(lock->nActive <= lock->nHolding);
		}
		else
		{
			/* --------------
			 * set nHolding to zero so that we can garbage collect the lock
			 * down below...
			 * --------------
			 */
			lock->nHolding = 0;
			lock->nActive = 0;
                        /* Fix the lock status, just for next LOCK_PRINT message. */
			for (i = 1; i <= numLockModes; i++)
			{
				Assert(lock->holders[i] == lock->activeHolders[i]);
				lock->holders[i] = lock->activeHolders[i] = 0;
			}
		}
		LOCK_PRINT("LockReleaseAll: updated", lock, 0);

		HOLDER_PRINT("LockReleaseAll: deleting", holder);

		/*
		 * Remove the holder entry from the process' lock queue
		 */
                SHMQueueLock(&holder->queue);
		SHMQueueDelete(&holder->queue);
                SHMQueueRelease(&holder->queue);
		/*
		 * remove the holder entry from the hashtable
		 */

		SearchHolderTable(lockmethod,&holder->tag,HASH_REMOVE);

		if (!lock->nHolding)
		{
			/* --------------------
			 * if there's no one waiting in the queue, we've just released
			 * the last lock.
			 * --------------------
			 */
			LOCK_PRINT("LockReleaseAll: deleting", lock, 0);
			Assert(lockMethodTable->lockHash->hash == tag_hash);
		} else {
                    if (wakeupNeeded) {
			ThreadLockWakeup(lockmethod, lock);
                    }

                }
                
                SearchLockTable(lockmethod,&(lock->tag),HASH_REMOVE);
next_item:
		holder = nextHolder;
	} while (holder);

	/*
	 * Reinitialize the queue only if nothing has been left in.
	 */
	if (nleft == 0)
	{
            SHMQueueInit(lockQueue, &proc->gate);
	}

	return TRUE;
}

LOCK* SearchLockTable(LOCKMETHOD tid, LOCKTAG* lid, HASHACTION action) {
    LOCKMETHODTABLE* table = LockMethodTable[tid];
    pthread_mutex_t*        table_lock;
    LOCK*            target = (LOCK*)lid;
	bool		found;

        if (!table)
        {
                elog(NOTICE, "bad lock table %d", tid);
                return FALSE;
        }

	table_lock = &table->ctl->lock_guard;

        pthread_mutex_lock(table_lock);

        if ( action == HASH_REMOVE ) {
            target->removing -= 1;
    /*  make sure someone didn't grab the entry while not locked */        
            if ( target->refs || target->removing ) {
                pthread_mutex_unlock(table_lock);
   /*  drop the protection lock cause it won't be destroyed  */
                ReleaseLockProtection(target);
                return NULL;
            }
        }

        target = (LOCK*)hash_search(table->lockHash,
                            (Pointer) lid,
                            action,
                            &found);

	if (!target)
	{
		pthread_mutex_unlock(table_lock);
		elog(ERROR,"corrupted %ld %ld by %ld on %d",lid->relId,lid->dbId,(long)pthread_self(),action);
		return NULL;
	}

        if ( action == HASH_ENTER ) {
           if ( !found ) {
                pthread_mutex_init(&target->protection, &process_mutex_attr);
                target->refs = 0;
                target->mask = 0;
                target->nHolding = 0;
                target->nActive = 0;
                target->removing = 0;
                MemSet((char *) target->holders, 0, sizeof(int) * MAX_LOCKMODES);
                MemSet((char *) target->activeHolders, 0, sizeof(int) * MAX_LOCKMODES);
                ThreadQueueInit(&(target->waitThreads),&target->protection);
            }
            target->refs += 1;
        } else if ( action == HASH_REMOVE ) {
            pthread_mutex_destroy(&target->protection);
            target = NULL;
        } else {
/*  HASH_FIND  is the start of a remove  */
            target->refs -= 1;
            target->removing += 1;
        }

        pthread_mutex_unlock(table_lock);
        
        if ( action != HASH_REMOVE ) {
            pthread_mutex_lock(&target->protection);
        }

        return target;
}

void 
ReleaseLockProtection(LOCK* lock) {
    pthread_mutex_unlock(&lock->protection);
}

HOLDER* SearchHolderTable(LOCKMETHOD tid, HOLDERTAG* lid, HASHACTION action) {
    LOCKMETHODTABLE* table = LockMethodTable[tid];
    pthread_mutex_t*        table_lock;
    HOLDER*            target;
	bool		found;


	table_lock = &table->ctl->holder_guard;

        pthread_mutex_lock(table_lock);

        target = hash_search(table->holderHash,
                            (Pointer) lid,
                            action,
                            &found);

        if ( action == HASH_REMOVE ) {
            if ( !found ) elog(NOTICE,"Holder Table Corrupted (Remove not found)");
            target = NULL;
        } else if ( action == HASH_ENTER && !found ) {
            target->nHolding = 0;
            MemSet((char *) target->holders, 0, sizeof(int) * MAX_LOCKMODES);
            ThreadAddLock(&target->queue);
	}

        pthread_mutex_unlock(table_lock);

        return target;
        
}

size_t
LockShmemSize(int maxBackends)
{
	size_t			size = 0;

	size += MAXALIGN(sizeof(PROC_HDR)); /* ProcGlobal */
	size += MAXALIGN(maxBackends * sizeof(THREAD));		/* each GetEnv()->thread */
	size += MAXALIGN(maxBackends * sizeof(LOCKMETHODCTL));		/* each lockMethodTable->ctl */

	/* lockHash table */
	size += hash_estimate_size(NLOCKENTS(maxBackends),
							   SHMEM_LOCKTAB_ENTRYSIZE);

	/* holderHash table */
	size += hash_estimate_size(NLOCKENTS(maxBackends),
							   SHMEM_HOLDERTAB_ENTRYSIZE);

	/*
	 * Since the lockHash entry count above is only an estimate, add 10%
	 * safety margin.
	 */
	size += size / 10;

	return size;
}

/*
 * DeadlockCheck -- Checks for deadlocks for a given process
 *
 * We can't block on user locks, so no sense testing for deadlock
 * because there is no blocking, and no timer for the block.
 *
 * This code takes a list of locks a process holds, and the lock that
 * the process is sleeping on, and tries to find if any of the processes
 * waiting on its locks hold the lock it is waiting for.  If no deadlock
 * is found, it goes on to look at all the processes waiting on their locks.
 *
 * We have already locked the master lock before being called.
 */
bool
DeadLockCheck(THREAD *thisProc, LOCK *findlock)
{
	HOLDER	   *holder = NULL;
	HOLDER	   *nextHolder = NULL;
	THREAD	   *waitProc;
	THREAD_QUEUE *waitQueue;
	SHM_QUEUE  *lockQueue = &(thisProc->lockQueue);
	SHMEM_OFFSET end = MAKE_OFFSET(lockQueue);
	LOCKMETHODCTL *lockctl = LockMethodTable[DEFAULT_LOCKMETHOD]->ctl;
	LOCK	   *lock;
	int			i,
				j;
        THREAD*                 thread = GetMyThread();
	bool		first_run = (thisProc == thread);

	static THREAD *checked_procs[MAXBACKENDS];
	static int	nprocs;

	/* initialize at start of recursion */
	if (first_run)
	{
		checked_procs[0] = thisProc;
		nprocs = 1;
	}

        SHMQueueLock(lockQueue);
	if (SHMQueueEmpty(lockQueue)) {
            SHMQueueRelease(lockQueue);
		return false;
        }

	SHMQueueFirst(lockQueue, (Pointer *) &holder, &holder->queue);

	do
	{
		/* ---------------------------
		 * XXX Here we assume the shared memory queue is circular and
		 * that we know its internal structure.  Should have some sort of
		 * macros to allow one to walk it.	mer 20 July 1991
		 * ---------------------------
		 */
		if (holder->queue.next == end)
			nextHolder = NULL;
		else
			SHMQueueFirst(&holder->queue,
						  (Pointer *) &nextHolder, &nextHolder->queue);


		lock = (LOCK *) MAKE_PTR(holder->tag.lock);

		/* Ignore user locks */
		if (lock->tag.lockmethod != DEFAULT_LOCKMETHOD)
			goto nxtl;

		HOLDER_PRINT("DeadLockCheck", holder);
		LOCK_PRINT("DeadLockCheck", lock, 0);

		/*
		 * waitLock is always in lockQueue of waiting proc, if !first_run
		 * then upper caller will handle waitProcs queue of waitLock.
		 */
		if ((LOCK*)MAKE_PTR(thisProc->waitLock) == lock && !first_run)
			goto nxtl;

		/*
		 * If we found proc holding findlock and sleeping on some my other
		 * lock then we have to check does it block me or another waiters.
		 */
		if (lock == findlock && !first_run)
		{
			int			lm;

			Assert(holder->nHolding > 0);
			for (lm = 1; lm <= lockctl->numLockModes; lm++)
			{
				if (holder->holders[lm] > 0 &&
					lockctl->conflictTab[lm] & findlock->waitMask) {
                                        SHMQueueRelease(lockQueue);
					return true;
                                }
			}

			/*
			 * Else - get the next lock from thisProc's lockQueue
			 */
			goto nxtl;
		}

		waitQueue = &(lock->waitThreads);
		waitProc = (THREAD *) MAKE_PTR(waitQueue->links.prev);

		/*
		 * NOTE: loop must count down because we want to examine each item
		 * in the queue even if waitQueue->size decreases due to waking up
		 * some of the processes.
		 */
		for (i = waitQueue->size; --i >= 0; )
		{
			if (waitProc == thisProc)
			{
				/* This should only happen at first level */
				goto nextWaitProc;
			}
			if (lock == findlock)		/* first_run also true */
			{
				/*
				 * If me blocked by his holdlock...
				 */
				if (lockctl->conflictTab[thread->waitLockMode] & waitProc->holdLock)
				{
					/* and he blocked by me -> deadlock */
					if (lockctl->conflictTab[waitProc->waitLockMode] & thread->holdLock)
						return true;
					/* we shouldn't look at lockQueue of our blockers */
					goto nextWaitProc;
				}

				/*
				 * If he isn't blocked by me and we request
				 * non-conflicting lock modes - no deadlock here because
				 * he isn't blocked by me in any sense (explicitly or
				 * implicitly). Note that we don't do like test if
				 * !first_run (when thisProc is holder and non-waiter on
				 * lock) and so we call DeadLockCheck below for every
				 * waitProc in thisProc->lockQueue, even for waitProc-s
				 * un-blocked by thisProc. Should we? This could save us
				 * some time...
				 */
				if (!(lockctl->conflictTab[waitProc->waitLockMode] & thread->holdLock) &&
					!(lockctl->conflictTab[waitProc->waitLockMode] & (1 << thread->waitLockMode)))
					goto nextWaitProc;
			}

			/*
			 * Skip this waiter if already checked.
			 */
			for (j = 0; j < nprocs; j++)
			{
				if (checked_procs[j] == waitProc)
					goto nextWaitProc;
			}

			/* Recursively check this process's lockQueue. */
			Assert(nprocs < GetMaxBackends());
			checked_procs[nprocs++] = waitProc;

			if (DeadLockCheck(waitProc, findlock))
			{
				int			holdLock;

				/*
				 * Ok, but is waitProc waiting for me (thisProc) ?
				 */
				if ((LOCK*)MAKE_PTR(thisProc->waitLock) == lock)
				{
					Assert(first_run);
					holdLock = thisProc->holdLock;
				}
				else
				{
					/* should we cache holdLock to speed this up? */
					holdLock = LockGetMyHoldLocks(holder->tag.lock, thisProc);
					Assert(holdLock != 0);
				}
				if (lockctl->conflictTab[waitProc->waitLockMode] & holdLock)
				{
					/*
					 * Last attempt to avoid deadlock: try to wakeup myself.
					 */
					if (first_run)
					{
						if (LockResolveConflicts(DEFAULT_LOCKMETHOD,
												 thread->waitLockMode,
												 (LOCK*)MAKE_PTR(thread->waitLock),
												 (HOLDER*)MAKE_PTR(thread->waitHolder),
												 thread,
												 NULL) == STATUS_OK)
						{
							GrantLock((LOCK*)MAKE_PTR(thread->waitLock),
									  (HOLDER*)MAKE_PTR(thread->waitHolder),
									  thread->waitLockMode);
							ThreadWakeup(thread, NO_ERROR);
							return false;
						}
					}
                                        SHMQueueRelease(lockQueue);
					return true;
				}

				/*
				 * Hell! Is he blocked by any (other) holder ?
				 */
				if (LockResolveConflicts(DEFAULT_LOCKMETHOD,
										 waitProc->waitLockMode,
										 lock,
										 (HOLDER*)MAKE_PTR(waitProc->waitHolder),
										 waitProc,
										 NULL) != STATUS_OK)
				{
					/*
					 * Blocked by others - no deadlock...
					 */
					LOCK_PRINT("DeadLockCheck: blocked by others",
							   lock, waitProc->waitLockMode);
					goto nextWaitProc;
				}

				/*
				 * Well - wakeup this guy! This is the case of
				 * implicit blocking: thisProc blocked someone who
				 * blocked waitProc by the fact that he/someone is
				 * already waiting for lock.  We do this for
				 * anti-starving.
				 */
				GrantLock(lock, (HOLDER*)MAKE_PTR(waitProc->waitHolder), waitProc->waitLockMode);
				waitProc = ThreadWakeup(waitProc, NO_ERROR);
				/*
				 * Use next-proc link returned by ThreadWakeup, since this
				 * proc's own links field is now cleared.
				 */
				continue;
			}

nextWaitProc:
			waitProc = (THREAD *) MAKE_PTR(waitProc->links.prev);
		}

nxtl:
		holder = nextHolder;
	} while (holder);
        SHMQueueRelease(lockQueue);
	/* if we got here, no deadlock */
	return false;
}
#ifdef LOCK_DEBUG

/*
 * Dump all locks in the proc->lockQueue. Must have already acquired
 * the masterLock.
 */
void
DumpLocks(void)
{
	SHMEM_OFFSET location;
	THREAD	   *proc;
	SHM_QUEUE  *lockQueue;
	HOLDER	   *holder = NULL;
	HOLDER	   *nextHolder = NULL;
	SHMEM_OFFSET end;
	LOCK	   *lock;
	int			lockmethod = DEFAULT_LOCKMETHOD;
	LOCKMETHODTABLE *lockMethodTable;
        Env*            env = GetEnv();

	ShmemPIDLookup(env->threadPid, &location);
	if (location == INVALID_OFFSET)
		return;
	proc = (THREAD *) MAKE_PTR(location);
	if (proc != env->thread)
		return;
	lockQueue = &proc->lockQueue;
	end = MAKE_OFFSET(lockQueue);

	Assert(lockmethod < NumLockMethods);
	lockMethodTable = LockMethodTable[lockmethod];
	if (!lockMethodTable)
		return;

	if (proc->waitLock)
		LOCK_PRINT("DumpLocks: waiting on", proc->waitLock, 0);

	if (SHMQueueEmpty(lockQueue))
		return;

	SHMQueueFirst(lockQueue, (Pointer *) &holder, &holder->queue);

	do
	{
		/* ---------------------------
		 * XXX Here we assume the shared memory queue is circular and
		 * that we know its internal structure.  Should have some sort of
		 * macros to allow one to walk it.	mer 20 July 1991
		 * ---------------------------
		 */
		if (holder->queue.next == end)
			nextHolder = NULL;
		else
			SHMQueueFirst(&holder->queue,
						  (Pointer *) &nextHolder, &nextHolder->queue);

		lock = (LOCK *) MAKE_PTR(holder->tag.lock);

		HOLDER_PRINT("DumpLocks", holder);
		LOCK_PRINT("DumpLocks", lock, 0);

		holder = nextHolder;
	} while (holder);
}

/*
 * Dump all postgres locks. Must have already acquired the masterLock.
 */
void
DumpAllLocks(void)
{
	HOLDER	   *holder = NULL;
	LOCK	   *lock;
	int			lockmethod = DEFAULT_LOCKMETHOD;
	LOCKMETHODTABLE *lockMethodTable;
	HTAB	   *holderTable;
	HASH_SEQ_STATUS status;

	Assert(lockmethod < NumLockMethods);
	lockMethodTable = LockMethodTable[lockmethod];
	pthread_mutex_lock(&lockMethodTable->ctl->masterlock);

	holderTable = lockMethodTable->holderHash;

	if (env->thread->waitLock)
		LOCK_PRINT("DumpAllLocks: waiting on", env->thread->waitLock, 0);

	hash_seq_init(&status, holderTable);
	while ((holder = (HOLDER *) hash_seq_search(&status)) &&
		   (holder != (HOLDER *) TRUE))
	{
		HOLDER_PRINT("DumpAllLocks", holder);

		if (holder->tag.lock)
		{
			lock = (LOCK *) MAKE_PTR(holder->tag.lock);
			LOCK_PRINT("DumpAllLocks", lock, 0);
		}
		else
			elog(DEBUG, "DumpAllLocks: holder->tag.lock = NULL");
	}
	pthread_mutex_unlock(&lockMethodTable->ctl->masterlock);
}


#endif /*LOCK_DEBUG*/
