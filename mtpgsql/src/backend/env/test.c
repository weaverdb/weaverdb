
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <umem.h>

typedef long BlockIdData;
typedef short OffsetNumber;
typedef char  bool;

#define ItemPointerCopy(fromPointer, toPointer) \
( \
	*(toPointer) = *(fromPointer) \
)


typedef struct ItemIdData
{								/* line pointers */
	unsigned int	lp_off;		/* offset to find tup */
	/* can be reduced by 2 if necc. */
	unsigned short	lp_len;		/* length of tuple */
	unsigned char	lp_flags;		/* flags on tuple */
	unsigned char	lp_overflow;		/* flags on tuple */
} ItemIdData;

typedef struct PageHeaderData
{
    	long			checksum;
	int  pd_lower;		/* offset to start of free space */
	int pd_upper;		/* offset to end of free space */
	int pd_special;	/* offset to start of special space */
	int	pd_opaque;	/* AM-generic information */
	ItemIdData	pd_linp[1];		/* beginning of line pointer array */
} PageHeaderData;

typedef union ItemPointerData
{
	BlockIdData ip_blkid;
	OffsetNumber ip_posid;
} ItemPointerData;

typedef enum CollectionState {
    COLLECTION_RUNNING,
    COLLECTION_WAITING,
    COLLECTION_SIGNALED,
/* the delegate is waiting for pickup before continuing */
    DELEGATE_WAITING,
    DELEGATE_SIGNALED,
    DELEGATE_RUNNING
} CollectionState;

typedef struct DelegateData {
    pthread_mutex_t     guard;
    pthread_cond_t      gate;
    int                 size;
    CollectionState     collstate;
    CollectionState     delestate;
    ItemPointerData*    items;
    void*               cxt;
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

typedef MarkerData* Marker;
typedef ItemPointerData* ItemPointer;

#define true  0x01;
#define false 0x00;

bool
DelegatedScanNext(Marker marker, ItemPointer  ret_item) {
        
        int save = marker->pointer;
        bool more = true;
 /*
        if ( CheckForCancel() ) {
            elog(ERROR,"Query Cancelled");
        }

        if ( marker->pointer == marker->size ) {
            more = CollectPointers(marker);
        }
        
                 no net gain here need to refine  
        else if ( marker->size - marker->pointer < 2 ) {

		CollectReadyForMore(marker);
        
	}
 */
        if ( !more ) {
            return false;
        }
/*         
        Assert(marker->pointer < marker->size);
*/         
        ItemPointerCopy(&marker->items[marker->pointer++],ret_item);
/*        
        Assert(marker->pointer == save + 1);
*/        
        if ( marker->pointer >= marker->size ) return false;
        
        return true;
}

int main(int argc,char* argv[])  {
/*
    ItemPointerData pointer;
    Marker marker = umem_alloc(sizeof(MarkerData),UMEM_NOFAIL);
    
    marker->size = 100;
    
    marker->items = umem_alloc(marker->size * sizeof(ItemPointerData),UMEM_NOFAIL);
    int count = 0;
    
    while ( DelegatedScanNext(marker, &pointer) ) {
        printf("count: %d\n",count++);
    }
*/
    PageHeaderData data;
    
    int count = (long)&data.pd_linp - (long)&data;
    printf("count: %d\n",count);
}
