/*-------------------------------------------------------------------------
 *
 * freelist.c
 *	  routines for manipulating the buffer pool's replacement strategy
 *	  freelist.
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
#include <signal.h>
#include <errno.h>
#include <sys/time.h>

#include "postgres.h"
#include "env/env.h"

#include "env/dbwriter.h"
#ifdef SPIN_IS_MUTEX
#include "storage/m_lock.h"
#else
#include "storage/s_lock.h"
#endif
#include "env/properties.h"
#include "storage/bufmgr.h"


typedef struct {
    int                 head;
    int                 tail;
    int 		last;
    int                 waiting;
    pthread_mutex_t     guard;
    pthread_cond_t     gate;
} FreeList;

typedef struct flush_manager {
    bool                flushing;
    pthread_cond_t      flush_wait;
    pthread_mutex_t     flush_gate;
    int                 flush_count;
    long                flush_time;
} FlushManager;


static FreeList* MasterList;

static FreeList* IndexList;

static FlushManager  FlushBlock;

static float     IndexBufferReserve = 0.0;
static	 int 		split = 0;
static bool        lingeringbuffers = false;

static BufferDesc* RemoveNearestNeighbor(BufferDesc *bf);

static void SetTailBuffer(BufferDesc* buf);
static BufferDesc* GetHead(Relation rel);
static void SetHead(BufferDesc* buf);
static long InitiateFlush(void);
static int buffer_wait = 400;
static float addscale = .10;

static BufferDesc* GetHead(Relation rel) {
    BufferDesc*  head = NULL;
    long         longwait = 0;
    int timerr = 0;
    long elapsed;
    struct timespec t1,t2;
   
    clock_gettime(WHICH_CLOCK,&t1);
 /*   
    FreeList* which = ( rel->rd_rel->relkind == RELKIND_INDEX && IndexList != NULL ) ? IndexList : MasterList;
*/
    FreeList* which = MasterList;
    
    pthread_mutex_lock(&which->guard);
    /*  need a valid buffer from the list */
    while ( which->head == INVALID_DESCRIPTOR  ) {
        FreeList* oplist = ( which == IndexList || IndexList == NULL ) ? MasterList : IndexList;

        if ( which != oplist ) {
                pthread_mutex_unlock(&which->guard);
                pthread_mutex_lock(&oplist->guard);
        }

        if ( oplist->head == INVALID_DESCRIPTOR ) {
                oplist->waiting++;
                struct timespec waittime;
                
                ptimeout(&waittime,(buffer_wait + longwait));
                timerr = pthread_cond_timedwait(&oplist->gate, &oplist->guard, &waittime);
                oplist->waiting--;
                if (timerr == ETIMEDOUT) {
                    pthread_mutex_unlock(&oplist->guard);
                    longwait = InitiateFlush();
                    /*  going back to original list so lock it  */
                    pthread_mutex_lock(&which->guard);
                } else {
                    which = oplist;
                }
        } else {
            /*  we are going to pull the opposite list so switch to it  */
            which = oplist;
            DTRACE_PROBE2(mtpg, buffer__freesteal, rel->rd_rel->relkind, split);
        }
    }
    
    Assert(which->head >= 0 && which->head < MaxBuffers);
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
    clock_gettime(WHICH_CLOCK,&t2);

    elapsed = (t2.tv_sec - t1.tv_sec) * 1000;      // sec to ms
    elapsed += (t2.tv_nsec - t1.tv_nsec) / 1000000;   // ns to ms
    
    DTRACE_PROBE1(mtpg,buffer__freetime,elapsed);
    
    return head;
}

static void SetHead(BufferDesc* buf) {
    FreeList* which = ( buf->kind == RELKIND_INDEX && IndexList != NULL ) ? IndexList : MasterList;
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

void AddBuffersToTail(BufferDesc* buf) {
    BufferDesc*  tail = NULL;
    FreeList* which = MasterList;
    
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
     
    if (tail == NULL) {
        which->head = buf->buf_id;
    } else {
        pthread_mutex_lock(&tail->cntx_lock.guard);
        Assert((tail->locflags & BM_FREE));
        Assert((tail->freeNext == INVALID_DESCRIPTOR));
        tail->freeNext = buf->buf_id;
        pthread_mutex_unlock(&tail->cntx_lock.guard);
    }
    
    while ( buf->freeNext != INVALID_DESCRIPTOR ) {
        buf = &BufferDescriptors[buf->freeNext];
    }
    
    which->tail = buf->buf_id;
    if ( which->waiting ) pthread_cond_broadcast(&which->gate);
    pthread_mutex_unlock(&which->guard);
}

static void SetTailBuffer(BufferDesc* buf) {
    BufferDesc*  tail = NULL;
    
    DTRACE_PROBE3(mtpg,buffer__store,buf->blind.dbname,buf->blind.relname,buf->tag.blockNum);
    FreeList* which = ( buf->kind == RELKIND_INDEX && IndexList != NULL ) ? IndexList : MasterList;
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
     
    if (tail == NULL) {
        which->head = buf->buf_id;
        which->tail = INVALID_DESCRIPTOR;
    } else {
        pthread_mutex_lock(&tail->cntx_lock.guard);
        Assert((tail->locflags & BM_FREE));
        Assert((buf->freeNext == INVALID_DESCRIPTOR));
        Assert((tail->freeNext == INVALID_DESCRIPTOR));
        tail->freeNext = buf->buf_id;
        which->tail = buf->buf_id;
        pthread_mutex_unlock(&tail->cntx_lock.guard);
    }
    if ( which->waiting ) pthread_cond_broadcast(&which->gate);
    pthread_mutex_unlock(&which->guard);
}

static long InitiateFlush() {
    bool iflushed = false;
    pthread_mutex_lock(&FlushBlock.flush_gate);
    if ( IsDBWriter() ) {
        FlushAllDirtyBuffers(false);
    } else if ( FlushBlock.flushing ) {

    } else {
        FlushBlock.flushing = true;
        pthread_mutex_unlock(&FlushBlock.flush_gate);
        iflushed = FlushAllDirtyBuffers(false);
        pthread_mutex_lock(&FlushBlock.flush_gate);
        FlushBlock.flushing = false;
        pthread_cond_broadcast(&FlushBlock.flush_wait);
        if ( iflushed ) {
            if ( FlushBlock.flush_count++ > 0 && NBuffers < MaxBuffers ) {
                AddMoreBuffers(NBuffers * addscale);
                FlushBlock.flush_count = 0;
            }
        }
    }
    pthread_mutex_unlock(&FlushBlock.flush_gate);
    return GetFlushTime();
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
                next->locflags &= ~(BM_FREE | BM_USED);
            } else if ( lingeringbuffers && (next->locflags & BM_USED) ) {
                bf->freeNext = next->freeNext;
                next->freeNext = INVALID_DESCRIPTOR;
         /*  we are in the freelist and going to be added back to the end  */
                Assert(next->locflags & BM_FREE);
                next->locflags &= ~(BM_USED);
                tail = true;
            } else {
                leave = true;
            }
            pthread_mutex_unlock(&next->cntx_lock.guard);
            
            if ( leave ) return NULL;
            if ( tail ) {
                return next;
            }
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

    return buf->bias;
}



int ManualPin(BufferDesc* buf, bool pageaccess) {
    int valid = 0;
    BufferDesc*  tail = NULL;
    
    
    pthread_mutex_lock(&buf->cntx_lock.guard);

    if ( buf->locflags & BM_RETIRED ) {
        pthread_mutex_unlock(&buf->cntx_lock.guard);
        return 0;
    }
    /*  if we are doing a pageaccess ( not the dbwriter ) and there is an e_lock
     *  then wait for the e_lock to be released, do this before
     *  valid check because the buffer could no longer be valid
     *  by the time the release gets to us.
     */
    while ( pageaccess && (buf->locflags & BM_EXCLUSIVE) ) {
        buf->p_waiting++;
        pthread_cond_wait(&buf->cntx_lock.gate, &buf->cntx_lock.guard);
        buf->p_waiting--;
    }
    if ( buf->locflags & BM_VALID ) {
        if ( pageaccess ) buf->pageaccess++;
        if ( buf->refCount++ == 0 ) {
            buf->locflags |= BM_USED;
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
            buf->locflags &= ~(BM_USED);
            add = true;
            buf->locflags |= BM_FREE;
            Assert(buf->freeNext == DETACHED_DESCRIPTOR);
            buf->freeNext = INVALID_DESCRIPTOR;
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
        bufHdr->locflags &= ~(BM_USED);
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
            head->locflags &= ~(BM_USED);
            valid = false;
        } else if ( head->bias > 0 ) {
            /*  this one is baised to not be used unless
             * absolutely nessessary reduce bias and set tail to true
             * which adds it to the end of the list */
            head->bias -= 1;
            head->locflags &= ~(BM_USED);
            valid = false;
            if ( !(head->locflags & BM_FREE) ) {
                head->locflags |= BM_FREE;
                head->freeNext = INVALID_DESCRIPTOR;
                tail = true;
            }
        } else if ( lingeringbuffers && (head->locflags & BM_USED) ) {
            head->locflags &= ~(BM_USED);
            valid = false;
            if ( !(head->locflags & BM_FREE) ) {
                head->locflags |= BM_FREE;
                head->freeNext = INVALID_DESCRIPTOR;
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
            head->locflags &= ~(BM_USED);
            valid = true;
        }
        pthread_mutex_unlock(&head->cntx_lock.guard);
        
        if ( tail ) {
            SetTailBuffer(head);
        }
 
    }
    DTRACE_PROBE3(mtpg,buffer__evict,head->blind.dbname,head->blind.relname,head->tag.blockNum);
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
    
    double reserve = ( PropertyIsValid("index_buffer_reserve") ) ?
        GetFloatProperty("index_buffer_reserve")
    :
        IndexBufferReserve;
    
    if ( PropertyIsValid("buffer_scale")) {
        addscale = GetFloatProperty("buffer_scale");
    }
    
    lingeringbuffers = GetBoolProperty("lingering_buffers");
    if ( !lingeringbuffers ) {
        /* backward compatible */
        lingeringbuffers = GetBoolProperty("lingeringbuffers");
    }
    
    if ( PropertyIsValid("buffer_wait")) {
        buffer_wait = GetIntProperty("buffer_wait");
    }
            
    if (init) {
        split = NBuffers * reserve;
        
        pthread_mutex_init(&FlushBlock.flush_gate, &process_mutex_attr);
        pthread_cond_init(&FlushBlock.flush_wait, &process_cond_attr);
        FlushBlock.flushing = false;
        
        if ( split != 0 ) {
            IndexList = os_malloc(sizeof(FreeList));
            pthread_mutex_init(&IndexList->guard, &process_mutex_attr);
            pthread_cond_init(&IndexList->gate, &process_cond_attr);
            IndexList->head = 0;
            IndexList->tail = split - 1;
            IndexList->last = 0;
            IndexList->waiting = 0;
        } else {
            IndexList = NULL;
        }
        
        MasterList = os_malloc(sizeof(FreeList));
        pthread_mutex_init(&MasterList->guard, &process_mutex_attr);
        pthread_cond_init(&MasterList->gate, &process_cond_attr);
        MasterList->head = split;
        MasterList->tail = NBuffers - 1;
        MasterList->last = 0;
        MasterList->waiting = 0;
        
        for (count=0;count<NBuffers;count++) {
            BufferDesc*  buf = &BufferDescriptors[count];
            buf->freeNext = count+1;
            buf->locflags |= BM_FREE;
        }
        
        if ( split != 0 ) BufferDescriptors[split - 1].freeNext = INVALID_DESCRIPTOR;
        BufferDescriptors[NBuffers - 1].freeNext = INVALID_DESCRIPTOR;
    }
    
}

