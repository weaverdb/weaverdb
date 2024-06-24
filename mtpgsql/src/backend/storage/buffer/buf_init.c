/*-------------------------------------------------------------------------
 *
 * buf_init.c
 *	  buffer manager initialization routines
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  $Header: /cvs/weaver/mtpgsql/src/backend/storage/buffer/buf_init.c,v 1.2 2007/01/09 00:13:37 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */

#include <sys/file.h>
#include <sys/mman.h>
#include <math.h>
#include <signal.h>
#include <pthread.h>


#include "postgres.h"

#include "env/env.h"
#include "env/properties.h"
#include "catalog/catalog.h"
#include "executor/execdebug.h"
#include "miscadmin.h"
#include "storage/buf.h"
#include "storage/buf_internals.h"
#include "storage/bufmgr.h"
#include "storage/fd.h"
#include "storage/ipc.h"
#include "storage/lmgr.h"
#ifdef SPIN_IS_MUTEX
#include "storage/m_lock.h"
#else
#include "storage/s_lock.h"
#endif
#include "storage/shmem.h"
#include "storage/smgr.h"
#include "storage/spin.h"
#include "utils/builtins.h"
#include "utils/hsearch.h"
#include "utils/memutils.h"

/*
 *	if BMTRACE is defined, we trace the last 200 buffer allocations and
 *	deallocations in a circular buffer in shared memory.
 */

int			ShowPinTrace = 0;

int                     NTables = 1;
int			NBuffers = DEF_NBUFFERS;	/* default is set in config.h */
int			SBuffers = DEF_NBUFFERS;	/* default is set in config.h */
int                     MaxBuffers = DEF_NBUFFERS;
int			Num_Descriptors;


BufferDesc *BufferDescriptors;
static char* BufferBlocks;
MemoryContext buffer_cxt;

/*
 * Data Structures:
 *		buffers live in a freelist and a lookup data structure.
 *
 *
 * Buffer Lookup:
 *		Two important notes.  First, the buffer has to be
 *		available for lookup BEFORE an IO begins.  Otherwise
 *		a second process trying to read the buffer will
 *		allocate its own copy and the buffeer pool will
 *		become inconsistent.
 *
 * Buffer Replacement:
 *		see freelist.c.  A buffer cannot be replaced while in
 *		use either by data manager or during IO.
 *
 * WriteBufferBack:
 *		currently, a buffer is only written back at the time
 *		it is selected for replacement.  It should
 *		be done sooner if possible to reduce latency of
 *		BufferAlloc().	Maybe there should be a daemon process.
 *
 * Synchronization/Locking:
 *
 * BufMgrLock lock -- must be acquired before manipulating the
 *		buffer queues (lookup/freelist).  Must be released
 *		before exit and before doing any IO.
 *
 * IO_IN_PROGRESS -- this is a flag in the buffer descriptor.
 *		It must be set when an IO is initiated and cleared at
 *		the end of	the IO.  It is there to make sure that one
 *		process doesn't start to use a buffer while another is
 *		faulting it in.  see IOWait/IOSignal.
 *
 * refCount --	A buffer is pinned during IO and immediately
 *		after a BufferAlloc().	A buffer is always either pinned
 *		or on the freelist but never both.	The buffer must be
 *		released, written, or flushed before the end of
 *		transaction.
 *
 * PrivateRefCount -- Each buffer also has a private refCount the keeps
 *		track of the number of times the buffer is pinned in the current
 *		processes.	This is used for two purposes, first, if we pin a
 *		a buffer more than once, we only need to change the shared refCount
 *		once, thus only lock the buffer pool once, second, when a transaction
 *		aborts, it should only unpin the buffers exactly the number of times it
 *		has pinned them, so that it will not blow away buffers of another
 *		backend.
 *
 */

long int	ReadBufferCount;
long int	ReadLocalBufferCount;
long int	BufferHitCount;
long int	LocalBufferHitCount;
long int	BufferFlushCount;
long int	LocalBufferFlushCount;

static void InitializeBuffers(int start, int count, char* block);
static void* LockedAlloc(MemoryContext cxt, Size size) {
    void* pointer = MemoryContextAlloc(cxt,size);
    if ( pointer != NULL ) {
        mlock(pointer,size);
    }
    return pointer;
}

/*
 * Initialize module:
 *
 * should calculate size of pool dynamically based on the
 * amount of available memory.
 */
void
InitBufferPool(IPCKey key)
{
	bool		foundBufs = false,foundDescs = false;

        if ( MaxBuffers < NBuffers ) MaxBuffers = NBuffers;
                
        if ( key == PrivateIPCKey ) {
            buffer_cxt = AllocSetContextCreate(NULL,"BufferMainMemory",
                ALLOCSET_DEFAULT_MINSIZE,
                ALLOCSET_DEFAULT_INITSIZE,
                ALLOCSET_DEFAULT_MAXSIZE);
            Num_Descriptors = MaxBuffers;
            BufferDescriptors = os_malloc(Num_Descriptors * sizeof(BufferDesc));
            BufferBlocks = NULL;
       } else {
            buffer_cxt = NULL;
            MaxBuffers = NBuffers;
            Num_Descriptors = NBuffers;
            BufferDescriptors = (BufferDesc*)
                    ShmemInitStruct("Buffer Descriptors",Num_Descriptors * sizeof(BufferDesc), &foundDescs);

            BufferBlocks = (char*)
                    ShmemInitStruct("Buffer Blocks",NBuffers * BLCKSZ, &foundBufs);
        
            if ( !BufferDescriptors || !BufferBlocks ) elog(FATAL,"failed to create buffer in shared memory");

        }
        
        if (foundDescs || foundBufs)
        {
                /* both should be present or neither */
                Assert(foundDescs && foundBufs);
        } else {
                InitializeBuffers(0,MaxBuffers,BufferBlocks);
        }
        
        elog(DEBUG,"using %d buffers max buffers %d",NBuffers,MaxBuffers);

	/* Init the rest of the module */
	InitBufTable(NTables);
	InitFreeList(!foundDescs);
}

int
AddMoreBuffers(int count) {
    if ( NBuffers == MaxBuffers ) {
        int i;
        BufferDesc *buf,*head,*tail;
        int activate = 0;
        
        head = NULL;
        tail = NULL;
        
        for (i = 0; activate < count && i < MaxBuffers; i++)
        {
            buf = &BufferDescriptors[i];
            pthread_mutex_lock(&buf->cntx_lock.guard);
            if ( buf->locflags & BM_RETIRED ) {
                activate += 1;
                buf->data = LockedAlloc(buffer_cxt,BLCKSZ);
                buf->shadow = LockedAlloc(buffer_cxt,BLCKSZ);
                buf->locflags &= ~(BM_RETIRED);
                buf->freeNext = INVALID_DESCRIPTOR;
                if ( head == NULL ) {
                    head = buf;
                    tail = buf;
                } else {
                    tail->freeNext = i;
                    tail = buf;
                }
                Assert(buf->data != NULL);
            }
            pthread_mutex_unlock(&buf->cntx_lock.guard);
        }
        AddBuffersToTail(head);
        return activate;
    } else {
        BufferDesc* buf = NULL;
        int i=0;
        if ( buffer_cxt == NULL ) return 0;
        if ( count > MaxBuffers - NBuffers ) count = MaxBuffers - NBuffers;
        for (i=NBuffers;i<NBuffers+count;i++) {
            buf = &BufferDescriptors[i];
            pthread_mutex_lock(&buf->cntx_lock.guard);
            buf->locflags &= ~(BM_RETIRED);
            buf->data = LockedAlloc(buffer_cxt,BLCKSZ);
            buf->shadow = LockedAlloc(buffer_cxt,BLCKSZ);
            pthread_mutex_unlock(&buf->cntx_lock.guard);
            Assert(buf->data != NULL);
        }
        buf->freeNext = INVALID_DESCRIPTOR;
        AddBuffersToTail(&BufferDescriptors[NBuffers]);
        NBuffers += count;
        return count;
    }
}

int
RetireBuffers(int start, int count) {
    int i;
    if ( start >= NBuffers ) return 0;
    if ( buffer_cxt == NULL ) return 0;
    if ( start + count >= NBuffers ) count = NBuffers - start;
    BufferDesc* buf = &BufferDescriptors[start];
    
    for (i = start; i < count; buf++, i++) {
        pthread_mutex_lock(&buf->cntx_lock.guard);
        Assert ( buf->locflags & BM_DELETED ) ;
        Assert ( buf->refCount == 0 ) ;
        buf->locflags |= ( BM_RETIRED ) ;
        buf->locflags &= ~( BM_VALID ) ;
        if ( buf->data < BufferBlocks || buf->data > BufferBlocks + ( BLCKSZ * SBuffers ) ) pfree(buf->data);
        if ( buf->shadow < BufferBlocks || buf->shadow > BufferBlocks + ( BLCKSZ * SBuffers ) ) pfree(buf->shadow);
        buf->data = NULL;
        buf->shadow = NULL;
        pthread_mutex_unlock(&buf->cntx_lock.guard);
    }
    
    return count;
}

void
InitializeBuffers(int start, int count, char* block) {
    int i;
    BufferDesc* buf = &BufferDescriptors[start];
    
    SBuffers = NBuffers;
    
        for (i = start; i < start + count; i++)
        {
            buf = &BufferDescriptors[i];
            CLEAR_BUFFERTAG(&(buf->tag));
            buf->locflags = (BM_DELETED | BM_FREE);
            
            if ( i >= NBuffers ) {
                buf->locflags |= BM_RETIRED;
            } else {
                if ( block ) {
                    buf->data = block;
                    block += BLCKSZ;
                    buf->shadow = block;
                    block += BLCKSZ;
                } else {
                    buf->data = LockedAlloc(buffer_cxt,BLCKSZ);
                    buf->shadow = LockedAlloc(buffer_cxt,BLCKSZ);
                }
                Assert(buf->data != NULL);
            }

            buf->ioflags = 0;
            buf->refCount = 0;
            buf->pageaccess = 0;
            buf->freeNext = i+1;
            buf->buf_id = i;

            pthread_mutex_init(&buf->io_in_progress_lock.guard,&process_mutex_attr);
            pthread_cond_init(&buf->io_in_progress_lock.gate,&process_cond_attr);
            pthread_mutex_init(&(buf->cntx_lock.guard),&process_mutex_attr);
            pthread_cond_init(&(buf->cntx_lock.gate),&process_cond_attr);

            buf->r_locks = 0;		/* # of shared locks */

            buf->e_waiting = 0;
            buf->w_waiting = 0;
            buf->r_waiting = 0;
            buf->p_waiting = 0;

            buf->bias = 0;
        }
    
    buf->freeNext = INVALID_DESCRIPTOR;
}

/* -----------------------------------------------------
 * BufferShmemSize
 *
 * compute the size of shared memory for the buffer pool including
 * data pages, buffer descriptors, hash tables, etc.
 * ----------------------------------------------------
 */
size_t
BufferShmemSize()
{
	size_t			size = 0;
        char*           table_count;

        table_count = GetProperty("buffer_tables");
        if ( table_count != NULL ) {
            NTables = atoi(table_count);
            if ( NTables < 0 || NTables > 9 ) {
                NTables = 1;
            }
        }   
	/* size of shmem index hash table */
	size += hash_estimate_size(SHMEM_INDEX_SIZE,SHMEM_INDEX_ENTRYSIZE);

	/* size of buffer descriptors */
	size += MAXALIGN((MaxBuffers) * sizeof(BufferDesc));

	/* size of data pages */
	size += NBuffers * MAXALIGN(BLCKSZ);

        size += NTables * sizeof(BufferTable);
	/* size of buffer hash table */
        /*  2x b/c we are using 2 tables one for index and one for everything else  */
	size += hash_estimate_size(MaxBuffers,sizeof(BufferLookupEnt)) * NTables;

	return size;
}
