/*-------------------------------------------------------------------------
 *
 * shmem.h
 *	  shared memory management structures
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 *
 *-------------------------------------------------------------------------
 */
#ifndef SHMEM_H
#define SHMEM_H

#include <pthread.h>
#include "storage/spin.h"
#include "utils/hsearch.h"


/* The shared memory region can start at a different address
 * in every process.  Shared memory "pointers" are actually
 * offsets relative to the start of the shared memory region(s).
 */
typedef size_t SHMEM_OFFSET;

#define INVALID_OFFSET (-1)
#define BAD_LOCATION (-1)

/* start of the lowest shared memory region.  For now, assume that
 * there is only one shared memory region
 */
extern SHMEM_OFFSET ShmemBase;


/* coerce an offset into a pointer in this process's address space */
#define MAKE_PTR(xx_offs)\
  (ShmemBase+((unsigned long)(xx_offs)))

/* coerce a pointer into a shmem offset */
#define MAKE_OFFSET(xx_ptr)\
  (SHMEM_OFFSET) (((unsigned long)(xx_ptr))-ShmemBase)

#define SHM_PTR_VALID(xx_ptr)\
  (((unsigned long)xx_ptr) > ShmemBase)

/* cannot have an offset to ShmemFreeStart (offset 0) */
#define SHM_OFFSET_VALID(xx_offs)\
  ((xx_offs != 0) && (xx_offs != INVALID_OFFSET))


extern SPINLOCK ShmemLock;
extern SPINLOCK ShmemIndexLock;

/* shmemqueue.c */
typedef struct SHM_QUEUE
{
	SHMEM_OFFSET        prev;
	SHMEM_OFFSET        next;
        pthread_mutex_t*    lock;   
} SHM_QUEUE;

/* shmem.c */
PG_EXTERN void ShmemIndexReset(void);
PG_EXTERN void ShmemCreate(unsigned int key, size_t size);
PG_EXTERN int	InitShmem(unsigned int key, size_t size, int maxBackends);
PG_EXTERN void *ShmemAlloc(Size size);
PG_EXTERN int	ShmemIsValid(size_t addr);
PG_EXTERN HTAB *ShmemInitHash(char *name, int init_size, int max_size,
			  HASHCTL *infoP, int hash_flags);
PG_EXTERN bool ShmemPIDLookup(int pid, SHMEM_OFFSET *locationPtr);
PG_EXTERN SHMEM_OFFSET ShmemPIDDestroy(int pid);
PG_EXTERN long *ShmemInitStruct(char *name, Size size,
				bool *foundPtr);


typedef int TableID;

/* size constants for the shmem index table */
 /* max size of data structure string name */
#define SHMEM_INDEX_KEYSIZE (50)
 /* data in shmem index table hash bucket */
#define SHMEM_INDEX_DATASIZE (sizeof(ShmemIndexEnt) - SHMEM_INDEX_KEYSIZE)
#define SHMEM_INDEX_ENTRYSIZE (sizeof(ShmemIndexEnt))
 /* maximum size of the shmem index table */
#define SHMEM_INDEX_SIZE		 (100)

/* this is a hash bucket in the shmem index table */
typedef struct
{
	char		key[SHMEM_INDEX_KEYSIZE];		/* string name */
	unsigned long location;		/* location in shared mem */
	unsigned long size;			/* numbytes allocated for the structure */
} ShmemIndexEnt;

/*
 * prototypes for functions in shmqueue.c
 */
PG_EXTERN void SHMQueueInit(SHM_QUEUE *queue, pthread_mutex_t* mutex);
PG_EXTERN void SHMQueueElemInit(SHM_QUEUE *queue);
PG_EXTERN void SHMQueueDelete(SHM_QUEUE *queue);
PG_EXTERN void SHMQueueInsertTL(SHM_QUEUE *queue, SHM_QUEUE *elem);
PG_EXTERN void SHMQueueFirst(SHM_QUEUE *queue, Pointer *nextPtrPtr,
			  SHM_QUEUE *nextQueue);
PG_EXTERN bool SHMQueueEmpty(SHM_QUEUE *queue);
PG_EXTERN int SHMQueueLock(SHM_QUEUE  *queue);
PG_EXTERN int SHMQueueRelease(SHM_QUEUE  *queue);

#endif	 /* SHMEM_H */
