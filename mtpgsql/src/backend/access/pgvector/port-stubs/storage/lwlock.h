/* pgvector lwlock shim: pthread rwlocks for in-process parallel builds.
 *
 * Upstream HNSW parallel build relies on LW_SHARED (many readers) vs
 * LW_EXCLUSIVE (one writer) on flushLock / element locks. Mapping both
 * modes to an exclusive spinlock serializes the entire build and is wrong
 * for flushLock upgrade patterns.
 */

#ifndef STORAGE_LWLOCK_H
#define STORAGE_LWLOCK_H

#include <pthread.h>
#include "os.h"
#include "storage/m_lock.h"

struct LWLock
{
	pthread_rwlock_t rwlock;
};
typedef struct LWLock LWLock;

#define LW_SHARED		1
#define LW_EXCLUSIVE	2

static inline void
LWLockInitialize(LWLock *lock, int trancheId)
{
	(void) trancheId;
	pthread_rwlock_init(&lock->rwlock, NULL);
}

static inline void
LWLockAcquire(LWLock *lock, int mode)
{
	if (mode == LW_SHARED)
		pthread_rwlock_rdlock(&lock->rwlock);
	else
		pthread_rwlock_wrlock(&lock->rwlock);
}

static inline void
LWLockRelease(LWLock *lock)
{
	pthread_rwlock_unlock(&lock->rwlock);
}

#ifndef AddinShmemInitLock
extern int AddinShmemInitLock;
#endif

static inline int
LWLockNewTrancheId(void)
{
	return 1;
}

#ifndef SpinLockInit
#define SpinLockInit(lock)		m_init(lock)
#define SpinLockAcquire(lock)	m_lock(lock)
#define SpinLockRelease(lock)	m_unlock(lock)
#endif

#endif /* STORAGE_LWLOCK_H */
