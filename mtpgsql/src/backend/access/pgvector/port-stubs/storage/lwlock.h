/* Stub lwlock.h for pgvector (single-threaded build; locks are no-ops) */

#ifndef STORAGE_LWLOCK_H
#define STORAGE_LWLOCK_H

struct LWLock
{
	int			dummy;
};
typedef struct LWLock LWLock;

#define LW_SHARED		1
#define LW_EXCLUSIVE	2

static inline void
LWLockInitialize(LWLock *lock, int trancheId)
{
	(void) lock;
	(void) trancheId;
}

static inline void
LWLockAcquire(LWLock *lock, int mode)
{
	(void) lock;
	(void) mode;
}

static inline void
LWLockRelease(LWLock *lock)
{
	(void) lock;
}

#ifndef AddinShmemInitLock
extern int AddinShmemInitLock;
#endif

static inline int
LWLockNewTrancheId(void)
{
	return 1;
}

#include "os.h"

PG_EXTERN void m_init(slock_t *lock);
PG_EXTERN void m_lock(slock_t *lock);
PG_EXTERN void m_unlock(slock_t *lock);

#ifndef SpinLockInit
#define SpinLockInit(lock)		m_init(lock)
#define SpinLockAcquire(lock)	m_lock(lock)
#define SpinLockRelease(lock)	m_unlock(lock)
#endif

#endif /* STORAGE_LWLOCK_H */
