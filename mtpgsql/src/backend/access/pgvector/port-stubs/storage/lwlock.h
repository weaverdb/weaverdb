/* pgvector lwlock shim: pthread-backed locks for in-process parallel builds */

#ifndef STORAGE_LWLOCK_H
#define STORAGE_LWLOCK_H

#include "os.h"
#include "storage/m_lock.h"

struct LWLock
{
	slock_t		lock;
};
typedef struct LWLock LWLock;

#define LW_SHARED		1
#define LW_EXCLUSIVE	2

static inline void
LWLockInitialize(LWLock *lock, int trancheId)
{
	(void) trancheId;
	m_init(&lock->lock);
}

static inline void
LWLockAcquire(LWLock *lock, int mode)
{
	(void) mode;
	m_lock(&lock->lock);
}

static inline void
LWLockRelease(LWLock *lock)
{
	m_unlock(&lock->lock);
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
