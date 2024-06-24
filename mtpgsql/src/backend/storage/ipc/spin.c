/*-------------------------------------------------------------------------
 *
 * spin.c
 *	  routines for managing spin locks
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  $Header: /cvs/weaver/mtpgsql/src/backend/storage/ipc/spin.c,v 1.1.1.1 2006/08/12 00:21:24 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */
/*
 * POSTGRES has two kinds of locks: semaphores (which put the
 * process to sleep) and spinlocks (which are supposed to be
 * short term locks).  Currently both are implemented as SysV
 * semaphores, but presumably this can change if we move to
 * a machine with a test-and-set (TAS) instruction.  Its probably
 * a good idea to think about (and allocate) short term and long
 * term semaphores separately anyway.
 *
 * NOTE: These routines are not supposed to be widely used in Postgres.
 *		 They are preserved solely for the purpose of porting Mark Sullivan's
 *		 buffer manager to Postgres.
 */
#include <errno.h>
#include <pthread.h>
#include "postgres.h"


#ifndef HAS_TEST_AND_SET
#include <sys/sem.h>
#endif

#include "storage/multithread.h"

#ifdef SPIN_IS_MUTEX
#include "storage/m_lock.h"
#else
#include "storage/s_lock.h"
#endif

/* globals used in this file */
IpcSemaphoreId SpinLockId;

SPINLOCK HeapBufLock;
SPINLOCK IndexBufLock;
SPINLOCK FreeBufMgrLock;

void
CreateSpinlocks(IPCKey key)
{
	/* the spin lock shared memory must have been created by now */
	return;
}

void
InitSpinLocks(void)
{
	extern SPINLOCK ShmemLock;
	extern SPINLOCK ShmemIndexLock;
	extern SPINLOCK HeapBufLock;
	extern SPINLOCK IndexBufLock;
	extern SPINLOCK FreeBufMgrLock;
	extern SPINLOCK ProcStructLock;
	extern SPINLOCK SInvalLock;
	extern SPINLOCK OidGenLockId;
	extern SPINLOCK XidGenLockId;
	extern SPINLOCK XidSetLockId;
	extern SPINLOCK ControlFileLockId;

#ifdef STABLE_MEMORY_STORAGE
	extern SPINLOCK MMCacheLock;

#endif

	/* These six spinlocks have fixed location is shmem */
	ShmemLock = (SPINLOCK) SHMEMLOCKID;
	ShmemIndexLock = (SPINLOCK) SHMEMINDEXLOCKID;
	HeapBufLock = (SPINLOCK) HEAPBUFLOCKID;
	IndexBufLock = (SPINLOCK) INDEXBUFLOCKID;
        FreeBufMgrLock	= (SPINLOCK)FREEBUFMGRLOCKID;
	ProcStructLock = (SPINLOCK) PROCSTRUCTLOCKID;
	SInvalLock = (SPINLOCK) SINVALLOCKID;
	OidGenLockId = (SPINLOCK) OIDGENLOCKID;
	XidGenLockId = (SPINLOCK) XIDGENLOCKID;
	XidSetLockId = (SPINLOCK) XIDSETLOCKID;
	ControlFileLockId = (SPINLOCK) CNTLFILELOCKID;
#ifdef STABLE_MEMORY_STORAGE
	MMCacheLock = (SPINLOCK) MMCACHELOCKID;
#endif
	return;
}

#ifdef LOCKDEBUG
#define PRINT_LOCK(LOCK) \
	TPRINTF(TRACE_SPINLOCKS, \
			"(locklock = %d, flag = %d, nshlocks = %d, shlock = %d, " \
			"exlock =%d)\n", LOCK->locklock, \
			LOCK->flag, LOCK->nshlocks, LOCK->shlock, \
			LOCK->exlock)
#endif

/* from ipc.c */
extern SLock *SLockArray;

void
SpinAcquire(SPINLOCK lockid)
{
	SLock	   *slckP;

	/* This used to be in ipc.c, but move here to reduce function calls */
	slckP = &(SLockArray[lockid]);

	S_LOCK(slckP);
}

void
SpinRelease(SPINLOCK lockid)
{
	 SLock	   *slckP;

	/* This used to be in ipc.c, but move here to reduce function calls */
	slckP = &(SLockArray[lockid]);

	S_UNLOCK(slckP);
}

