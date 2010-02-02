/*-------------------------------------------------------------------------
 *
 * proc.c
 *	  routines to manage per-process shared memory data structure
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  $Header: /cvs/weaver/mtpgsql/src/backend/storage/lmgr/multithread.c,v 1.3 2007/03/20 03:07:39 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */
/*
 *	Each postgres backend gets one of these.  We'll use it to
 *	clean up after the process should the process suddenly die.
 *
 *
 * Interface (a):
 *		ProcSleep(), ProcWakeup(), ProcWakeupNext(),
 *		ProcQueueAlloc() -- create a shm queue for sleeping processes
 *		ProcQueueInit() -- create a queue without allocing memory
 *
 * Locking and waiting for buffers can cause the backend to be
 * put to sleep.  Whoever releases the lock, etc. wakes the
 * process up again (and gives it an error code so it knows
 * whether it was awoken on an error condition).
 *
 * Interface (b):
 *
 * ProcReleaseLocks -- frees the locks associated with this process,
 * ProcKill -- destroys the shared memory state (and locks)
 *		associated with the process.
 *
 * 5/15/91 -- removed the buffer pool based lock chain in favor
 *		of a shared memory lock chain.	The write-protection is
 *		more expensive if the lock chain is in the buffer pool.
 *		The only reason I kept the lock chain in the buffer pool
 *		in the first place was to allow the lock table to grow larger
 *		than available shared memory and that isn't going to work
 *		without a lot of unimplemented support anyway.
 *
 * 4/7/95 -- instead of allocating a set of 1 semaphore per process, we
 *		allocate a semaphore from a set of PROC_NSEMS_PER_SET semaphores
 *		shared among backends (we keep a few sets of semaphores around).
 *		This is so that we can support more backends. (system-wide semaphore
 *		sets run out pretty fast.)				  -ay 4/95
 *
 * $Header: /cvs/weaver/mtpgsql/src/backend/storage/lmgr/multithread.c,v 1.3 2007/03/20 03:07:39 synmscott Exp $
 */
#include <sys/time.h>
#include <sys/sdt.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <pthread.h>

#include <sys/types.h>

#include "postgres.h"
#include "env/env.h"
#include "storage/multithread.h"

#include "miscadmin.h"

#include "storage/ipc.h"

#include "storage/lmgr.h"
#include "storage/bufmgr.h"
#include "utils/trace.h"

#include "storage/shmem.h" 

#include <errno.h>


void		HandleDeadLock(SIGNAL_ARGS);
static void ProcFreeAllSemaphores(void);
static bool GetOffWaitqueue(THREAD *);


typedef struct ThreadGlobals {
/*   replacement from proc.c   */
	THREAD			*thread;
	BackendId		MyBackendId;
	BackendTag		MyBackendTag;	
} ThreadGlobals;

static SectionId thread_id = SECTIONID("TRED");

/*  Thread local storage for ThreadGlobals  */
#ifdef TLS
TLS ThreadGlobals*  thread_globals = NULL;
#else
#define thread_globals  GetEnv()->thread_globals
#endif

static ThreadGlobals* GetThreadGlobals(void);
static ThreadGlobals* InitializeThreadGlobals(void);

static THREAD* ThreadEnqueue(LOCKMETHODCTL *lockctl,
		  LOCKMODE lockmode,
		  LOCK *lock,THREAD* self);
static void ThreadDequeue(THREAD* self);


#define DeadlockCheckTimer pg_options[OPT_DEADLOCKTIMEOUT]

/* --------------------
 * Spin lock for manipulating the shared process data structure:
 * ProcGlobal.... Adding an extra spin lock seemed like the smallest
 * hack to get around reading and updating this structure in shared
 * memory. -mer 17 July 1991
 * --------------------
 */
SPINLOCK	ProcStructLock;
static PROC_HDR *ProcGlobal = NULL;


static char *DeadLockMessage = "Deadlock detected -- See the lock(l) manual page for a possible cause.";

/*
 * InitProcGlobal -
 *	  initializes the global process table. We put it here so that
 *	  the postmaster can do this initialization. (ProcFreeAllSemaphores needs
 *	  to read this table on exiting the postmaster. If we have the first
 *	  backend do this, starting up and killing the postmaster without
 *	  starting any backends will be a problem.)
 *
 *	  We also allocate all the per-process semaphores we will need to support
 *	  the requested number of backends.  We used to allocate semaphores
 *	  only when backends were actually started up, but that is bad because
 *	  it lets Postgres fail under load --- a lot of Unix systems are
 *	  (mis)configured with small limits on the number of semaphores, and
 *	  running out when trying to start another backend is a common failure.
 *	  So, now we grab enough semaphores to support the desired max number
 *	  of backends immediately at initialization --- if the sysadmin has set
 *	  MaxBackends higher than his kernel will support, he'll find out sooner
 *	  rather than later.
 */
void
InitThreadGlobal(IPCKey key, int maxBackends)
{
	bool		found = false;

	/* attach to the free list */
	ProcGlobal = (PROC_HDR *)
		ShmemInitStruct("Proc Header", (unsigned) sizeof(PROC_HDR), &found);

	/* --------------------
	 * We're the first - initialize.
	 * XXX if found should ever be true, it is a sign of impending doom ...
	 * ought to complain if so?
	 * --------------------
	 */


	if (!found)
	{
		int			i;

		ProcGlobal->freeProcs = INVALID_OFFSET;
		ProcGlobal->groupleader = getpid();
                ProcGlobal->free = 0;
                ProcGlobal->alloc = 0;
                ProcGlobal->created = 0;
                ProcGlobal->count = 0;
	} else {
		if ( ProcGlobal->count == 64 ) return;
		ProcGlobal->subs[ProcGlobal->count++] = getpid();
	}
}

/* ------------------------
 * InitProc -- create a per-process data structure for this process
 * used by the lock manager on semaphore queues.
 * ------------------------
 */
void
InitThread(ThreadType tt)
{
	int counter = 0;
	bool		found = false;
	unsigned long location,
				myOffset;
				
	ThreadId		newthread;
	TransactionId   xid = InvalidTransactionId;
        ThreadGlobals* env = GetThreadGlobals();

	SpinAcquire(ProcStructLock);

	if (env->thread != NULL)
	{
		SpinRelease(ProcStructLock);
		elog(ERROR, "ProcInit: you already exist");
		return;
	}

	/* try to get a proc from the free list first */

	myOffset = ProcGlobal->freeProcs;

	if (myOffset != INVALID_OFFSET)
	{
		env->thread = (THREAD *) MAKE_PTR(myOffset);
		ProcGlobal->freeProcs = env->thread->links.next;
                ProcGlobal->free -= 1;
	}
	else
	{
		/*
		 * have to allocate one.  We can't use the normal shmem index
		 * table mechanism because the proc structure is stored by PID
		 * instead of by a global name (need to look it up by PID when we
		 * cleanup dead processes).
		 */

		env->thread = (THREAD *) ShmemAlloc(sizeof(THREAD),NULL);
		if (env->thread == NULL)
		{
			SpinRelease(ProcStructLock);
			elog(FATAL, "cannot create new proc: out of memory");
		}

		/* this cannot be initialized until after the buffer pool */
		SHMQueueInit(&(env->thread->lockQueue),&env->thread->gate);
                ProcGlobal->created += 1;
	}
        ProcGlobal->alloc += 1;
	/*
	 * zero out the spin lock counts and set the sLocks field for
	 * ProcStructLock to 1 as we have acquired this spinlock above but
	 * didn't record it since we didn't have MyProc until now.
	 */
	MemSet(env->thread->sLocks, 0, sizeof(env->thread->sLocks));
	env->thread->sLocks[ProcStructLock] = 1;
        if ( pthread_cond_init(&env->thread->sem,&process_cond_attr ) ) {
                perror("COND: ");
        }

	pthread_mutex_init(&env->thread->gate,&process_mutex_attr);

	/* ----------------------
	 * Release the lock.
	 * ----------------------
	 */
        memset(&newthread,0x00,sizeof(ThreadId));
        newthread.proc = getpid();
        newthread.thread = GetEnv()->eid;
        memcpy(&env->thread->tid,&newthread,sizeof(ThreadId));
	
        DTRACE_PROBE4(mtpg,thread__create,tt,ProcGlobal->created,ProcGlobal->alloc,ProcGlobal->free);
	SpinRelease(ProcStructLock);

        env->thread->ttype = tt;
	env->thread->databaseId = GetDatabaseId();
	env->thread->xid = xid;
	env->thread->xmin= xid;
	env->thread->state = TRANS_DEFAULT;

	/* ----------------
	 * Start keeping spin lock stats from here on.	Any botch before
	 * this initialization is forever botched
	 * ----------------
	 */
	MemSet(env->thread->sLocks, 0, MAX_SPINS * sizeof(*env->thread->sLocks));

	env->thread->errType = NO_ERROR;
	SHMQueueElemInit(&(env->thread->links));
}

/* -----------------------
 * get off the wait queue
 * -----------------------
 */
static bool
GetOffWaitqueue(THREAD *proc)
{
	bool		gotoff = false;

        if ( proc->links.lock == NULL ) return gotoff;

        SHMQueueLock(&proc->links);
	if (proc->links.next != INVALID_OFFSET)
	{
		LOCK   *waitLock = (LOCK*)MAKE_PTR(proc->waitLock);

                pthread_mutex_lock(&waitLock->protection);
		LOCKMODE lockmode = proc->waitLockMode;

		/* Remove proc from lock's wait queue */
                ThreadDequeue(proc);

		/* Undo increments of holder counts by waiting process */
		Assert(waitLock->nHolding > 0);

		--waitLock->nHolding;
		Assert(waitLock->holders[lockmode] > 0);
		--waitLock->holders[lockmode];
		/* don't forget to clear waitMask bit if appropriate */
		if (waitLock->activeHolders[lockmode] == waitLock->holders[lockmode])
			waitLock->waitMask &= ~(1 << lockmode);

		/* Clean up the proc's own state */
		proc->waitLock = (SHMEM_OFFSET)0;
		proc->waitHolder = (SHMEM_OFFSET)0;

		/* See if any other waiters can be woken up now */
		ThreadLockWakeup(LOCK_LOCKMETHOD(*waitLock), waitLock);

                pthread_mutex_unlock(&waitLock->protection);
		gotoff = true;
	}
        SHMQueueRelease(&proc->links);

	return gotoff;
}

/*
 * ProcReleaseLocks() -- release all locks associated with this process
 *
 */
void
ThreadReleaseLocks(bool isCommit)
{
	TransactionId xid;
	ThreadGlobals* tenv = GetThreadGlobals();
	
	xid = GetCurrentTransactionId();
	if (!tenv->thread)
            return;
  /*  this should never happen unless some other
  *    thread is releasing us.  otherwise we'd be
  *    in the thread sleep loop!
  */
	if ( GetOffWaitqueue(tenv->thread) ) {
            elog(DEBUG,"got off wait queue tid: %u",pthread_self());
        }
	LockReleaseAll(HEAP_LOCKMETHOD, tenv->thread,
				   !isCommit, xid);
	LockReleaseAll(INDEX_LOCKMETHOD, tenv->thread,
				   !isCommit, xid);
}

/*
 * ProcRemove -
 *	  used by the postmaster to clean up the global tables. This also frees
 *	  up the semaphore used for the lmgr of the process. (We have to do
 *	  this is the postmaster instead of doing a IpcSemaphoreKill on exiting
 *	  the process because the semaphore set is shared among backends and
 *	  we don't want to remove other's semaphores on exit.)
 */
bool
DestroyThread() 
{
	THREAD*   thread = GetThreadGlobals()->thread;
	
	pthread_mutex_destroy(&thread->gate); 
	pthread_cond_destroy(&thread->sem);    
	SpinAcquire(ProcStructLock);
	
	thread->links.next = ProcGlobal->freeProcs;
	ProcGlobal->freeProcs = MAKE_OFFSET(thread);
        ProcGlobal->free += 1;
        ProcGlobal->alloc -= 1;

        DTRACE_PROBE4(mtpg,thread__destroy,thread->ttype,ProcGlobal->created,ProcGlobal->alloc,ProcGlobal->free);
	SpinRelease(ProcStructLock);
}


/*
 * ProcQueue package: routines for putting processes to sleep
 *		and  waking them up
 */

/*
 * ProcQueueInit -- initialize a shared memory process queue
 */
void
ThreadQueueInit(THREAD_QUEUE *queue, pthread_mutex_t* lock)
{
	SHMQueueInit(&(queue->links), lock);
	queue->size = 0;
}

THREAD*
ThreadEnqueue(LOCKMETHODCTL *lockctl,
		  LOCKMODE lockmode,
		  LOCK *lock,THREAD* thread)
{
	int                 i;
	int                 myMask = (1 << lockmode);
	THREAD_QUEUE            *waitQueue = &(lock->waitThreads);
        THREAD              *proc = (THREAD *) MAKE_PTR(waitQueue->links.prev);
	int                 aheadHolders[MAX_LOCKMODES];
	bool                selfConflict = (lockctl->conflictTab[lockmode] & myMask),
                            prevSame = false;
	/* if we don't conflict with any waiter - be first in queue */
	if (!(lockctl->conflictTab[lockmode] & lock->waitMask)) {
            lock->waitMask |= myMask;
            SHMQueueInsertTL(&(proc->links), &(thread->links));
            waitQueue->size++;
            return proc;
        }

	for (i = 1; i < MAX_LOCKMODES; i++)
		aheadHolders[i] = lock->activeHolders[i];
	(aheadHolders[lockmode])++;

	for (i = 0; i < waitQueue->size; i++)
	{
		/* am I waiting for him ? */
		if (lockctl->conflictTab[lockmode] & proc->holdLock)
		{
			/* is he waiting for me ? */
			if (lockctl->conflictTab[proc->waitLockMode] & thread->holdLock)
			{
				/* Yes, report deadlock failure */
				thread->errType = STATUS_ERROR;
                                return NULL;
			}
			/* being waiting for him - go past */
		}
		/* if he waits for me */
		else if (lockctl->conflictTab[proc->waitLockMode] & thread->holdLock)
			break;
		/* if conflicting locks requested */
		else if (lockctl->conflictTab[proc->waitLockMode] & myMask)
		{

			/*
			 * If I request non self-conflicting lock and there are others
			 * requesting the same lock just before me - stay here.
			 */
			if (!selfConflict && prevSame)
				break;
		}

		/*
		 * Last attempt to don't move any more: if we don't conflict with
		 * rest waiters in queue.
		 */
		else if (!(lockctl->conflictTab[lockmode] & lock->waitMask))
			break;

		prevSame = (proc->waitLockMode == lockmode);
		(aheadHolders[proc->waitLockMode])++;
		if (aheadHolders[proc->waitLockMode] == lock->holders[proc->waitLockMode])
			lock->waitMask &= ~(1 << proc->waitLockMode);
		proc = (THREAD *) MAKE_PTR(proc->links.prev);
	}

        lock->waitMask |= myMask;
        SHMQueueInsertTL(&(proc->links), &(thread->links));
        waitQueue->size++;
        
        return proc;
}

static void ThreadDequeue(THREAD* target) {
        SHMQueueDelete(&(target->links));
        SHMQueueElemInit(&(target->links));
	(((LOCK*)MAKE_PTR(target->waitLock))->waitThreads.size)--;
}

/*
 * ProcSleep -- put a process to sleep
 *
 * P() on the semaphore should put us to sleep.  The process
 * semaphore is cleared by default, so the first time we try
 * to acquire it, we sleep.
 *
 * ASSUME: that no one will fiddle with the queue until after
 *		we release the spin lock.
 *
 * NOTES: The process queue is now a priority queue for locking.
 */
int
ThreadSleep(LOCKMETHODCTL *lockctl,
		  LOCKMODE lockmode,
		  LOCK *lock,
		  HOLDER *holder)
{

/*  the queue is already locked due to the fact that the mutex for 
    the queue and the lock are the same */
        ThreadGlobals           *tenv = GetThreadGlobals();
        THREAD*                 self = tenv->thread;
        int                     origMask = lock->waitMask;

	self->waitLock = MAKE_OFFSET(lock);
	self->waitHolder = MAKE_OFFSET(holder);
	self->waitLockMode = lockmode;
	/* We assume the caller set up MyProc->holdLock */

	if ( ThreadEnqueue(lockctl,lockmode,lock,self) ) {
            self->locked = 1;
            while ( self->locked == 1 ) {
    #ifndef MACOSX
                    timestruc_t  	t;
    #else
                    struct timespec		t;
    #endif
                    int 		err = 0;

                    t.tv_sec = time(NULL) + 2;
                    t.tv_nsec = 0;

                    err =  pthread_cond_timedwait(&self->sem,&lock->protection,&t);

                    while ( err != 0 ) {
                        if ( err == ETIMEDOUT && !CheckForCancel() ) break;

                        ThreadDequeue(self);
                        self->locked = 0;
                        self->errType = STATUS_ERROR;
                        lock->waitMask = origMask;
                        
                        break;
                    }
            }
        }

	self->waitLock = (SHMEM_OFFSET)0;
	self->waitHolder = (SHMEM_OFFSET)0;

	return self->errType;
}


/*
 * ProcWakeup -- wake up a process by releasing its private semaphore.
 *
 *	 remove the process from the wait queue and set its links invalid.
 *	 RETURN: the next process in the wait queue.
 */
THREAD *
ThreadWakeup(THREAD *proc, int errType)
{
	THREAD	   *retProc;

	/* assume that spinlock has been acquired */

	if (proc->links.prev == INVALID_OFFSET ||
		proc->links.next == INVALID_OFFSET)
		return (THREAD *) NULL;

	retProc = (THREAD *) MAKE_PTR(proc->links.prev);

	/* you have to update waitLock->waitProcs.size yourself */
        ThreadDequeue(proc);

	proc->errType = errType;
	proc->locked = 0;

	if ( pthread_cond_signal(&proc->sem) ) {
		elog(DEBUG,"Thread wake problem");
	}

	return retProc;
}

/*
 * ThreadLockWakeup -- routine for waking up processes when a lock is
 *		released.
 */
int
ThreadLockWakeup(LOCKMETHOD lockmethod, LOCK *lock)
{
	THREAD_QUEUE *queue = &(lock->waitThreads);
	THREAD	   *proc;
	int			awoken = 0;
	LOCKMODE	last_lockmode = 0;
	int			queue_size = queue->size;

	Assert(queue_size >= 0);

	if (!queue_size)
		return STATUS_NOT_FOUND;

	proc = (THREAD *) MAKE_PTR(queue->links.prev);

	while (queue_size-- > 0)
	{
            bool wake = TRUE;
            
                if (proc->waitLockMode == last_lockmode)
		{
			/*
			 * This proc will conflict as the previous one did, don't even
			 * try.
			 */
                    wake = FALSE;
		}

		/*
		 * Does this proc conflict with locks held by others ?
		 */
		if (wake && LockResolveConflicts(lockmethod,
                                            proc->waitLockMode,
                                             lock,
                                              (HOLDER*)MAKE_PTR(proc->waitHolder),
                                             proc,
                                             NULL) != STATUS_OK)
		{
#ifdef LOCK_FIFO
			break;
#else		
			/* Yes.  Quit if we already awoke at least one process. */
			if (awoken != 0)
				break;
			/* Otherwise, see if any later waiters can be awoken. */
			last_lockmode = proc->waitLockMode;
                        wake = FALSE;
#endif
		}

		/*
		 * OK to wake up this sleeping process.
		 */
		if ( wake ) {
                    GrantLock(lock, (HOLDER*)MAKE_PTR(proc->waitHolder), proc->waitLockMode);
                    proc =ThreadWakeup(proc, NO_ERROR);
                    awoken++;
                } else {
                    proc = (THREAD *) MAKE_PTR(proc->links.prev);
                }
	}
#ifdef LOCK_FIFO
	return STATUS_OK;
#else
	Assert(queue->size >= 0);

	if (awoken)
		return STATUS_OK;
	else
	{
		/* Something is still blocking us.	May have deadlocked. */
#ifdef LOCK_DEBUG
		if (lock->tag.lockmethod == USER_LOCKMETHOD ? Trace_userlocks : Trace_locks)
		{
			elog(DEBUG, "ThreadLockWakeup: lock(%lx) can't wake up any process",
				 MAKE_OFFSET(lock));
			if (Debug_deadlocks)
				DumpAllLocks();
		}
#endif
		return STATUS_NOT_FOUND;
	}
#endif
}

void
ThreadAddLock(SHM_QUEUE *elem)
{
	SHMQueueLock(&GetThreadGlobals()->thread->lockQueue);
        SHMQueueElemInit(elem);
	SHMQueueInsertTL(&GetThreadGlobals()->thread->lockQueue, elem);
	SHMQueueRelease(&GetThreadGlobals()->thread->lockQueue);
}

/* --------------------
 * We only get to this routine if we got SIGALRM after DEADLOCK_CHECK_TIMER
 * while waiting for a lock to be released by some other process.  If we have
 * a real deadlock, we must also indicate that I'm no longer waiting
 * on a lock so that other processes don't try to wake me up and screw
 * up my semaphore.
 * --------------------
 */

void
ThreadReleaseSpins(THREAD *proc)
{
	int			i;

	if (!proc)
		proc = GetThreadGlobals()->thread;

	if (!proc)
		return;
	for (i = 0; i < (int) MAX_SPINS; i++)
	{
		if (proc->sLocks[i])
		{
			Assert(proc->sLocks[i] == 1);
			SpinRelease(i);
		}
	}
	AbortBufferIO();
}

/*****************************************************************************
 *
 *****************************************************************************/

 void
 ShutdownProcess(bool master)
 {

 }
 
 void
 ThreadTransactionStart(TransactionId xid) {
	ThreadGlobals* global = GetThreadGlobals();
	pthread_mutex_lock(&global->thread->gate);
	global->thread->state = TRANS_START;
	global->thread->xmin = xid;	
	global->thread->xid = xid;
	pthread_mutex_unlock(&global->thread->gate);
 }
 
  
 TransactionId
 ThreadTransactionEnd() {
	ThreadGlobals* global = GetThreadGlobals();
	if ( global->thread != (THREAD *) NULL) {
		pthread_mutex_lock(&global->thread->gate);
		global->thread->state = TRANS_COMMIT;
		pthread_mutex_unlock(&global->thread->gate);
		return global->thread->xid;
	 }
 }
 
 void
 ThreadTransactionReset() {
	 ThreadGlobals* global = GetThreadGlobals();
	 if ( global->thread != (THREAD *) NULL) {
		pthread_mutex_lock(&global->thread->gate);
		global->thread->state = TRANS_DEFAULT;
		global->thread->xid = InvalidTransactionId;
		global->thread->xmin = InvalidTransactionId;
		pthread_mutex_unlock(&global->thread->gate);
	 }
 }
 
BackendId
GetMyBackendId() {
    return GetThreadGlobals()->MyBackendId;
}

  
THREAD*
GetMyThread() {
    ThreadGlobals* global = GetThreadGlobals();
    if ( global == NULL ) return NULL;
    return global->thread;
}

void
SetMyBackendId(BackendId in) {
    GetThreadGlobals()->MyBackendId = in;
}

 
BackendTag
GetMyBackendTag() {
    return GetThreadGlobals()->MyBackendTag;
}

void
SetMyBackendTag(BackendTag in) {
    GetThreadGlobals()->MyBackendTag = in;
}
 
ThreadGlobals*
GetThreadGlobals(void)
{
    ThreadGlobals* globals = thread_globals;
    if ( globals == NULL ) {
        globals = InitializeThreadGlobals();
        thread_globals = globals;
    }
    return globals;
}

ThreadGlobals*
InitializeThreadGlobals(void) {
    ThreadGlobals* info = AllocateEnvSpace(thread_id,sizeof(ThreadGlobals)); 
    memset(info,0x00,sizeof(ThreadGlobals));
    
    thread_globals = info;

    return info;
}
