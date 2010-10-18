/*-------------------------------------------------------------------------
 *
 * m_lock.h
 *	   This file contains the implementation (if any) for spinlocks.
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  $Header: /cvs/weaver/mtpgsql/src/include/storage/m_lock.h,v 1.1.1.1 2006/08/12 00:22:24 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */

/*
 *	 DESCRIPTION
 *		The public macros that must be provided are:
 *
 *		void S_INIT_LOCK(slock_t *lock)
 *
 *		void S_LOCK(slock_t *lock)
 *
 *		void S_UNLOCK(slock_t *lock)
 *
 *		void S_LOCK_FREE(slock_t *lock)
 *			Tests if the lock is free. Returns non-zero if free, 0 if locked.
 *
 *		The S_LOCK() macro	implements a primitive but still useful random
 *		backoff to avoid hordes of busywaiting lockers chewing CPU.
 *
 *		Effectively:
 *		void
 *		S_LOCK(slock_t *lock)
 *		{
 *			while (TAS(lock))
 *			{
 *			// back off the cpu for a semi-random short time
 *			}
 *		}
 *
 *		This implementation takes advantage of a tas function written
 *		(in assembly language) on machines that have a native test-and-set
 *		instruction. Alternative mutex implementations may also be used.
 *		This function is hidden under the TAS macro to allow substitutions.
 *
 *		#define TAS(lock) tas(lock)
 *		int tas(slock_t *lock)		// True if lock already set
 *
 *		There are default implementations for all these macros at the bottom
 *		of this file. Check if your platform can use these or needs to
 *		override them.
 *
 *	NOTES
 *		If none of this can be done, POSTGRES will default to using
 *		System V semaphores (and take a large performance hit -- around 40%
 *		of its time on a DS5000/240 is spent in semop(3)...).
 *
 *		AIX has a test-and-set but the recommended interface is the cs(3)
 *		system call.  This provides an 8-instruction (plus system call
 *		overhead) uninterruptible compare-and-set operation.  True
 *		spinlocks might be faster but using cs(3) still speeds up the
 *		regression test suite by about 25%.  I don't have an assembler
 *		manual for POWER in any case.
 *
 */
#ifndef S_LOCK_H
#define S_LOCK_H
#include <pthread.h>

#include "storage/ipc.h"


PG_EXTERN void m_lock(slock_t *lock);
#define S_LOCK(lock) m_lock(lock)

PG_EXTERN int m_check_lock(slock_t *lock);
#define S_LOCK_FREE(lock) m_check_lock(lock)

PG_EXTERN void m_unlock(slock_t *lock);
#define S_UNLOCK(lock) m_unlock(lock)

PG_EXTERN void m_init(slock_t *lock);
#define S_INIT_LOCK(lock) m_init(lock)

PG_EXTERN void m_destroy(slock_t *lock);
#define S_DESTROY_LOCK(lock) m_destroy(lock)

PG_EXTERN int m_trylock(slock_t *lock);
#define TAS(lock) m_trylock(lock)

PG_EXTERN void m_lock_sleep(int count);
#define s_lock_sleep(count) m_lock_sleep(count)

#endif
