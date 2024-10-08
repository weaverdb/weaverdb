/*-------------------------------------------------------------------------
 *
 * s_lock.c
 *	  buffer manager interface routines
 *
 * Portions Copyright (c) 2000-2024, Myron Scott  <myron@weaverdb.org>
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *
 *
 *-------------------------------------------------------------------------
 */

#include <sys/time.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>


#include "postgres.h"
#include "env/env.h"

#include "storage/m_lock.h"

#define M_NSPINCYCLE	20
#define M_MAX_BUSY		1000 * M_NSPINCYCLE

int			m_spincycle[M_NSPINCYCLE] =
{0, 0, 0, 0, 10000, 0, 0, 0, 10000, 0,
	0, 10000, 0, 0, 10000, 0, 10000, 0, 10000, 10000
};

#ifdef UNUSED
static void
m_lock_stuck(slock_t *lock)
{
	fprintf(stderr,
			"\nFATAL: s_lock(%08lx), stuck spinlock. Aborting.\n",
			(unsigned long) lock);
	fprintf(stdout,
			"\nFATAL: s_lock(%08lx), stuck spinlock. Aborting.\n",
			(unsigned long) lock);
}
#endif

void
m_lock_sleep(int spin)
{
	struct timeval delay;

	delay.tv_sec = 0;
	delay.tv_usec = m_spincycle[spin % M_NSPINCYCLE];
	(void) select(0, NULL, NULL, NULL, &delay);
}


void
m_lock(slock_t *lock)
{
	if ( pthread_mutex_lock(lock) ) {
		perror("M_LOCK (LOCK):");
	}
}

int
m_check_lock(slock_t *lock)
{
	int result = pthread_mutex_trylock(lock);
	if ( result == EBUSY ) return 1;
	else {
		pthread_mutex_unlock(lock);
		return 0;
	}
}

int 
m_trylock(slock_t *lock)
{
	return pthread_mutex_trylock(lock);
}

void
m_unlock(slock_t *lock)
{
	if ( pthread_mutex_unlock(lock) ) {
		perror("M_LOCK (UNLOCK):");
	}
}

void 
m_init(slock_t *lock)
{
        int result = pthread_mutex_init(lock,&process_mutex_attr);
        if (result) {
		perror("M_LOCK (INIT):");
                fprintf(stdout, "\nResult: %d\n", result);
                elog(FATAL, "result: %d", result);
        }
}

void 
m_destroy(slock_t *lock)
{
	pthread_mutex_destroy(lock);
}


