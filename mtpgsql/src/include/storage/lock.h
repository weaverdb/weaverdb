/*-------------------------------------------------------------------------
 *
 * lock.h
 *	  POSTGRES low-level lock mechanism
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 *
 *-------------------------------------------------------------------------
 */
#ifndef LOCK_H_
#define LOCK_H_

#include <pthread.h>

#include "storage/ipc.h"
#include "storage/itemptr.h"
#include "storage/shmem.h"


/* originally in procq.h */
typedef struct THREAD_QUEUE
{
	SHM_QUEUE	links;
	int			size;
} THREAD_QUEUE;

typedef struct threadid {
    pid_t 	proc;
    pthread_t	thread;
} ThreadId;

typedef enum ThreadType {
        NORMAL_THREAD,
        POOLSWEEP_THREAD,
        DBWRITER_THREAD,
        DAEMON_THREAD,
        DOL_THREAD
} ThreadType;


#ifdef LOCK_DEBUG
extern int  Trace_lock_oidmin;
extern bool Trace_locks;
extern bool Trace_userlocks;
extern int  Trace_lock_table;
extern bool Debug_deadlocks;
#endif /* LOCK_DEBUG */


/* ----------------------
 * The following defines are used to estimate how much shared
 * memory the lock manager is going to require.
 * See LockShmemSize() in lock.c.
 *
 * NLOCKS_PER_XACT - The number of unique objects locked in a transaction
 *					 (this should be configurable!)
 * NLOCKENTS - The maximum number of lock entries in the lock table.
 * ----------------------
 */
#define NLOCKS_PER_XACT			64
#define NLOCKENTS(maxBackends)	(NLOCKS_PER_XACT*(maxBackends))

typedef int LOCKMASK;

typedef int LOCKMODE;
typedef int LOCKMETHOD;

/* MAX_LOCKMODES cannot be larger than the bits in LOCKMASK */
#define MAX_LOCKMODES	10

/*
 * MAX_LOCK_METHODS corresponds to the number of spin locks allocated in
 * CreateSpinLocks() or the number of shared memory locations allocated
 * for lock table spin locks in the case of machines with TAS instructions.
 */
#define MAX_LOCK_METHODS	4

#define INVALID_TABLEID		0

#define INVALID_LOCKMETHOD	INVALID_TABLEID
#define HEAP_LOCKMETHOD         1
#define INDEX_LOCKMETHOD         2
#define USER_LOCKMETHOD		3
#define MIN_LOCKMETHOD		HEAP_LOCKMETHOD

#define DEFAULT_LOCKMETHOD      HEAP_LOCKMETHOD

/*
 * This is the control structure for a lock table.	It
 * lives in shared memory:
 *
 * lockmethod -- the handle used by the lock table's clients to
 *		refer to the type of lock table being used.
 *
 * numLockModes -- number of lock types (READ,WRITE,etc) that
 *		are defined on this lock table
 *
 * conflictTab -- this is an array of bitmasks showing lock
 *		type conflicts. conflictTab[i] is a mask with the j-th bit
 *		turned on if lock types i and j conflict.
 *
 * prio -- each lockmode has a priority, so, for example, waiting
 *		writers can be given priority over readers (to avoid
 *		starvation).
 *
 * masterlock -- synchronizes access to the table
 */
typedef struct LOCKMETHODCTL
{
	LOCKMETHOD              lockmethod;
	int			numLockModes;
	int			conflictTab[MAX_LOCKMODES];
	int			prio[MAX_LOCKMODES];
	pthread_mutex_t         lock_guard;
	pthread_mutex_t         holder_guard;
} LOCKMETHODCTL;

/*
 * Non-shared header for a lock table.
 *
 * lockHash -- hash table holding per-locked-object lock information
 * holderHash -- hash table holding per-lock-holder lock information
 * ctl - shared control structure described above.
 */
typedef struct LOCKMETHODTABLE
{
	HTAB	   *    lockHash;
	HTAB	   *    holderHash;
	LOCKMETHODCTL * ctl;
} LOCKMETHODTABLE;


/*
 * LOCKTAG is the key information needed to look up a LOCK item in the
 * lock hashtable.  A LOCKTAG value uniquely identifies a lockable object.
 */
typedef struct LOCKTAG
{
	Oid			relId;
	Oid			dbId;
	union
	{
		BlockNumber blkno;
		TransactionId xid;
	}			objId;

	/*
	 * offnum should be part of objId.tupleId above, but would increase
	 * sizeof(LOCKTAG) and so moved here; currently used by userlocks
	 * only.
	 */
	OffsetNumber offnum;

	uint16		lockmethod;		/* needed by userlocks */
} LOCKTAG;


/*
 * Per-locked-object lock information:
 *
 * tag -- uniquely identifies the object being locked
 * mask -- union of the conflict masks of all lock types
 *		currently held on this object.
 * waitProcs -- queue of processes waiting for this lock
 * holders -- count of each lock type currently held on the
 *		lock.
 * nHolding -- total locks of all types.
 */
typedef struct LOCK
{
	/* hash key */
	LOCKTAG		tag;

	/* data */
	int			mask;
	int			waitMask;
	THREAD_QUEUE		waitThreads;
	int			holders[MAX_LOCKMODES];
	int			nHolding;
	int			activeHolders[MAX_LOCKMODES];
	int			nActive;
        int                     refs;
        int                     removing;
        pthread_mutex_t         protection;
} LOCK;

#define SHMEM_LOCKTAB_KEYSIZE  sizeof(LOCKTAG)
#define SHMEM_LOCKTAB_DATASIZE (sizeof(LOCK) - SHMEM_LOCKTAB_KEYSIZE)
#define SHMEM_LOCKTAB_ENTRYSIZE (sizeof(LOCK))

#define LOCK_LOCKMETHOD(lock) ((lock).tag.lockmethod)


/*
 * We may have several different transactions holding or awaiting locks
 * on the same lockable object.  We need to store some per-holder information
 * for each such holder (or would-be holder).
 *
 * HOLDERTAG is the key information needed to look up a HOLDER item in the
 * holder hashtable.  A HOLDERTAG value uniquely identifies a lock holder.
 *
 * There are two possible kinds of holder tags: a transaction (identified
 * both by the PID of the backend running it, and the xact's own ID) and
 * a session (identified by backend PID, with xid = InvalidTransactionId).
 *
 * Currently, session holders are used for user locks and for cross-xact
 * locks obtained for VACUUM.  We assume that a session lock never conflicts
 * with per-transaction locks obtained by the same backend.
 */
typedef struct HOLDERTAG
{
	SHMEM_OFFSET 			lock;			/* link to per-lockable-object information */
	ThreadId			pid;			/* PID of backend */
	TransactionId 			xid;			/* xact ID, or InvalidTransactionId */
} HOLDERTAG;

typedef struct HOLDER
{
	/* tag */
	HOLDERTAG	tag;

	/* data */
	int			holders[MAX_LOCKMODES];
	int			nHolding;

	SHM_QUEUE	queue;
} HOLDER;

#define SHMEM_HOLDERTAB_KEYSIZE  sizeof(HOLDERTAG)
#define SHMEM_HOLDERTAB_DATASIZE (sizeof(HOLDER) - SHMEM_HOLDERTAB_KEYSIZE)
#define SHMEM_HOLDERTAB_ENTRYSIZE (sizeof(HOLDER))

#define HOLDER_LOCKMETHOD(holder) \
		(((LOCK *) MAKE_PTR((holder).tag.lock))->tag.lockmethod)


typedef struct th
{
	SHM_QUEUE		links;			/*  has to be first  */		

	pthread_mutex_t			gate;
	pthread_cond_t			sem;			/*   semaphore lock  */

	int			locked;			/*  1 locked 0 not locked  */
	int			errType;		/* error code tells why we woke up */

	int			isSoft;		/* If critSects > 0, we are in sensitive
								 * routines that cannot be recovered when
								 * the process fails. */

	int			prio;			/* priority for sleep queue */

	volatile TransactionId 		xid;			/* transaction currently being executed by
								 * this proc */

	volatile TransactionId 		xmin;			/* minimal running XID as it was when we
								 * were starting our xact: vacuum must not
								 * remove tuples deleted by xid >= xmin ! */
	volatile int					state;
	SHMEM_OFFSET	   	waitLock;		/* Lock we're sleeping on ... */
	SHMEM_OFFSET	   	waitHolder;		/* Per-holder info for our lock */
	LOCKMODE		waitLockMode;	/* type of lock we're waiting for */
	int			token;			/* type of lock we sleeping for */
	int			holdLock;		/* while holding these locks */
	ThreadId		tid;			/* This backend's process id */
        ThreadType		ttype;
	Oid			databaseId;		/* OID of database this backend is using */
	short			sLocks[MAX_SPINS];	/* Spin lock stats */
	SHM_QUEUE		lockQueue;		
} THREAD;

#ifdef __cplusplus
extern "C" {
#endif
/*
 * function prototypes
 */
PG_EXTERN void InitLocks(void);
PG_EXTERN void LockDisable(bool status);
PG_EXTERN bool LockingDisabled(void);
PG_EXTERN LOCKMETHOD LockMethodTableInit(char *tabName, LOCKMASK *conflictsP,
					int *prioP, int numModes, int maxBackends);
PG_EXTERN LOCKMETHOD LockMethodTableRename(LOCKMETHOD lockmethod);
PG_EXTERN bool LockAcquire(LOCKMETHOD lockmethod, LOCKTAG *locktag,
						TransactionId xid, LOCKMODE lockmode,bool failfast);
PG_EXTERN bool LockRelease(LOCKMETHOD lockmethod, LOCKTAG *locktag,
						TransactionId xid, LOCKMODE lockmode);
PG_EXTERN bool LockReleaseAll(LOCKMETHOD lockmethod, THREAD *proc,
						   bool allxids, TransactionId xid);
PG_EXTERN int LockResolveConflicts(LOCKMETHOD lockmethod, LOCKMODE lockmode,
								LOCK *lock, HOLDER *holder, THREAD *proc,
								int *myHolders);
PG_EXTERN void GrantLock(LOCK *lock, HOLDER *holder, LOCKMODE lockmode);
PG_EXTERN size_t	LockShmemSize(int maxBackends);
PG_EXTERN bool DeadLockCheck(THREAD *thisProc, LOCK *findlock);
#ifdef LOCK_DEBUG
PG_EXTERN void DumpLocks(void);
PG_EXTERN void DumpAllLocks(void);
#endif

#ifdef __cplusplus
}
#endif

#endif	 /* LOCK_H */
