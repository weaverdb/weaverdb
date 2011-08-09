/*-------------------------------------------------------------------------
 *
 * proc.h
 *
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * $Id: multithread.h,v 1.1.1.1 2006/08/12 00:22:24 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */
#ifndef _MULTITHREAD_H_
#define _MULTITHREAD_H_

#undef _ASM

#include <pthread.h>
#include <sys/types.h>
#include <setjmp.h> 
#include "access/xlog.h"
#include "storage/lock.h"
#include "storage/backendid.h"

/*
 * PROC_NSEMS_PER_SET is the number of semaphores in each sys-V semaphore set
 * we allocate.  It must be *less than* 32 (or however many bits in an int
 * on your machine), or our free-semaphores bitmap won't work.  You also must
 * not set it higher than your kernel's SEMMSL (max semaphores per set)
 * parameter, which is often around 25.
 *
 * MAX_PROC_SEMS is the maximum number of per-process semaphores (those used
 * by the lock mgr) we can keep track of.  It must be a multiple of
 * PROC_NSEMS_PER_SET.
 */

typedef struct procglobal
{
	SHMEM_OFFSET 	freeProcs;
	pid_t		groupleader;
	pid_t		subs[64];
	int		count;
        int             free;
        int             alloc;
        int             created;

	/*
	 * In each freeSemMap entry, the PROC_NSEMS_PER_SET least-significant
	 * bits flag whether individual semaphores are in use, and the next
	 * higher bit is set to show that the entire set is allocated.
	 */
} PROC_HDR;
#define PROC_INCR_SLOCK(lock) \
do { \
	if (GetEnv() != NULL && GetEnv()->thread) (GetEnv()->thread->sLocks[(lock)])++; \
} while (0)

#define PROC_DECR_SLOCK(lock) \
do { \
	if (GetEnv() != NULL && GetEnv()->thread) (GetEnv()->thread->sLocks[(lock)])--; \
} while (0)

/*
#define PROC_INCR_SLOCK(lock) \
do { \
	if (GetEnv()->thread) (GetEnv()->thread->sLocks[(lock)])++; \
} while (0)

#define PROC_DECR_SLOCK(lock) \
do { \
	if (GetEnv()->thread) (GetEnv()->thread->sLocks[(lock)])--; \
} while (0)
*/

/*
 * flags explaining why process woke up
 */
#define NO_ERROR		0
#define ERR_TIMEOUT		1
#define ERR_BUFFER_IO	2

#define MAX_PRIO		50
#define MIN_PRIO		(-1)

extern SPINLOCK ThreadStructLock;

/*
 * Function Prototypes
 */

PG_EXTERN void InitThread(ThreadType tt);
PG_EXTERN void InitThreadGlobal(IPCKey key, int maxBackends);
PG_EXTERN void ThreadReleaseLocks(bool isCommit);
PG_EXTERN bool DestroyThread(void);

/* make static in storage/lmgr/proc.c -- jolly */

PG_EXTERN void ThreadTransactionStart(TransactionId xid);
PG_EXTERN TransactionId ThreadTransactionEnd();
PG_EXTERN void ThreadTransactionReset();

PG_EXTERN void ThreadQueueInit(THREAD_QUEUE *queue, pthread_mutex_t* lock);
PG_EXTERN int ThreadSleep(LOCKMETHODCTL *lockctl,LOCKMODE lockmode,LOCK *lock,HOLDER *holder);
PG_EXTERN THREAD *ThreadWakeup(THREAD *proc, int errType);
PG_EXTERN int ThreadLockWakeup( LOCKMETHOD lockmethod,LOCK *lock);
PG_EXTERN void ThreadAddLock(SHM_QUEUE *elem);
PG_EXTERN void ThreadReleaseSpins(THREAD *proc);
PG_EXTERN void LockWaitCancel(void); 
PG_EXTERN void ShutdownProcess(bool master);

PG_EXTERN THREAD* GetMyThread();

PG_EXTERN BackendId GetMyBackendId();
PG_EXTERN void SetMyBackendId(BackendId in);

PG_EXTERN BackendTag GetMyBackendTag();
PG_EXTERN void SetMyBackendTag(BackendTag in);


#endif	 /* PROC_H */
