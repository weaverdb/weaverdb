/*-------------------------------------------------------------------------
 *
 * spin.h
 *	  synchronization routines
 *
 *
 * Portions Copyright (c) 2006-2024, Myron Scott  <myron@weaverdb.org>
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 *
 *-------------------------------------------------------------------------
 */
#ifndef SPIN_H
#define SPIN_H

#include "storage/ipc.h"

/*
 * two implementations of spin locks
 *
 * sequent, sparc, sun3: real spin locks. uses a TAS instruction; see
 * src/storage/ipc/s_lock.c for details.
 *
 * default: fake spin locks using semaphores.  see spin.c
 *
 */

typedef int SPINLOCK;
#ifdef __cplusplus
extern "C" {
#endif

PG_EXTERN void CreateSpinlocks(IPCKey key);
PG_EXTERN void InitSpinLocks(void);
PG_EXTERN void SpinAcquire(SPINLOCK lockid);
PG_EXTERN void SpinRelease(SPINLOCK lockid);
#ifdef __cplusplus
}
#endif

#endif	 /* SPIN_H */
