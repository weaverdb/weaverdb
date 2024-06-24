/*-------------------------------------------------------------------------
 *
 * delegatedscan.c
 *
 * IDENTIFICATION
 *
 *-------------------------------------------------------------------------
 */

#include <pthread.h>
#include <errno.h>



#include "postgres.h"

#include "env/env.h"
#include "executor/executor.h"
#include "env/dolhelper.h"
#include "access/heapam.h"
#include "env/delegatedscan.h"
#include "utils/mcxt.h"
#include "access/hio.h"


typedef enum CollectionState {
    COLLECTION_RUNNING,
    COLLECTION_WAITING,
    COLLECTION_SIGNALED,
/* the delegate is waiting for pickup before continuing */
    DELEGATE_WAITING,
    DELEGATE_SIGNALED,
    DELEGATE_RUNNING,
} CollectionState;

typedef struct DelegateData {
    pthread_mutex_t     guard;
    pthread_cond_t      gate;
    int                 size;
    CollectionState     collstate;
    CollectionState     delestate;
    ItemPointerData*    items;
    MemoryContext       cxt;
    void*		scan_args;
    bool                delegate_done;
    bool                collector_done;
    bool		collector_more;
} DelegateData;

typedef struct MarkerData {
    DelegateData        delegate;
    int                 size;
    int                 pointer;
    int                 total;
    ItemPointerData*    items;
} MarkerData;

typedef void* (*thread_func)(void*);

static int TransferMax = (16 * 1024);

static void CollectReadyForMore(Marker marker);
static bool CollectPointers(Marker marker);
static void CleanupDelegate(Delegate delegate);
static int DelegateWait(Delegate delegate,CollectionState collection);
static int DelegateSignal(Delegate delegate, CollectionState collection);
static int DelegateLock(Delegate delegate);
static int DelegateUnlock(Delegate delegate);
static int CountBlocks(ItemPointer pointer, int size);
static int compare_blocks(ItemPointer c1, ItemPointer c2);

typedef int (*comparator)(const void*,const void*);

Marker
DelegatedScanStart(void*(*scan_method)(Delegate), void* scan_args) {
        Marker marker = palloc(sizeof(MarkerData));
        Delegate delegate = &marker->delegate;

        marker->size = 0;
        marker->pointer = 0;
        marker->total = 0;
        marker->items = NULL;

        pthread_mutex_init(&delegate->guard,NULL);
        pthread_cond_init(&delegate->gate,NULL);
        delegate->size = 0;
        delegate->cxt = MemoryContextGetCurrentContext();
        delegate->items = NULL;
        delegate->collstate = COLLECTION_RUNNING;
        delegate->delestate = DELEGATE_RUNNING;
        delegate->scan_args = scan_args;
        delegate->collector_done = false;
        delegate->delegate_done = false;
	delegate->collector_more = false;

        DolConnection dol = GetDolConnection();
        
        ProcessDolCommand(dol,(thread_func)scan_method,delegate);

        return marker;
}

void*
DelegatedScanArgs(Delegate delegate) {	
	return delegate->scan_args;
}

bool
DelegatedScanNext(Marker marker, ItemPointer  ret_item) {
        
        bool more = true;

        if ( CheckForCancel() ) {
            elog(ERROR,"Query Cancelled");
        }

        if ( marker->pointer == marker->size ) {
            more = CollectPointers(marker);
        }
        
          /*        no net gain here need to refine  
        else if ( marker->size - marker->pointer < 2 ) {

		CollectReadyForMore(marker);
        
	}
 */
        if ( !more ) {
            return false;
        }
        
        Assert(marker->pointer < marker->size);
/*  no ++ in macro, can expand to be called more than once.  */        
        ItemPointerCopy(&marker->items[marker->pointer],ret_item);
        
        marker->pointer += 1;
        
        return true;
}

static void CollectReadyForMore(Marker marker) {
        Delegate delegate = &marker->delegate;
	
	DelegateLock(delegate);

	delegate->collector_more = true;

	DelegateUnlock(delegate);

}

void
DelegatedScanEnd(Marker marker) {
        Delegate delegate = &marker->delegate;

        CleanupDelegate(delegate);

        if ( marker->items != NULL ) pfree(marker->items);

        pthread_mutex_destroy(&delegate->guard);
        pthread_cond_destroy(&delegate->gate);

        pfree(marker);
}

void
DelegatedDone(Delegate delegate) {
    DelegateLock(delegate);

    while ( !delegate->collector_done ) {
        if ( delegate->size == 0 ) delegate->size = -1;
        if ( delegate->collstate == COLLECTION_WAITING ) {
            DelegateSignal(delegate,COLLECTION_SIGNALED);
        }
        DelegateWait(delegate,DELEGATE_WAITING);
    }

    if ( delegate->items ) {
        pfree(delegate->items);
        delegate->items = NULL;
    }

    delegate->delegate_done = true;

    if ( delegate->collstate == COLLECTION_WAITING ) {
        DelegateSignal(delegate,COLLECTION_SIGNALED);
    }

    DelegateUnlock(delegate);
}

bool 
DelegatedTransferPointers(Delegate delegate,ItemPointer items,int size) {
    bool cont = true;

    if ( CheckForCancel() ) {
        elog(ERROR,"Query Cancelled");
    }
    
    DelegateLock(delegate);
   
    if ( delegate->collector_done ) {
        DelegateUnlock(delegate);
        return false;
    }

    while ( delegate->size + size > DelegatedGetTransferMax()  ) {
        DelegateWait(delegate,DELEGATE_WAITING);
    }

    ItemPointerData* move = palloc(sizeof(ItemPointerData) * (1 + size + delegate->size));
    if ( delegate->items != NULL ) {
        memmove(move,delegate->items,sizeof(ItemPointerData) * delegate->size);
        pfree(delegate->items);
    }
    memmove((move + delegate->size),items,size * sizeof(ItemPointerData));

    delegate->items = move;
    delegate->size += size;

    if ( delegate->collstate == COLLECTION_WAITING ) {
        DelegateSignal(delegate, COLLECTION_SIGNALED);
    }

    DelegateUnlock(delegate);
/*
    DTRACE_PROBE2(mtpg,count,size,CountBlocks(items,size));
*/
    return cont;
}

bool DelegatedCollectorWaiting(Delegate delegate) {
/*  try doing this without mutex locks */
	bool iswaiting = false;
	if ( !pthread_mutex_trylock(&delegate->guard) ) {
		iswaiting =(delegate->collector_more || (delegate->collstate == COLLECTION_WAITING));
		pthread_mutex_unlock(&delegate->guard);
	} else {
		return false; 
	}
    return iswaiting;
}

/*  needs to be called from main thread  */
static void 
CleanupDelegate(Delegate delegate) {
    DelegateLock(delegate);
    
    delegate->collector_done = true;

    if ( delegate->delestate == DELEGATE_WAITING ) {
        DelegateSignal(delegate, DELEGATE_SIGNALED);
    }

    while ( !delegate->delegate_done ) {
        DelegateWait(delegate, COLLECTION_WAITING);
    }

    DelegateUnlock(delegate);
}

static bool 
CollectPointers(Marker marker) {

    Delegate delegate = &marker->delegate;
    int code = CheckDolHelperErrors();

    if ( code != 0 ) {
        char*        msg = palloc(256);
        char*        state= palloc(40);

        GetDolHelperErrorMessage(state,msg);
        elog(ERROR,msg,code);
    }

    DelegateLock(delegate);

    if ( marker->items != NULL ) {
        pfree(marker->items);
        marker->items = NULL;
    }

    if ( delegate->delestate == DELEGATE_WAITING ) {
/*  
    the delegate is waiting for the collection to be picked up b/c 
    the main thread should not get too far behind the delegate
*/
        DelegateSignal(delegate, DELEGATE_SIGNALED);
    }

    while ( delegate->size == 0 ) {
        DelegateWait(delegate,COLLECTION_WAITING);
    }

    if ( delegate->size < 0 ) {
/*  we are done  return false;*/
        marker->size = 0;
        marker->pointer = 0;
        DelegateUnlock(delegate);
        return false;
    }

    marker->size = delegate->size;
    marker->total += delegate->size;

    marker->items = MemoryContextAlloc(delegate->cxt,sizeof(ItemPointerData) * delegate->size);
    memmove(marker->items,delegate->items,sizeof(ItemPointerData) * delegate->size);
    
    marker->pointer = 0;
    delegate->size = 0;
    delegate->collector_more = false;

    if ( delegate->delestate == DELEGATE_WAITING ) {
        DelegateSignal(delegate,DELEGATE_SIGNALED);
    }

    DelegateUnlock(delegate);
    
    
    return true;
}

static int 
DelegateLock(Delegate delegate) {
    return pthread_mutex_lock(&delegate->guard);
}


static int 
DelegateUnlock(Delegate delegate) {
    return pthread_mutex_unlock(&delegate->guard);
}

static int 
DelegateSignal(Delegate delegate, CollectionState state) {
     switch ( state  ) {
        case COLLECTION_SIGNALED:
            delegate->collstate = COLLECTION_SIGNALED;
            break;
        case DELEGATE_SIGNALED:
            delegate->delestate = DELEGATE_SIGNALED;
            break;
        default:
            break;
    }
    return pthread_cond_signal(&delegate->gate);
}

static int 
DelegateWait(Delegate delegate, CollectionState state) {
    struct timespec waittime;
    long timeout = 4000;
    
     switch ( state  ) {
        case COLLECTION_WAITING:
            delegate->collstate = COLLECTION_WAITING;
            break;
        case DELEGATE_WAITING:
            delegate->delestate = DELEGATE_WAITING;
            break;
        default:
            elog(FATAL,"unknown delegate state");
    }
     
     ptimeout(&waittime,timeout);
                
        while ( pthread_cond_timedwait(&delegate->gate,&delegate->guard, &waittime) == ETIMEDOUT ) {
            if ( CheckForCancel() ) {
                DelegateUnlock(delegate);
                elog(ERROR,"Query Cancelled");
            }
            if ( state == COLLECTION_WAITING && delegate->collstate == COLLECTION_SIGNALED ) {
                break;
            }
            if ( state == DELEGATE_WAITING && delegate->delestate == DELEGATE_SIGNALED ) {
                break;
            }
            waittime.tv_sec = time(NULL) + (timeout / 1000);
            waittime.tv_nsec = (timeout % 1000) * 1000000;
        }


    switch ( state  ) {
        case COLLECTION_WAITING:
            delegate->collstate = COLLECTION_RUNNING;
            break;
        case DELEGATE_WAITING:
            delegate->delestate = DELEGATE_RUNNING;
            break;
        default:
            elog(FATAL,"unknown delegate state");
    }

    return 0;
}

bool
DelegatedGetTuple(Marker marker, Relation rel, Snapshot time, TupleTableSlot* slot, ItemPointer pointer, Buffer * buffer)
{
	int             result;
	TransactionId   xid;
	int             len = 0;
	HeapTupleData   tp;

	tp.t_self = *pointer;
        tp.t_info = 0;
	*buffer = RelationGetHeapTupleWithBuffer(rel, &tp, *buffer);
	if ( !BufferIsValid(*buffer) ) return false;

	if ( time != NULL ) {
		bool valid = FALSE;
		LockHeapTuple(rel, *buffer, &tp, TUPLE_LOCK_READ);
		valid = HeapTupleSatisfies(rel,*buffer,&tp,time,0,NULL);
		LockHeapTuple(rel, *buffer, &tp, TUPLE_LOCK_UNLOCK);
		if ( !valid ) return FALSE;
	}

	ExecStoreTuple(&tp,slot,false);  /*  this is a direct page so do not transfer */	        

	return true;
}

static int CountBlocks(ItemPointer items, int size) {
    int x = 0;
    BlockNumber current = InvalidBlockNumber;
    int count = 0;
    
    qsort(items,size,sizeof(ItemPointerData),(comparator)compare_blocks);
    for (x=0;x<size;x++) {
        if ( ItemPointerGetBlockNumber(items + x) != current ) {
            current = ItemPointerGetBlockNumber(items + x);
            count++;
        }
    }
    return count;
}


int compare_blocks(ItemPointer c1, ItemPointer c2) {
    BlockNumber b1 = ItemPointerGetBlockNumber(c1);
    BlockNumber b2 = ItemPointerGetBlockNumber(c2);

    if ( b1 == b2 ) return 0;
    else if ( b1 < b2 ) return -1;
    else return 1;
}

int 
DelegatedGetTransferMax() {
	return TransferMax;
}

void
DelegatedSetTransferMax(int max) {
	TransferMax = max;
}

