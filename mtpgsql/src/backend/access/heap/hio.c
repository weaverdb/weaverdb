/*-------------------------------------------------------------------------
 *
 * hio.c
 *	  POSTGRES heap access method input/output code.
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  $Id: hio.c,v 1.2 2007/05/23 15:39:24 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"
#include "env/env.h"
#include "storage/localbuf.h"
#include "env/freespace.h"
#include "access/heapam.h"
#include "access/hio.h"
#include "access/blobstorage.h"


static Buffer RelationGetTupleData
   (Relation rel, HeapTuple tuple, bool readonly, Buffer buffer);
/*
 * amputunique	- place tuple at tid
 *	 Currently on errors, calls elog.  Perhaps should return -1?
 *	 Possible errors include the addition of a tuple to the page
 *	 between the time the linep is chosen and the page is L_UP'd.
 *
 *	 This should be coordinated with the B-tree code.
 *	 Probably needs to have an amdelunique to allow for
 *	 internal index records to be deleted and reordered as needed.
 *	 For the heap AM, this should never be needed.
 *
 *	 Note - we assume that caller hold BUFFER_LOCK_EXCLUSIVE on the buffer.
 *
 */
void
RelationPutHeapTuple(Relation relation,
					 Buffer buffer,
					 HeapTuple tuple)
{
	Page		pageHeader;
	OffsetNumber offnum;
	Size		len;
	ItemId		itemId;
	Item		item;

	/* ----------------
	 *	increment access statistics
	 * ----------------
	 */
#if USESTATS
	IncrHeapAccessStat(local_RelationPutHeapTuple);
	IncrHeapAccessStat(global_RelationPutHeapTuple);
#endif
        pageHeader = (Page) BufferGetPage(buffer);
	len = MAXALIGN(tuple->t_len);		/* be conservative */
	
        Assert(len <= PageGetFreeSpace(pageHeader));
        Assert(BufferIsCritical(buffer));
        
	offnum = PageAddItem((Page) pageHeader, (Item) tuple->t_data,
						 tuple->t_len, InvalidOffsetNumber, LP_USED);
	
	itemId = PageGetItemId((Page) pageHeader, offnum);
	item = PageGetItem((Page) pageHeader, itemId);

	ItemPointerSet(&((HeapTupleHeader) item)->t_ctid,
				   BufferGetBlockNumber(buffer), offnum);

	/* return an accurate tuple */
	ItemPointerSet(&tuple->t_self, BufferGetBlockNumber(buffer), offnum);
	ItemPointerSet(&tuple->t_data->t_ctid, BufferGetBlockNumber(buffer), offnum);

}

BlockNumber
RelationPutHeapTupleAtFreespace(Relation relation, HeapTuple tuple, BlockNumber limit)
{
	Buffer		buffer = InvalidBuffer;
	Page		pageHeader;
	BlockNumber     lastblock;
	OffsetNumber    offnum;
	Size		len;
	ItemId		itemId;
	Item		item;
	bool		satisfied = false;
        Size            pageSize = 0;
	
	/*
	 * If we're gonna fail for oversize tuple, do it right away... this
	 * code should go away eventually.
	*/ 
	 if ( tuple->t_info & TUPLE_HASBUFFERED ) {
                limit =  span_buffered_blob(relation,tuple);
	 }
	len = MAXALIGN(tuple->t_len);		/* be conservative */
	if (len > MaxTupleSize) {
            if ( relation->rd_att->blobatt > 0 ) {
            /* setting limit here makes sure that the header tuple follows any
                    segments already stored */
                    limit = store_tuple_blob(relation,tuple,SIZE_SPAN);
                    len = MAXALIGN(tuple->t_len);	
            } else {
                    Env * env = GetEnv();
                    env->errorcode = 901;
                    env->tupleSize = len;
                    elog(ERROR, "Tuple is too big: size %u, max size %ld",
                            len, MaxTupleSize);
            }
	}
	
        while ( !satisfied ) {	
            if ( BufferIsValid(buffer) ) {
                BlockNumber blk = BufferGetBlockNumber(buffer);
                ReleaseBuffer(relation,buffer);
                DeactivateFreespace(relation,blk,pageSize);
            }
           
            if ( !BlockNumberIsValid(limit) ) {
                limit = 0;
                elog(NOTICE,"Invalid limit for heap io");
            }
            
            lastblock = GetFreespace(relation,len,limit);

            buffer = ReadBuffer(relation,lastblock);

            if ( !BufferIsValid(buffer) ) {
                DeactivateFreespace(relation,lastblock,0);
                continue;
            }

            LockBuffer(relation, buffer, BUFFER_LOCK_EXCLUSIVE);
            pageHeader = (Page) BufferGetPage(buffer);	
            pageSize = PageGetFreeSpace(pageHeader);
/* 
have to check the size b/c Updates try and 
put tuples on the same page as the
one it replaces, the number held by the freespace
manager may be old and incorrect.   MKS 12.31.2001
*/	
            if ( BufferHasError(buffer) ) {
                pageSize = 0;
            } else if ( pageSize < MAXALIGN(len) ) { 
                DTRACE_PROBE2(mtpg,freespace__miss,len,pageSize);
            } else {
                DTRACE_PROBE2(mtpg,freespace__hit,len,pageSize);
                satisfied = true;
            }
            if ( satisfied ) {
                Assert(BufferIsCritical(buffer));
                offnum = PageAddItem( pageHeader, (Item) tuple->t_data,
                                 tuple->t_len, InvalidOffsetNumber, LP_USED);

                if ( offnum == InvalidOffsetNumber ) {
                    elog(FATAL,"Invalid offset");
                }
	
                itemId = PageGetItemId((Page) pageHeader, offnum);
                item = PageGetItem((Page) pageHeader, itemId);
        
                ItemPointerSet(&((HeapTupleHeader)item)->t_ctid, lastblock, offnum);
                ItemPointerSet(&tuple->t_self, lastblock, offnum);	
                ItemPointerSet(&tuple->t_data->t_ctid, lastblock, offnum);	

            }
            
            LockBuffer(relation, buffer, BUFFER_LOCK_UNLOCK);
        }

        lastblock = BufferGetBlockNumber(buffer);

        WriteBuffer(relation, buffer);
        return lastblock;
}

Buffer
RelationGetTupleData(Relation rel, HeapTuple tuple, bool readonly, Buffer buffer) {
        Page dp = NULL;
        ItemId lp = NULL;
	ItemPointer pointer = &tuple->t_self;
        
        if ( readonly ) {
            BufferDesc* desc = LocalBufferSpecialAlloc(rel,ItemPointerGetBlockNumber(pointer));
            
            if ( desc != NULL ) {
                int status = smgrread(rel->rd_smgr, ItemPointerGetBlockNumber(pointer), (char *) MAKE_PTR(desc->data));
                desc->ioflags &= BM_READONLY;
                if ( status == SM_SUCCESS ) {
                    if ( !PageConfirmChecksum((Page)MAKE_PTR(desc->data) ) ) {
                        elog(NOTICE, "Heap Page is corrupted name:%s page:%ld", rel->rd_rel->relname, ItemPointerGetBlockNumber(pointer));
                    }
                }
            }
            buffer = BufferDescriptorGetBuffer(desc);
        } else if ( BufferIsValid(buffer) ) {
            buffer = ReleaseAndReadBuffer(buffer, rel, ItemPointerGetBlockNumber(pointer));
        } else {
            buffer = ReadBuffer(rel, ItemPointerGetBlockNumber(pointer));
        }
        
        if (!BufferIsValid(buffer)) {
            elog(ERROR, "get_segment: Bad Buffer");
        }
        
        if (BufferHasError(buffer)) {
            ReleaseBuffer(rel,buffer);
            elog(ERROR, "get_segment: Error Buffer");
        }
        
	LockBuffer((rel),buffer, BUFFER_LOCK_SHARE);
        dp = BufferGetPage(buffer);
        if ( PageIsValid(dp) ) {
            	OffsetNumber  offnum = ItemPointerGetOffsetNumber(pointer);
                if ( offnum <= PageGetMaxOffsetNumber(dp) ) {
                    lp = PageGetItemId(dp, offnum);
                    if ( !ItemIdIsUsed(lp) ) lp = NULL;
                }
        }
        LockBuffer((rel),buffer, BUFFER_LOCK_UNLOCK);
        if ( BufferIsValid(buffer) ) {
            if ( ItemIdIsValid(lp) ) {
                 tuple->t_data = (HeapTupleHeader) PageGetItem(dp, lp);
                 tuple->t_len = ItemIdGetLength(lp);
            } else {
                 ReleaseBuffer(rel,buffer);
                 buffer = InvalidBuffer;
            }   
	}
        return buffer;
 }

Buffer
RelationGetHeapTuple(Relation rel, HeapTuple tuple) {
    return RelationGetHeapTupleWithBuffer(rel, tuple, InvalidBuffer);
}


Buffer
RelationGetHeapTupleWithBuffer(Relation rel, HeapTuple tuple, Buffer inbuffer) {
    tuple->t_datamcxt = NULL;
    tuple->t_datasrc = NULL;
    tuple->t_info = 0;
    tuple->t_data = NULL;
    tuple->t_len = 0;
    
    return RelationGetTupleData(rel, tuple, ((rel->rd_rel->relkind == RELKIND_RELATION) && (tuple->t_info == TUPLE_READONLY)), inbuffer);
}

void
LockHeapTuple(Relation rel, Buffer buf, HeapTuple tuple, int mode) {
	LockBuffer(rel, buf, mode);
}

int
LockHeapTupleForUpdate(Relation relation, Buffer * buf, HeapTuple tuple, Snapshot snapshot) {
    int                 result = -1;     
        SnapshotHolder*     holder = RelationGetSnapshotCxt(relation);
        bool nowait = false;
        if ( !IsSnapshotNow(snapshot) &&
                !IsSnapshotAny(snapshot) &&
                !IsSnapshotSelf(snapshot)) {
            nowait = snapshot->nowait;
        }

     while ( result != HeapTupleMayBeUpdated ) {
        *buf = RelationGetHeapTuple(relation, tuple);
        if ( !BufferIsValid(*buf) ) {
            elog(NOTICE,"lock for update pointer error");
            return HeapTupleInvisible;
        }
        LockHeapTuple(relation,*buf,tuple,TUPLE_LOCK_WRITE);
        result = HeapTupleSatisfiesUpdate(holder,tuple,snapshot);
        if (result == HeapTupleInvisible)
        {
                char  pid[255];

                memset(pid,0x00,255);
                sprintf(pid,"pstack %d",getpid());
                
                elog(NOTICE, "locking invisible tuple");
                my_system(pid);
                break;
        } else if (result == HeapTupleBeingUpdated) {
                TransactionId xwait = tuple->t_data->t_xmax;
                if ( nowait ) return result;
                LockHeapTuple(relation,*buf,tuple,TUPLE_LOCK_UNLOCK);
                ReleaseBuffer((relation), *buf);
                XactLockTableWait(xwait);
                /*  cycle around and recheck qhat happend to the tuple  */
        } else {
                /*  anything else can be handled outside the loop  */
            break;
        }
    }    
    
    return result;
}

void 
UnlockHeapTuple(Relation rel, Buffer buf, HeapTuple tuple) {
    LockBuffer(rel , buf, BUFFER_LOCK_UNLOCK);
}

