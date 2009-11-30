/*-------------------------------------------------------------------------
 *
 * freelist.c
 *	  routines for manipulating the buffer pool's replacement strategy
 *	  freelist.
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  $Header: /cvs/weaver/mtpgsql/src/backend/storage/buffer/freelist.c,v 1.3 2007/05/23 15:39:23 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */
/*
 * OLD COMMENTS
 *
 * Data Structures:
 *		SharedFreeList is a circular queue.  Notice that this
 *		is a shared memory queue so the next/prev "ptrs" are
 *		buffer ids, not addresses.
 *
 * Sync: all routines in this file assume that the buffer
 *		semaphore has been acquired by the caller.
 */

#include "postgres.h"

#include "env/env.h"

#include <signal.h>
#include <sys/sdt.h>

#include "env/dbwriter.h"
#ifdef SPIN_IS_MUTEX
#include "storage/m_lock.h"
#else
#include "storage/s_lock.h"
#endif

#include "storage/bufmgr.h"


extern SPINLOCK FreeBufMgrLock;
extern SLock *SLockArray;


static BufferDesc *SharedFreeList;


typedef struct {
    int                 head;
    int                 tail;
    int 		last;
    pthread_mutex_t     guard;
    
} FreeList;

typedef struct flush_manager {
    bool                flushing;
    pthread_cond_t      flush_wait;
    pthread_mutex_t     flush_gate;
    bool                waiting[MAXBACKENDS];
} FlushManager;


static FreeList MasterList;

static FreeList IndexList;

static FlushManager  FlushBlock;

float     IndexBufferReserve = 0.25;
static	 int 		split = 0;
bool        lingeringbuffers = false;

static BufferDesc* RemoveNearestNeighbor(BufferDesc *bf);

static void SetTailBuffer(BufferDesc* buf);
static BufferDesc* GetHead(Relation rel);
static void SetHead(BufferDesc* buf);
static bool InitiateFlush();

static BufferDesc* GetHead(Relation rel) {
    BufferDesc*  head = NULL;
    
    FreeList* which = ( rel->rd_rel->relkind == RELKIND_INDEX ) ? &IndexList : &MasterList;
    
    pthread_mutex_lock(&which->guard);
    /*  need a valid buffer from the list */
    while ( which->head == INVALID_DESCRIPTOR  ) {
        FreeList* oplist = ( which == &IndexList ) ? &MasterList : &IndexList;

        pthread_mutex_unlock(&which->guard);
        pthread_mutex_lock(&oplist->guard);

        if ( oplist->head == INVALID_DESCRIPTOR ) {
            pthread_mutex_unlock(&oplist->guard);
            InitiateFlush();
            /*  going back to original list so lock it  */
            pthread_mutex_lock(&which->guard);
        } else {
            /*  we are going to pull the opposite list so switch to it  */
            which = oplist;
            DTRACE_PROBE2(mtpg, buffer__freesteal, rel->rd_rel->relkind, split);
        }
    }

    Assert(which->head >= 0 && which->head < NBuffers);
    head = &BufferDescriptors[which->head];

    pthread_mutex_lock(&head->cntx_lock.guard);

    Assert((head->locflags & BM_FREE));
    which->head = head->freeNext;
    head->locflags &= ~(BM_FREE);
    head->freeNext = DETACHED_DESCRIPTOR;
    which->last = head->buf_id;
    /*  if the head equals the tail, make sure the tail is set to invalid */
    if ( which->head == which->tail ) {
        which->tail = INVALID_DESCRIPTOR;
    }

    pthread_mutex_unlock(&head->cntx_lock.guard);
    /* we could return this locked and save a unlock/lock cycle 
     * but for cleanliness, just unlock the head and return 
     * it and let the consumer relock it 
     */
    
    pthread_mutex_unlock(&which->guard);

    return head;
}

static void SetHead(BufferDesc* buf) {
    BufferDesc*  head = NULL;
    FreeList* which = ( buf->kind == RELKIND_INDEX ) ? &IndexList : &MasterList;
    pthread_mutex_lock(&which->guard);
    pthread_mutex_lock(&buf->cntx_lock.guard);
    /*  if the tail is invalid push the old head to the tail  */
  /*  BM_FREE signals intention to add to list and should already be set  */
    Assert((buf->locflags & BM_FREE));
    Assert(buf->freeNext == DETACHED_DESCRIPTOR);
    /* if the tail is invalid, the current head becomes the tail  */
    if ( which->tail == INVALID_DESCRIPTOR ) {
        which->tail = which->head;
    }
    Assert(buf->buf_id != which->head);
    buf->freeNext = which->head;
    which->head = buf->buf_id;
    pthread_mutex_unlock(&buf->cntx_lock.guard);
    pthread_mutex_unlock(&which->guard);
}

static void SetTailBuffer(BufferDesc* buf) {
    BufferDesc*  tail = NULL;
    Buffer       origtail = InvalidBuffer;
    
    FreeList* which = ( buf->kind == RELKIND_INDEX ) ? &IndexList : &MasterList;
    pthread_mutex_lock(&which->guard);
    
    /* must deal with special cases of when the head
     * or the tail is invalid
     **/
    if ( which->head == INVALID_DESCRIPTOR ) {
        tail = NULL;
    } else if ( which->tail == INVALID_DESCRIPTOR ) {
        tail = &BufferDescriptors[which->head];
    } else {
        tail = &BufferDescriptors[which->tail];
    }
    
    if ( tail != NULL ) {
        pthread_mutex_lock(&tail->cntx_lock.guard);
    }
    
    pthread_mutex_lock(&buf->cntx_lock.guard);
    Assert(buf->freeNext == DETACHED_DESCRIPTOR);
  /*  BM_FREE signals intention to add to list and should already be set  */
    Assert((buf->locflags & BM_FREE));
    buf->freeNext = INVALID_DESCRIPTOR;
    pthread_mutex_unlock(&buf->cntx_lock.guard);   
    
    if (tail == NULL) {
        which->head = buf->buf_id;
        which->tail = INVALID_DESCRIPTOR;
    } else {
        Assert((tail->locflags & BM_FREE));
        Assert((tail->freeNext == INVALID_DESCRIPTOR));
        tail->freeNext = buf->buf_id;
        which->tail = buf->buf_id;
        pthread_mutex_unlock(&tail->cntx_lock.guard);
    }
    
    pthread_mutex_unlock(&which->guard);
}

static bool InitiateFlush() {
    bool iflushed = false;
    pthread_mutex_lock(&FlushBlock.flush_gate);
    if ( IsDBWriter() ) {
        FlushAllDirtyBuffers();
    } else if ( FlushBlock.flushing ) {
        FlushBlock.waiting[GetEnv()->eid] = TRUE;
        pthread_cond_wait(&FlushBlock.flush_wait, &FlushBlock.flush_gate);
        FlushBlock.waiting[GetEnv()->eid] = FALSE;
    } else {
        FlushBlock.flushing = true;
        iflushed = true;
        FlushBlock.waiting[GetEnv()->eid] = TRUE;
        pthread_mutex_unlock(&FlushBlock.flush_gate);
        FlushAllDirtyBuffers();
        pthread_mutex_lock(&FlushBlock.flush_gate);
        FlushBlock.waiting[GetEnv()->eid] = FALSE;
        FlushBlock.flushing = false;
        pthread_cond_broadcast(&FlushBlock.flush_wait);
    }
    pthread_mutex_unlock(&FlushBlock.flush_gate);
    return iflushed;
}

static BufferDesc* RemoveNearestNeighbor(BufferDesc *bf) {
    /*  already holding a lock on the passed in buffer */
    BufferDesc* next = NULL;
    
    while ( bf->freeNext != INVALID_DESCRIPTOR ) {
        /* can't be the head b/c the passed in is before us, don't worry about that special case  */
        next = &BufferDescriptors[bf->freeNext];
        if ( !pthread_mutex_trylock(&next->cntx_lock.guard) ) {
            bool leave = false;
            bool tail = false;
            
            if ( next->freeNext == INVALID_DESCRIPTOR ) {
                /* ok this is the tail, instead of trying to remove it just fault out  */
                leave = true;
            } else if ( next->refCount > 0 ) {
                bf->freeNext = next->freeNext;
                next->freeNext = DETACHED_DESCRIPTOR;
                next->locflags &= ~(BM_FREE);
            } else if ( lingeringbuffers && next->used ) {
                bf->freeNext = next->freeNext;
                next->freeNext = DETACHED_DESCRIPTOR;
         /*  we are in the freelist and going to be added back to the end  */
                Assert(next->locflags & BM_FREE);
                next->used = false;
                tail = true;
            } else {
                leave = true;
            }
            pthread_mutex_unlock(&next->cntx_lock.guard);
            
            if ( leave ) return NULL;
            if ( tail ) return next;
        } else {
            return NULL;
        }
    }
    
    return NULL;
}

int BiasPinned(BufferDesc* buf) {
    pthread_mutex_lock(&buf->cntx_lock.guard);
    
    buf->bias++;
    
    pthread_mutex_unlock(&buf->cntx_lock.guard);
}



int ManualPin(BufferDesc* buf, bool pageaccess) {
    bool  remove = false;
    int valid = 0;
    BufferDesc*  tail = NULL;
    pthread_mutex_lock(&buf->cntx_lock.guard);
    /*  if we are doing a pageaccess ( not the dbwriter ) and there is an e_lock
     *  then wait for the e_lock to be released, do this before
     *  valid check because the buffer could no longer be valid
     *  by the time the release gets to us.
     */
    while ( pageaccess && buf->e_lock ) {
        buf->p_waiting++;
        pthread_cond_wait(&buf->cntx_lock.gate, &buf->cntx_lock.guard);
        buf->p_waiting--;
    }
    if ( buf->locflags & BM_VALID ) {
        if ( pageaccess ) buf->pageaccess++;
        if ( buf->refCount++ == 0 ) {
            buf->used = true;
        }
        if (buf->locflags & BM_FREE) {
            /*  pin just sets the ref count and if it happens to be in the free list
             *  removes it's neighbor if it's not in the freelist
             */
            tail = RemoveNearestNeighbor(buf);
        }
        valid = 1;
    } else {
        valid = 0;
    }
    
    pthread_mutex_unlock(&buf->cntx_lock.guard);
    /*
     * here the used flag on the buffer caused the removal from the freelist
     * so just reset the used flag and add it back to the end */
    if ( tail != NULL ) {
        SetTailBuffer(tail);
    }
    return valid;
}

int ManualUnpin(BufferDesc* buf, bool pageaccess) {
    bool  add = false;
    pthread_mutex_lock(&buf->cntx_lock.guard);
    if ( buf->refCount == 0 ) {
        elog(DEBUG, "unpinning refcount 0");
    }

    buf->refCount--;
    if ( pageaccess ) buf->pageaccess--;
    if (buf->e_waiting > 0 && buf->pageaccess == buf->e_waiting) {
        pthread_cond_signal(&buf->cntx_lock.gate);
    }
    
    if( buf->refCount == 0 ) {
        if ( buf->locflags & BM_FREE ) {
            DTRACE_PROBE1(mtpg, buffer__doublefree, buf->buf_id);
        } else {
            buf->used = false;
            add = true;
            buf->locflags |= BM_FREE;
        }
    }

    pthread_mutex_unlock(&buf->cntx_lock.guard);
    
    if ( add ) SetTailBuffer(buf);
    
    return ( add ) ? 1: 0;
}

void PutFreeBuffer(BufferDesc* bufHdr) {
    bool put = true;
    pthread_mutex_lock(&bufHdr->cntx_lock.guard);
    
    Assert((bufHdr->refCount == 1 && bufHdr->pageaccess == 1));
    if (!(bufHdr->locflags & BM_FREE)) {
        bufHdr->used = false;
        bufHdr->refCount = 0;
        bufHdr->pageaccess = 0;
        bufHdr->locflags |= BM_FREE;
    } else {
 /*  already back in the freelist, this can happen using lingering buffers where
     the SetTail did not have time to */
        put = false;
    }
    
    pthread_mutex_unlock(&bufHdr->cntx_lock.guard);
    
    if ( put ) SetHead(bufHdr);
}

/*
 * GetFreeBuffer() -- get the 'next' buffer from the freelist.
 *
 */
BufferDesc * GetFreeBuffer(Relation rel) {
    BufferDesc*  head = NULL;
    bool valid = false;
    
    while ( !valid )  {
        
        bool tail = false;
        
        head = GetHead(rel);
        /*  could save a release/lock and return the buffer locked from GetHead but don't worry about that now */
        pthread_mutex_lock(&head->cntx_lock.guard);        
        if ( head->refCount > 0 ) {
            /* this buffer is no longer free
             * set the proper flags and set head to null to
             * go around again */
            /* done in get head
             * head->locflags &= ~(BM_FREE);
             * head->freeNext = DETACHED_DESCRIPTOR;
             */
            head->used = false;
            valid = false;
        } else if ( head->bias > 0 ) {
            /*  this one is baised to not be used unless
             * absolutely nessessary reduce bias and set tail to true
             * which adds it to the end of the list */
            head->bias -= 1;
            head->used = false;
            valid = false;
            if ( !(head->locflags & BM_FREE) ) {
                head->locflags |= BM_FREE;
                tail = true;
            }
        } else {
            /*  the candidate is good, release it and return it  */
            /*  the buffer is no longer in the freelist and
             * technically no longer valid b/c we are about to replace
             * the contents with a different buffer
             */
            Assert(head->refCount == 0);
            Assert(head->pageaccess == 0);
 
            head->locflags &= ~(BM_VALID);
            head->refCount = 1;
            head->pageaccess = 1;
            head->used = false;
            valid = true;
        }
        pthread_mutex_unlock(&head->cntx_lock.guard);
        
        if ( tail ) {
            SetTailBuffer(head);
        }
 
    }
    
    return head;
}

/*
 * InitFreeList -- initialize the dummy buffer descriptor used
 *		as a freelist head.
 *
 * Assume: All of the buffers are already linked in a circular
 *		queue.	 Only called by postmaster and only during
 *		initialization.
 */
void InitFreeList(bool init) {
    int count = 0;
    SharedFreeList = &(BufferDescriptors[Free_List_Descriptor]);
    
    if (init) {
        split = NBuffers * IndexBufferReserve;
        
        pthread_mutex_init(&FlushBlock.flush_gate, &process_mutex_attr);
        pthread_cond_init(&FlushBlock.flush_wait, &process_cond_attr);
        FlushBlock.flushing = false;
        memset(FlushBlock.waiting,0x00,sizeof(bool) * MAXBACKENDS);
        
        pthread_mutex_init(&IndexList.guard, &process_mutex_attr);
        IndexList.head = 0;
        IndexList.tail = split - 1;
        IndexList.last = 0;
        
        pthread_mutex_init(&MasterList.guard, &process_mutex_attr);
        MasterList.head = split;
        MasterList.tail = NBuffers - 1;
        MasterList.last = 0;
        
        for (count=0;count<NBuffers;count++) {
            BufferDesc*  buf = &BufferDescriptors[count];
            buf->freeNext = count+1;
            buf->locflags |= BM_FREE;
        }
        
        BufferDescriptors[split - 1].freeNext = INVALID_DESCRIPTOR;
        BufferDescriptors[NBuffers - 1].freeNext = INVALID_DESCRIPTOR;
    }
    
}

bool IsWaitingForFlush(unsigned owner) {
    bool iswaiting = FALSE;
        pthread_mutex_lock(&FlushBlock.flush_gate);
        iswaiting = FlushBlock.waiting[owner];
        pthread_mutex_unlock(&FlushBlock.flush_gate);     
        return iswaiting;
}

