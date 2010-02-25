/*-------------------------------------------------------------------------
 *
 * blobstorage.c
 *	  store a datum across multiple pages
 *
 *
 *
 * IDENTIFICATION
 *	  $Header: /cvs/weaver/mtpgsql/src/backend/access/heap/blobstorage.c,v 1.6 2007/05/31 15:02:30 synmscott Exp $
 *
 *-------------------------------------------------------------------------
*/

#include <sys/sdt.h>
#include <sys/varargs.h>

#include "postgres.h"
#include "env/env.h"

#include "env/connectionutil.h"
#include "env/freespace.h"
#include "access/blobstorage.h"

#include "utils/tqual.h"
#include "utils/syscache.h"
#include "access/heapam.h"
#include "catalog/pg_type.h"
#include "catalog/pg_extstore.h"
#include "catalog/catname.h"
#include "access/tupmacs.h"

#include "access/hio.h"
#include "catalog/catalog.h"
#include "miscadmin.h"
#include "storage/smgr.h"
#include "utils/builtins.h"
#include "utils/inval.h"
#include "utils/relcache.h"

#include "env/pg_crc.h"

typedef struct blobseg {
	ItemPointerData seg_next;
	int32           seg_length;
        bool            seg_blobhead;
	char           *seg_data;
} blob_segment_data;

typedef struct blobhead {
        int32           pointer_length;
	uint32           blob_length;
	ItemPointerData forward_pointer;
        Oid             relid;
}  blob_header;

typedef struct read_pipeline {
        int32               header;
        Oid                 rel;
        ItemPointerData     head_pointer;
        ItemPointerData     tail_pointer;
        uint32              length;
        uint32              read;
        char*               cache_data;
        int16               cache_offset;
        int16               cache_length;
        bool                read_only;
        MemoryContext       cxt;
}   read_pipeline;

typedef struct write_pipeline {
        int32               header;
        Oid		    rel;
        ItemPointerData     head_pointer;
        ItemPointerData     tail_pointer;
        uint32                length;
        uint32                written;
        BlockNumber         limit;
        bytea*               cache_data;
        uint32              cache_limit;
        MemoryContext       cxt;
} write_pipeline;

typedef struct segmentheader {
	int32           length;
	ItemPointerData forward;
	char            data[1];
}               segment_header;

static long     SEGHDRSZ = MAXALIGN(offsetof(segment_header, data));

typedef struct bloblist {
	int16           attnum;
	Datum           data;
	void           *next;
}               blob_list;

typedef blob_segment_data *blob_segment;

typedef struct dupingpack {
    HeapTuple   tuple;
    Buffer          buffer;
    BlockNumber     limit;
    BlockNumber     max;
} DupingPack;

Size    segment_size = 0;

static HeapTuple store_segment(Relation rel, blob_segment segment, BlockNumber limit);
static int  delete_segment(Relation rel, ItemPointer pointer, bool moved);
static int get_segment(Relation rel, ItemPointer pointer, bool read_only, char *target, int limit);
static bool store_blob_segments(Relation rel, bytea * data, ItemPointer start, ItemPointer end);
static blob_list * prioritize_blobs(Relation rel, HeapTuple tuple, int16 attnum);
static int delete_blob_segments(Relation rel, ItemPointer first, bool moved);
static Relation find_storage_relation(Relation rel,HeapTuple tuple,int16 attnum);
static int  delete_indirect_blob(Datum item);

static bool vacuum_check_update_pointer(Relation relation, Buffer targetbuffer, HeapTuple target, ItemPointer forward);
static bool lock_segment_for_update(Relation relation, Buffer*  buf, HeapTuple tuple);
static bool vacuum_link_end(Relation rel, ItemPointer forward);

static void unlock_segment(Relation relation, Buffer buf, HeapTuple tuple);

static DupingPack* vacuum_dup_segment(Relation rel, DupingPack* pack);

static void  blob_log(Relation rel, char* pattern, ...);

Size
sizeof_max_tuple_blob()
{

    if ( segment_size == 0 ) {
        segment_size = (MaxAttrSize - SEGHDRSZ);
        char* size = GetProperty("blobsegments");
        if ( size != NULL ) {
            Size ref = atoi(size);
            if ( ref > 0 && ref < segment_size)  {
                segment_size = ref;
            }
        }
    } 
    return segment_size;
}

HeapTuple
store_segment(Relation rel, blob_segment segment, BlockNumber limit)
{
	TransactionId   xid;
	HeapTuple       seg_tuple;
	segment_header *header;

	int32           structsz = MAXALIGN(SEGHDRSZ + segment->seg_length);

	header = (segment_header *) palloc(structsz);
	header->length = segment->seg_length;
	header->forward = segment->seg_next;
	memmove(header->data, segment->seg_data, segment->seg_length);
	seg_tuple = heap_addheader(3, structsz, (char *) header);
	xid = GetCurrentTransactionId();
	seg_tuple->t_data->t_xmin = xid;
        seg_tuple->t_data->progress.cmd.t_cmin = GetCurrentCommandId();
        seg_tuple->t_data->progress.cmd.t_cmax = FirstCommandId;
	seg_tuple->t_data->t_xmax = InvalidTransactionId;

	seg_tuple->t_data->t_infomask &= ~(HEAP_XACT_MASK);
	seg_tuple->t_data->t_infomask |= (HEAP_XMAX_INVALID | HEAP_BLOB_SEGMENT);
  /*  special mark for the head of a blob stream  */
        if ( segment->seg_blobhead ) {
            seg_tuple->t_data->t_infomask |= HEAP_BLOBHEAD;
        }

	if (rel->rd_rel->relkind == RELKIND_RELATION) {
            bool handled = false;

            Buffer buffer = ReadBuffer(rel,limit);
            if ( BufferIsValid(buffer) ) {
                LockBuffer(rel,buffer,BUFFER_LOCK_EXCLUSIVE);
                if ( MAXALIGN(seg_tuple->t_len) < PageGetFreeSpace(BufferGetPage(buffer)) ) {
                    RelationPutHeapTuple(rel,buffer,seg_tuple);
                    handled = true;
                }
                LockBuffer(rel,buffer,BUFFER_LOCK_UNLOCK);
            }
            if ( handled ) {
                WriteBuffer(rel,buffer);
            } else {
                ReleaseBuffer(rel, buffer);
                RelationPutHeapTupleAtFreespace(rel, seg_tuple, limit);
            }
	} else {
		elog(ERROR, "blob insert into non - heap");
	}

	pfree(header);

	return seg_tuple;
}

int
delete_segment(Relation rel, ItemPointer pointer, bool moved)
{
	HeapTupleData   tp;
	Buffer          buffer = InvalidBuffer;
	int             count;
	ItemPointerData target;
	TransactionId	myXID;
	CommandId	myCID;
        
        ItemPointerCopy(pointer,&target);
        ItemPointerSetInvalid(pointer);

        myXID = GetCurrentTransactionId();
        myCID = GetCurrentCommandId();

        /* store transaction information of xact deleting the tuple */
        while ( ItemPointerIsValid(&target) ) {
		bool delete = TRUE;

	        ItemPointerCopy(&target,&tp.t_self); 
                tp.t_info = 0;
                buffer = RelationGetHeapTuple(rel,&tp);
		if ( !BufferIsValid(buffer) ) return count;
		LockHeapTuple(rel,buffer,&tp,TUPLE_LOCK_WRITE);
		tp.t_data->t_xmax = myXID;

                if ( !moved ) tp.t_data->progress.cmd.t_cmax = myCID;
		else tp.t_data->t_infomask |= (HEAP_MOVED_OUT);

                tp.t_data->t_infomask &= ~(HEAP_MARKED_FOR_UPDATE | HEAP_XMAX_COMMITTED | HEAP_XMAX_INVALID);
                /*tp.t_data->t_infomask |= HEAP_MARKED_FOR_UPDATE;*/
                /*  the next target to test is the copied segment */
                ItemPointerCopy(&tp.t_data->t_ctid, &target);
                /* set the forward pointer only if this is the first time though  */
                if ( !ItemPointerIsValid(pointer) ) {
                    ItemPointerCopy(&((segment_header *) GETSTRUCT(&tp))->forward,pointer);
                }

                if ( !vacuum_check_update_pointer(rel, buffer, &tp, &target) ) {
      /*  the chain ends here because this is not a moved tuple, last man starnding 
          is not deleted for a moved chain */
                    if ( moved ) {
                        delete = false;
			tp.t_data->t_xmax = InvalidTransactionId;
                        tp.t_data->t_infomask &= ~(HEAP_XMAX_COMMITTED | HEAP_MARKED_FOR_UPDATE);
                        tp.t_data->t_infomask |= (HEAP_XMAX_INVALID);
                    }
                   
                   ItemPointerSetInvalid(&target);
                }
		LockHeapTuple(rel,buffer,&tp,TUPLE_LOCK_UNLOCK);
                
                if ( delete ) {
                    WriteBuffer(rel, buffer);
                    count++;
                } else {
                    ReleaseBuffer(rel, buffer);
                }
        }
    /*  tell the caller if this needs to be called again  */
	return count;
}


int
get_segment(Relation rel, ItemPointer pointer, bool read_only, char *target, int limit)
{
	HeapTupleData   tp;
	Buffer          buffer;
	segment_header *data;
	int             len = 0;
        
        Assert(ItemPointerIsValid(pointer));
        
        tp.t_self = *pointer;
  /*  if the TransactionDidHardCommit then opening the pipeline asks for read_only
   *  allocate in a local buffer cause we don't need 
   *  blobs blowing away buffers in the shared buffer pool
   */
        if ( read_only ) tp.t_info = TUPLE_READONLY;
            
        buffer = RelationGetHeapTuple(rel,&tp);

        if ( BufferIsValid(buffer) ) {
            LockHeapTuple(rel,buffer,&tp,TUPLE_LOCK_READ);
            /*  set commit flags if nessessary  */
            if ( !HeapTupleSatisfies(rel,buffer,&tp,GetSnapshotQuery(RelationGetSnapshotCxt(rel)),0,NULL) ) {
                len = -1;
            } else {
            	data = (segment_header *)GETSTRUCT(&tp);

            	if (MAXALIGN(data) != (Size)data) {
               	    len = -1;
            	} else if ( data->length > MaxAttrSize ) {
                    len = -1;
           	} else {
               	    len = data->length;
              	}
	   }
        } else {
                blob_log(rel,"get_segment -- bad forward pointer blk: %ld offset: %d",
                        ItemPointerGetBlockNumber(pointer),ItemPointerGetOffsetNumber(pointer));
                return -1;
        }
            
        if ( (len < 0) ) {
             blob_log(rel,"get_segment -- inconsistent blob data detected blk: %ld offset: %d",
                        ItemPointerGetBlockNumber(pointer),ItemPointerGetOffsetNumber(pointer));
            len = -1;
            ItemPointerSetInvalid(pointer);
        } else {
            if ( len > limit && target != NULL ) {
   /*  if the target is smaller than the size 
    *   of the segment don't update the pointer 
    *  and return 0
    */
                len = 0;
            } else {
                ItemPointerCopy(&data->forward, pointer);
                if ( target != NULL ) {
                    Assert(data->length == len);
                    memmove(target, data->data, len);
                }
            }
        }
        
        LockHeapTuple(rel,buffer,&tp,TUPLE_LOCK_UNLOCK);

	ReleaseBuffer(rel, buffer);

	return len;
}

uint32 
sizeof_indirect_blob(Datum pipe) {
        blob_header     header;
        memmove(&header,DatumGetPointer(pipe),sizeof(blob_header));
	return header.blob_length - VARHDRSZ;
}

Datum
open_read_pipeline_blob(Datum pointer, bool read_only) {
    bool  isNull;

    read_pipeline*      pipe = palloc(sizeof(read_pipeline));

    blob_header     header;
    memmove(&header, DatumGetPointer(pointer),sizeof(blob_header));

    pipe->rel = header.relid;

    pipe->head_pointer = header.forward_pointer;
    pipe->tail_pointer = header.forward_pointer;

    pipe->length = header.blob_length;
    pipe->read = 0;

    pipe->cache_data = NULL;
    pipe->cache_offset = 0;
    pipe->cache_length = 0;
    pipe->read_only = read_only;
    
    pipe->cxt = MemoryContextGetCurrentContext();

    SETVARSIZE(pipe,sizeof(read_pipeline));
    SETBUFFERED(pipe);

    return PointerGetDatum(pipe);
}

Datum
open_write_pipeline_blob(Relation rel) {
    write_pipeline*  pipe = palloc(sizeof(write_pipeline));
	
    pipe->rel = rel->rd_id;

    ItemPointerSetInvalid(&pipe->head_pointer);
    ItemPointerSetInvalid(&pipe->tail_pointer);
    pipe->length = 0;
    pipe->limit = 0;
    pipe->written = 0;
    pipe->cache_limit = sizeof_max_tuple_blob();
    pipe->cache_data = palloc(pipe->cache_limit + VARHDRSZ);
    SETVARSIZE(pipe->cache_data,VARHDRSZ);

    pipe->cxt = MemoryContextGetCurrentContext();

    SETVARSIZE(pipe,sizeof(write_pipeline));
    SETBUFFERED(pipe);

    return PointerGetDatum(pipe);
}

void
close_read_pipeline_blob(Datum pointer) {
    pfree(DatumGetPointer(pointer));
}

Datum 
close_write_pipeline_blob(Datum pointer) {

    blob_header* header = (blob_header *) palloc(sizeof(blob_header));

    write_pipeline* pipe = (write_pipeline*)DatumGetPointer(pointer);
    
    if ( VARSIZE(pipe->cache_data) > VARHDRSZ ) {
        write_pipeline_segment_blob(pointer,NULL);  /*  null means flush the cache to write_pipeline  */
    }

    header->pointer_length = sizeof(blob_header);
    header->blob_length = pipe->length + VARHDRSZ;
    header->forward_pointer = pipe->head_pointer;
    header->relid = pipe->rel;

    SETINDIRECT(header);
    
    pfree(pipe->cache_data);
    pfree(pipe);

    return PointerGetDatum(header);
}

bool 
read_pipeline_segment_blob(Datum pointer, char* target,int * length, int limit) {
    int count = 0;
    bool data_avail = false;
    
    read_pipeline* header = (read_pipeline*)DatumGetPointer(pointer);
    /* no more data, short circuit, return no data transfered */
    if ( header->cache_data == NULL && !ItemPointerIsValid(&header->tail_pointer) ) return false;
    if ( header->length == header->read ) return false;
        
    Relation rel = RelationIdGetRelation(header->rel,DEFAULTDBOID);
    LockRelation(rel,AccessShareLock);

    while( (limit - count) > 0 ) {
        int pass_lim = limit - count;
/*  first check the cache  */
        if ( header->cache_data ) {
            int cache_lim = (header->cache_length - header->cache_offset);
            int local_lim = ( pass_lim < cache_lim ) ? pass_lim : cache_lim;
            
            data_avail = true;
            memcpy((target + count),header->cache_data + header->cache_offset,local_lim);
            count += local_lim;
            header->cache_offset += local_lim;
            
            if ( header->cache_offset == header->cache_length ) {
                header->cache_offset = 0;
                header->cache_length = 0;
                pfree(header->cache_data);
                header->cache_data = NULL;
           }
        } else {
/*  go to disk  */
            if ( !ItemPointerIsValid(&header->tail_pointer) ) {
                break;
            } else {
                data_avail = true;
            }
            int read = get_segment(rel, &header->tail_pointer, header->read_only, (target + count), limit - count);
            if ( read < 0 ) {
                data_avail = false;
                blob_log(rel,"read pipeline error");
		ItemPointerSetInvalid(&header->tail_pointer);
		break;
            } else if ( read == 0 ) {
                int local_lim = sizeof_max_tuple_blob();
                Assert(header->cache_data == NULL);
                header->cache_data = MemoryContextAlloc(header->cxt,local_lim);
                read = get_segment(rel, &header->tail_pointer, header->read_only, header->cache_data, local_lim);
                if ( read < 0 ) {
                    data_avail = false;
                    break;
                }
                header->cache_offset = 0;
                header->cache_length = read;
            }
            else count += read;
        }
    }
    *length = count;
    header->read += count;
    
    UnlockRelation(rel,AccessShareLock);
    
    if ( header->read > header->length ) {
        blob_log(rel,"read_pipeline -- inconsistent blob detected read:%d length:%d",header->read,header->length);
        *length = count - (header->read - header->length);
        if ( *length <= 0 ) {
            *length = 0;
            data_avail = false;
        }
        header->read = header->length;
        ItemPointerSetInvalid(&header->tail_pointer);
    }
    RelationClose(rel);

    return data_avail;
}

bool
write_pipeline_segment_blob(Datum pointer, bytea * data)
{
    HeapTupleData tp;
    segment_header*  seg;
    ItemPointerData start,end;
    bytea*  send = NULL;
    
    write_pipeline * header = (write_pipeline*)DatumGetPointer(pointer);

    ItemPointerCopy(&header->head_pointer,&start);
    ItemPointerCopy(&header->tail_pointer,&end);
    
    if ( data != NULL ) {
    /*  cache manipulation,  copy the cache to the front and save the tail in the cache  */
        int data_len = VARSIZE(data) - VARHDRSZ;
        int cache_len = VARSIZE(header->cache_data) - VARHDRSZ;
        int tail = ((cache_len + data_len) % header->cache_limit);
        if ( cache_len == 0 && tail == 0 ) {
            /*  send data directly to storage data has been optimized so the cache can be skipped */  
        } else if ( (data_len + cache_len) > header->cache_limit ) {
            send = pmerge(header->cache_data,VARSIZE(header->cache_data),VARDATA(data),data_len);
            SETVARSIZE(send,VARSIZE(header->cache_data) + data_len - tail);
            memmove(VARDATA(header->cache_data),VARDATA(data) + (data_len - tail),tail);
            SETVARSIZE(header->cache_data,tail + VARHDRSZ);
            data = send;
        } else {
            /* move data into cache and return  */
            memmove(VARDATA(header->cache_data)+cache_len,VARDATA(data),data_len);
            SETVARSIZE(header->cache_data,cache_len+data_len+VARHDRSZ);
            return false;
        }
    } else {
        /* flush the cache */
        data = header->cache_data;
        if ( VARSIZE(data) == VARHDRSZ ) return false;

    }

    Relation rel = RelationIdGetRelation(header->rel,DEFAULTDBOID);
    LockRelation(rel,AccessShareLock);
/* start the blob segment by breaking it up into sections 
* and storing to disk.  Record the start and the end pointers 
* to the pipeline */
    if ( store_blob_segments(rel,data,&start,&end) ) {
    /* if the tail is valid, open it up and append the 
     * just added section */
        if ( ItemPointerIsValid(&header->tail_pointer) ) {
            Buffer buffer = InvalidBuffer;

            tp.t_self = header->tail_pointer;
            tp.t_info = 0;
            buffer = RelationGetHeapTuple(rel,&tp);

            if ( !BufferIsValid(buffer) ) {
                blob_log(rel,"bad tail pointer blk: %ld offset: %d",
                    ItemPointerGetBlockNumber(&header->tail_pointer),ItemPointerGetOffsetNumber(&header->tail_pointer));
                elog(ERROR,"writing stream");
            }
            
            LockHeapTuple(rel,buffer,&tp,TUPLE_LOCK_WRITE);
            seg = (segment_header*) GETSTRUCT(&tp);
            ItemPointerCopy(&start,&seg->forward);
            LockHeapTuple(rel,buffer,&tp,TUPLE_LOCK_UNLOCK);

            WriteBuffer(rel, buffer);

            ItemPointerCopy(&end,&header->tail_pointer);
        } else {
     /* the head and tail are invalid so 
      * this is the first segment of the blob */
            Assert(!ItemPointerIsValid(&header->head_pointer));
            ItemPointerCopy(&start,&header->head_pointer);
            ItemPointerCopy(&end,&header->tail_pointer);
        }
        header->length += VARSIZE(data) - VARHDRSZ;
    }

    if ( send != NULL ) pfree(send);
    UnlockRelation(rel,AccessShareLock);
    RelationClose(rel);

    return true;
}

bool
store_blob_segments(Relation rel, bytea * data, ItemPointer start, ItemPointer end)
{
	blob_segment    map;
	char           *raw;
	int             pos = 0;
	int             counter = 0;
        ItemPointerData link;
        BlockNumber     limit = (ItemPointerIsValid(start)) ? ItemPointerGetBlockNumber(start) : 0;
	int32           copylen = VARSIZE(data) - VARHDRSZ;
	int32           size = sizeof_max_tuple_blob();
        int             len = 0;
        BlockNumber*    storage;
        

        if (copylen <= 0) return FALSE;
	/* form the segmented map  */
	len = copylen / size;
	if ((copylen % size) != 0) len++;
        
        storage = palloc(len * sizeof(BlockNumber));

	map = (blob_segment) palloc(len * sizeof(blob_segment_data));
	raw = VARDATA(data);
        

	for (counter = 0; counter < len - 1; counter++) {
            /*  this is the head of the blob if the counter is zero and the start pointer is invalid
             *  store segment will mark is specially so we can find it on a 
             *  vacuum scan
             */
                map[counter].seg_blobhead = FALSE;
		map[counter].seg_data = raw + pos;
		pos += size;
		map[counter].seg_length = size;
                limit = GetFreespace(rel,size + sizeof(HeapTupleHeader) + (SEGHDRSZ),limit);
                storage[counter] = limit;
	}

        map[counter].seg_blobhead = FALSE;
	map[counter].seg_data = raw + pos;
	map[counter].seg_length = (copylen - pos);
        storage[counter] = GetFreespace(rel,map[counter].seg_length + sizeof(HeapTupleHeader) + (SEGHDRSZ),limit);
  /* if start is invalid the first segment is the head of the entire blob */      
        if ( !ItemPointerIsValid(start) ) {
            map[0].seg_blobhead = TRUE;
        }
        
        ItemPointerSetInvalid(&link);
	ItemPointerSetInvalid(start);
	ItemPointerSetInvalid(end);
        
        for (; counter >= 0; counter--) {   
            ItemPointerCopy(&link, &map[counter].seg_next);     
            HeapTuple       tuple = store_segment(rel, &map[counter], storage[counter]);
/*  the first section saved is actually the tail of the blob */
            if ( !ItemPointerIsValid(end) ) ItemPointerCopy(&tuple->t_self, end);

            ItemPointerCopy(&tuple->t_self, &link);
            limit = ItemPointerGetBlockNumber(&link);
            heap_freetuple(tuple);
	}
  /* save the head of the blob to the start reference */
        ItemPointerCopy(&link, start);

        pfree(storage);
	pfree(map);

	return TRUE;
}

bytea *
rebuild_indirect_blob(Datum item)
{
	int             pos = 0;
	ItemPointerData link;        
        blob_header     header;

        memmove(&header,DatumGetPointer(item),sizeof(blob_header));
	Relation        rel = RelationIdGetRelation(header.relid, DEFAULTDBOID);
	LockRelation(rel, AccessShareLock);

        bytea          *data = (bytea *) palloc(header.blob_length);
	SETVARSIZE(data, header.blob_length);
	link = header.forward_pointer;

	while (ItemPointerIsValid(&link)) {
            int read = get_segment(rel, &link, false, VARDATA(data) + pos, header.blob_length - pos - VARHDRSZ);
            if ( read < 0 ) {
               elog(ERROR,"error rebuilding blob");
            } else if ( read == 0 ) {
                elog(ERROR,"blob does not fit in provided space");
            } else {
                pos += read;
            }
	}

	if (pos + VARHDRSZ != header.blob_length) {
		elog(ERROR, "rebuild_indirect -- inconsistent spanning blob detected");
	}

        UnlockRelation(rel,AccessShareLock);
        RelationClose(rel);

	return data;
}

int 
delete_indirect_blob(Datum item)
{
	int             pos = 0;
	ItemPointerData link;        
        blob_header     header;

        memmove(&header,DatumGetPointer(item),sizeof(blob_header));
	Relation        rel = RelationIdGetRelation(header.relid, DEFAULTDBOID);
	LockRelation(rel, AccessShareLock);

        bytea          *data = (bytea *) palloc(header.blob_length);
	SETVARSIZE(data, header.blob_length);
	link = header.forward_pointer;

	while ( ItemPointerIsValid(&link) ) {
            pos+=delete_segment(rel, &link, false);
	}

        UnlockRelation(rel,AccessShareLock);
        RelationClose(rel);

	return pos;
}

Size
sizeof_tuple_blob(Relation rel, HeapTuple tuple)
{
	TupleDesc       atts = rel->rd_att;

	int             c = 0;
	bool            isNull = false;
	Datum           blob;

	Size            t_size = tuple->t_len;

	for (c = 0; c < atts->natts; c++) {
            if (atts->attrs[c]->attstorage == 'e' ) {
                blob = HeapGetAttr(tuple, atts->attrs[c]->attnum, rel->rd_att, &isNull);
                if ( blob == 0 ) break;
                if (!isNull && ISINDIRECT(blob)) {
                    blob_header     header;
                    memmove(&header,DatumGetPointer(blob),sizeof(blob_header));
                    t_size += (header.blob_length - (header.pointer_length & 0x00ffffff));
                }
            }
	}

	return t_size;
}

DupingPack*
vacuum_dup_segment(Relation rel, DupingPack* pack) {
        HeapTuple           copy = NULL;
        ItemPointerData     forward;
        segment_header*     segment;
        bool                delete = FALSE;
        int                 valid_check = ( HEAP_XMAX_INVALID | HEAP_XMIN_COMMITTED );
        
        if ( !BufferIsValid(pack->buffer) ) return NULL;

        LockHeapTuple(rel,pack->buffer,pack->tuple,TUPLE_LOCK_READ);
 /*  set any info flags and make sure that the tuple is in a stable state
  *  if the tuple is not min committed and xmax invalid forget about 
  *  trying to dupe the chain
  */       
        if ( HeapTupleSatisfies(rel,pack->buffer,pack->tuple,SnapshotNow,0,NULL) )  {
            if ( (pack->tuple->t_data->t_infomask & valid_check) == valid_check ) {
                segment = (segment_header*)GETSTRUCT(pack->tuple);
                ItemPointerCopy(&segment->forward,&forward); 
                                /* copy tuple */
                copy = heap_copytuple(pack->tuple);
            }
        }
        LockHeapTuple(rel,pack->buffer,pack->tuple,TUPLE_LOCK_UNLOCK);
        
        if ( copy == NULL ) {
     /* end the dup, segment was not stable to dup.  */
            ReleaseBuffer(rel,pack->buffer);
            pack->buffer = InvalidBuffer;
            pack->tuple = NULL;
            return NULL;
        }
        /*
         * Mark new tuple as moved_in by vacuum and store
         * xmin in t_cmin and store current XID in xmin
         */
        if (!(copy->t_data->t_infomask & HEAP_MOVED_IN)) {
                copy->t_data->progress.t_vtran = copy->t_data->t_xmin;
        }
        copy->t_data->t_xmin = GetCurrentTransactionId();
        copy->t_data->t_xmax = InvalidTransactionId;
        copy->t_data->t_infomask &= ~(HEAP_XACT_MASK);
        copy->t_data->t_infomask |= (HEAP_MOVED_IN | HEAP_XMAX_INVALID);

        pack->limit = RelationPutHeapTupleAtFreespace(rel, copy, pack->limit);
        
        LockHeapTuple(rel,pack->buffer,pack->tuple,TUPLE_LOCK_WRITE);
   /*  make sure the tuple is still stable  */
        if ( pack->limit < pack->max && ((pack->tuple->t_data->t_infomask & valid_check) == valid_check) ) {
   /*  mark the old tuple as dup'ed  */
            ItemPointerCopy(&copy->t_self,&pack->tuple->t_data->t_ctid); 
            pack->tuple->t_data->t_xmax = GetCurrentTransactionId();
	    pack->tuple->t_data->t_infomask |= (HEAP_MARKED_FOR_UPDATE | HEAP_UPDATED);
        } else {
   /*  delete the inserted tuple and end chain  */
            delete = TRUE;
        }
        LockHeapTuple(rel,pack->buffer,pack->tuple,TUPLE_LOCK_UNLOCK);
            
        if ( delete ) {
            HeapTupleData  delete;
            
            delete.t_self = copy->t_self;
            delete.t_info = 0;
       /* delete inserted segment and return, moving would not help  */
       /* move is false because ctid and self are the same which would prevent delete  */
         /*  have to do this manually b/c of snapshot  issues during vacuum */
            pack->buffer = RelationGetHeapTupleWithBuffer(rel,&delete,pack->buffer);
            if ( BufferIsValid(pack->buffer) ) {
                LockHeapTuple(rel,pack->buffer,&delete,TUPLE_LOCK_WRITE);
                delete.t_data->t_xmax = GetCurrentTransactionId();
                delete.t_data->t_infomask &= ~(HEAP_XMAX_INVALID);
                LockHeapTuple(rel,pack->buffer,&delete,TUPLE_LOCK_UNLOCK);
                WriteBuffer(rel,pack->buffer);
                pack->buffer= InvalidBuffer;
                pack->tuple = NULL;
            }
            pack = NULL;
        } else {
            WriteBuffer(rel, pack->buffer);

            if ( ItemPointerIsValid(&forward) ) {
                 ItemPointerCopy(&forward,&pack->tuple->t_self);
                 pack->tuple->t_info = 0;
                 pack->buffer = RelationGetHeapTuple(rel,pack->tuple);
            } else {
                pack->buffer = InvalidBuffer;
                pack->tuple = NULL;
            }
        }

        heap_freetuple(copy); 
        
        return pack;
}

int
vacuum_dup_chain_blob(Relation storerel, ItemPointer front, BlockNumber * last_moved) {
        int                 moved = 0;
        Buffer              userbuffer = InvalidBuffer;
        HeapTupleData       tuple;
        ItemPointerData     target;
	MemoryContext       parent = MemoryContextGetCurrentContext();
        bool                dupe = FALSE;

	MemoryContext   blob_context = SubSetContextCreate(parent, "SpanBlobContext");
	MemoryContextSwitchTo(blob_context);
        
        LockRelation(storerel,AccessShareLock);
        
        tuple.t_self = *front;
        tuple.t_info = 0;
        userbuffer = RelationGetHeapTuple(storerel,&tuple);
	LockHeapTuple(storerel,userbuffer,&tuple,TUPLE_LOCK_READ); 
        ItemPointerCopy(&tuple.t_data->t_ctid, &target);
        dupe = !vacuum_check_update_pointer(storerel, userbuffer, &tuple, &target);
	LockHeapTuple(storerel,userbuffer,&tuple,TUPLE_LOCK_UNLOCK); 
        
        if ( dupe ) {
            DupingPack      source;
            DupingPack*     pack = &source;
            
            pack->tuple = &tuple;
            pack->buffer = userbuffer;
            pack->limit = 0;
            pack->max = ItemPointerGetBlockNumber(front);

            Assert((tuple.t_data->t_infomask & (HEAP_BLOB_SEGMENT | HEAP_BLOBHEAD)) == 
                    (HEAP_BLOB_SEGMENT | HEAP_BLOBHEAD));
    /*  while the target is valid, move blocks */  
            while ( vacuum_dup_segment(storerel,pack) != NULL ) {
                moved++;
                if ( last_moved != NULL ) {
                    *last_moved = pack->limit;
                }
            }
        } else {
            ReleaseBuffer(storerel, userbuffer);
        }
            
        UnlockRelation(storerel,AccessShareLock);
        MemoryContextSwitchTo(parent);
        MemoryContextDelete(blob_context);
        return moved;
}


HeapTuple
vacuum_relink_tuple_blob(Relation rel, HeapTuple tuple)
{
	TupleDesc       atts = rel->rd_att;

	int             c = 0;
	blob_header    *header;
	Datum          *values;
	char           *nulls;
	char           *replaces;
	bool            isNull = false;
	HeapTuple       ret_tuple = NULL;
	BlockNumber     limit = 0;
	bool		changed = false;
        int linked = 0;

	MemoryContext   parent = MemoryContextGetCurrentContext();

	MemoryContext   blob_context = SubSetContextCreate(parent, "SpanBlobContext");
	MemoryContextSwitchTo(blob_context);

	values = palloc(atts->natts * sizeof(Datum));
	memset(values, 0x00, (atts->natts * sizeof(Datum)));
	nulls = palloc(atts->natts * sizeof(char));
	memset(nulls, ' ', (atts->natts * sizeof(char)));
	replaces = palloc(atts->natts * sizeof(char));
	memset(replaces, ' ', (atts->natts * sizeof(char)));
        
	for (c = 0; c < atts->natts; c++) {
            isNull = false;
            if (atts->attrs[c]->attstorage == 'e') {
                blob_header*     header;
                ItemPointerData		forward;
                Buffer          buf = InvalidBuffer;
                bool            dupe = FALSE;

                header = palloc(sizeof(blob_header));
                                
                Datum blob = HeapGetAttr(tuple, atts->attrs[c]->attnum, atts, &isNull);

                if (isNull || !ISINDIRECT(blob) ) {
                    continue;
                }

                Relation storerel = find_storage_relation(rel,tuple,atts->attrs[c]->attnum);
                LockRelation(storerel,AccessShareLock);

                memmove(header, DatumGetPointer(blob),sizeof(blob_header));
                
                ItemPointerCopy(&header->forward_pointer,&forward);
                if ( !ItemPointerIsValid(&forward) ) {
			RelationClose(storerel);
			continue;
		}

                dupe = vacuum_link_end(rel, &forward);

                if ( !dupe ) {     
                     pfree(header);
                } else {
                    changed = true;
                     ItemPointerCopy(&forward,&header->forward_pointer);
                     replaces[c] = 'r';
                     SETINDIRECT(header);
                     values[c] = PointerGetDatum(header);
                       	
			while ( ItemPointerIsValid(&forward) ) {
				HeapTupleData tupledata;
                                bool change = false;
                                
				ItemPointerCopy(&forward,&tupledata.t_self);
                                tupledata.t_info = 0;
				buf = RelationGetHeapTuple(storerel,&tupledata); 
				segment_header* unit = (segment_header*)GETSTRUCT(&tupledata);
				ItemPointerCopy(&unit->forward,&forward);
                        	change = vacuum_link_end(storerel,&forward);
                                LockHeapTuple(storerel,buf,&tupledata,TUPLE_LOCK_WRITE);
                                if ( change ) {
                                    unit->forward = forward;
                                    linked++;
                                }
                                LockHeapTuple(storerel,buf,&tupledata,TUPLE_LOCK_UNLOCK);
                                if ( change ) WriteBuffer(storerel,buf);
                                else ReleaseBuffer(storerel,buf);
                        }
                    }
				                                
                UnlockRelation(storerel,AccessShareLock);
                RelationClose(storerel);
            }
        }
       
	MemoryContextSwitchTo(parent);
	if ( changed ) {
		ret_tuple = heap_modifytuple(tuple, rel, values, nulls, replaces);
                ret_tuple->t_data->t_infomask |= tuple->t_data->t_infomask;
                ret_tuple->t_data->t_infomask |= HEAP_BLOBLINKED;
		ret_tuple->t_info |= TUPLE_HASBUFFERED;
	}
	MemoryContextDelete(blob_context);

	return ret_tuple;
}

bool
vacuum_link_end(Relation rel, ItemPointer forward) {

    HeapTupleData       tuple;
    Buffer              buf;
    bool		changed = FALSE;
   
    if ( !ItemPointerIsValid(forward) ) return false;
   
    ItemPointerCopy(forward, &tuple.t_self);
    tuple.t_info = 0;
    buf = RelationGetHeapTuple(rel, &tuple);
   
    while ( BufferIsValid(buf) ) {
	bool dupe;

        LockHeapTuple(rel,buf,&tuple,TUPLE_LOCK_READ);
        ItemPointerCopy(&tuple.t_data->t_ctid,forward);
        dupe = vacuum_check_update_pointer(rel, buf, &tuple, forward);
        ItemPointerCopy(forward,&tuple.t_self);
        LockHeapTuple(rel,buf,&tuple,TUPLE_LOCK_UNLOCK);

        if ( dupe ) {
            tuple.t_info = 0;
            buf = RelationGetHeapTupleWithBuffer(rel,&tuple,buf);
            changed = TRUE;
        } else {
	    break;
        }
    }
    ReleaseBuffer(rel,buf);
    return changed;
}

HeapTuple
vacuum_respan_tuple_blob(Relation rel, HeapTuple tuple, bool exclude_self)
{
	TupleDesc       atts = rel->rd_att;

	int             c = 0;
	blob_header    *header;
	Datum          *values;
	char           *nulls;
	char           *replaces;
	bool            isNull = false;
	HeapTuple       ret_tuple = NULL;
        bool            changed = false;

	MemoryContext   parent = MemoryContextGetCurrentContext();

	MemoryContext   blob_context = SubSetContextCreate(parent, "SpanBlobContext");
	MemoryContextSwitchTo(blob_context);

	values = palloc(atts->natts * sizeof(Datum));
	memset(values, 0x00, (atts->natts * sizeof(Datum)));
	nulls = palloc(atts->natts * sizeof(char));
	memset(nulls, ' ', (atts->natts * sizeof(char)));
	replaces = palloc(atts->natts * sizeof(char));
	memset(replaces, ' ', (atts->natts * sizeof(char)));
                        
	for (c = 0; c < atts->natts; c++) {
            isNull = false;
            if (atts->attrs[c]->attstorage == 'e') {
                Datum blob = HeapGetAttr(tuple, atts->attrs[c]->attnum, atts, &isNull);
                blob_header     header;
                memmove(&header, DatumGetPointer(blob),sizeof(blob_header));
    
                if (isNull || !ISINDIRECT(blob) ) {
                    continue;
                }

                Relation storerel = find_storage_relation(rel,tuple,atts->attrs[c]->attnum);
  /*  
   *    if the store relation is different from the 
   *    current relation, ignore respanning
   */
                if ( exclude_self && storerel->rd_id == header.relid ) {
                    RelationClose(storerel);
                    continue;
                }
                
                LockRelation(storerel,AccessShareLock);

                Datum read = open_read_pipeline_blob(blob,false);
                
                int buf_sz = (sizeof_max_tuple_blob() * 5) + VARHDRSZ;
                bytea* append = palloc(buf_sz);
                int length = 0; 

                Datum write = open_write_pipeline_blob(storerel);
                
                while (read_pipeline_segment_blob(read,VARDATA(append),&length,buf_sz - VARHDRSZ)) {
                    SETVARSIZE(append,length + VARHDRSZ);
                    write_pipeline_segment_blob(write,append);
                }
                
                replaces[c] = 'r';
                values[c] = close_write_pipeline_blob(write);
                close_read_pipeline_blob(read);
                
                pfree(append);
                
                UnlockRelation(storerel,AccessShareLock);
                RelationClose(storerel);
                changed = true;
            }
	}

	MemoryContextSwitchTo(parent);
        if ( changed ) {
            ret_tuple = heap_modifytuple(tuple, rel, values, nulls, replaces);
            ret_tuple->t_data->t_infomask |= tuple->t_data->t_infomask;
            ret_tuple->t_info |= TUPLE_HASBUFFERED;
            ret_tuple->t_data->t_infomask |= HEAP_BLOBLINKED;
            ItemPointerSetInvalid(&ret_tuple->t_self);
        }
	MemoryContextDelete(blob_context);
	/*
	 * if this is a memory allocated tuple delete the old memory segment
	 */


	return ret_tuple;
}

BlockNumber
span_buffered_blob(Relation rel, HeapTuple tuple)
{
	TupleDesc       atts = rel->rd_att;

	int             c = 0;
	blob_header    *header;
	Datum          *values;
	char           *nulls;
	char           *replaces;
	bool            isNull = false;
	HeapTuple       ret_tuple;
	BlockNumber     limit = 0;

	MemoryContext   parent = MemoryContextGetCurrentContext();

	MemoryContext   blob_context = SubSetContextCreate(parent, "SpanBlobContext");
	MemoryContextSwitchTo(blob_context);

	values = palloc(atts->natts * sizeof(Datum));
	memset(values, 0x00, (atts->natts * sizeof(Datum)));
	nulls = palloc(atts->natts * sizeof(char));
	memset(nulls, ' ', (atts->natts * sizeof(char)));
	replaces = palloc(atts->natts * sizeof(char));
	memset(replaces, ' ', (atts->natts * sizeof(char)));
                        
	for (c = 0; c < atts->natts; c++) {
            isNull = false;
            if (atts->attrs[c]->attstorage == 'e') {
		CommBuffer* com = NULL;
		bytea* 		append = NULL;
		int 		len = 0;
		int 		bufsz = (sizeof_max_tuple_blob() * 5) + VARHDRSZ;

                Datum blob = HeapGetAttr(tuple, atts->attrs[c]->attnum, atts, &isNull);

                if (isNull || !ISBUFFERED(blob) ) {
                    continue;
                }

                Relation storerel = find_storage_relation(rel,tuple,atts->attrs[c]->attnum);
                LockRelation(storerel,AccessShareLock);

                com = palloc(sizeof(CommBuffer));
                memcpy(com,DatumGetPointer(blob),sizeof(CommBuffer));
                append = palloc(bufsz);

                Datum write = open_write_pipeline_blob(storerel);
                while ( (len=com->pipe(com->args,VARDATA(append),0,bufsz - VARHDRSZ)) >= 0 ) {
                    if ( len > 0 ) {
                        SETVARSIZE(append,len + VARHDRSZ);
                        write_pipeline_segment_blob(write,append);
                    }
                }
                if ( len == COMM_ERROR ) {
                    elog(ERROR,"piping error");
                }

                replaces[c] = 'r';
                values[c] = close_write_pipeline_blob(write);
                pfree(append);
                pfree(com);
                if ( storerel->rd_id != rel->rd_id ) limit = 0;

                UnlockRelation(storerel,AccessShareLock);
                RelationClose(storerel);
            }
	}

	MemoryContextSwitchTo(parent);
	ret_tuple = heap_modifytuple(tuple, rel, values, nulls, replaces);
        ret_tuple->t_data->t_infomask |= tuple->t_data->t_infomask;
	MemoryContextDelete(blob_context);
	tuple->t_data = ret_tuple->t_data;
	tuple->t_len = ret_tuple->t_len;
	/*
	 * if this is a memory allocated tuple delete the old memory segment
	 */
	if (tuple->t_datasrc) {
		pfree(tuple->t_datasrc);
	}
	tuple->t_datasrc = ret_tuple;
        tuple->t_info |= TUPLE_HASBUFFERED;
	tuple->t_data->t_infomask |= HEAP_BLOBLINKED;
        ItemPointerSetInvalid(&tuple->t_self);  /* memory tuple */

	return limit;
}

BlockNumber
store_tuple_blob(Relation rel, HeapTuple tuple,int16 attnum)
{
	TupleDesc       atts = rel->rd_att;

	int             c = 0;
	blob_header    *header;
	Datum          *values;
	char           *nulls;
	char           *replaces;
	bool            isNull = false;
	HeapTuple       ret_tuple;
	blob_list      *list;
	BlockNumber     limit = 0;

	MemoryContext   parent = MemoryContextGetCurrentContext();
	MemoryContext   blob_context = SubSetContextCreate(parent, "SpanBlobContext");
	MemoryContextSwitchTo(blob_context);

	list = prioritize_blobs(rel, tuple, attnum);

	values = palloc(atts->natts * sizeof(Datum));
	memset(values, 0x00, (atts->natts * sizeof(Datum)));
	nulls = palloc(atts->natts * sizeof(char));
	memset(nulls, ' ', (atts->natts * sizeof(char)));
        replaces = palloc(atts->natts * sizeof(char));
	memset(replaces, ' ', (atts->natts * sizeof(char)));

	while (list != NULL) {
            Relation storerel = NULL;
            bytea*   data = NULL;

            if ( ISINDIRECT(DatumGetPointer(list->data)) ) {
                data = rebuild_indirect_blob(list->data);
                delete_indirect_blob(list->data);
            } else {
                data = (bytea*)DatumGetPointer(list->data);
            }

            if ( data != NULL ) {
                storerel = find_storage_relation(rel,tuple,list->attnum);
                LockRelation(storerel,AccessShareLock);

                ItemPointerData start,end;
                ItemPointerSetInvalid(&start);
                ItemPointerSetInvalid(&end);
                
                if ( store_blob_segments(storerel,data,&start,&end) ) {
                    if (storerel->rd_id == rel->rd_id && ItemPointerGetBlockNumber(&start) > limit) {
                        limit = ItemPointerGetBlockNumber(&start);
                        if ( storerel->rd_id != rel->rd_id ) limit = 0;
                    }
                    header = (blob_header *) palloc(sizeof(blob_header));

                    replaces[list->attnum - 1] = 'r';
                    header->pointer_length = sizeof(blob_header);
                    header->blob_length = VARSIZE(data);
                    header->forward_pointer = start;
                    header->relid = storerel->rd_id;

                    SETINDIRECT(header);

                    values[list->attnum - 1] = PointerGetDatum(header);
                }
                UnlockRelation(storerel,AccessShareLock);
                RelationClose(storerel);
            }
            list = list->next;
        }
        
	MemoryContextSwitchTo(parent);

	ret_tuple = heap_modifytuple(tuple, rel, values, nulls, replaces);
        ret_tuple->t_data->t_infomask |= tuple->t_data->t_infomask;

        MemoryContextDelete(blob_context);

	tuple->t_data = ret_tuple->t_data;
	/*
	 * if this is a memory allocated tuple delete the old memory segment
	 */
	if (tuple->t_datasrc) {
		pfree(tuple->t_datasrc);
	}
	tuple->t_datasrc = ret_tuple;
        tuple->t_info |= TUPLE_HASBUFFERED;
	tuple->t_len = ret_tuple->t_len;
	tuple->t_data->t_infomask |= HEAP_BLOBLINKED;
        ItemPointerSetInvalid(&tuple->t_self);  /* memory tuple */

	return limit;
}


blob_list      *
prioritize_blobs(Relation rel, HeapTuple tuple, int16 attnum)
{
	TupleDesc       atts = rel->rd_att;

	int             c = 0;
	blob_list      *header = NULL;
	bool            isNull = false;
	Datum           blob;
	HeapTuple       ret_tuple;
        int t_length = MAXALIGN(tuple->t_len);

	for (c = 0; c < atts->natts; c++) {
            if (atts->attrs[c]->attstorage != 'e') continue;
/*  attnum 0 indicates that we need to prioritize blobs so
 *  that a Tuple can fit into a block, this is used by hio.c
 */            
            if ( attnum == SIZE_SPAN ) {
                if ( t_length < MaxTupleSize ) continue;
            }
/*  attnum -1 indicates that we need to prioritize blobs so
 *  that a tuple can be moved by vacuum within a relation
 *  only pick the blobs that are stored in the sam relation
 */             
            if ( attnum == LOC_SPAN ) {
                Oid storeid = InvalidOid;
                Relation storerel = find_storage_relation(rel,tuple,attnum);
                storeid = storerel->rd_id;
                RelationClose(storerel);
                if ( storeid != rel->rd_id ) continue;
            }
 /*  if attnum > 0 (valid attnum), only try and move that blob   */           
            if ( attnum > 0 && atts->attrs[c]->attnum != attnum ) continue;

                            
            blob_list      *item = (blob_list *) palloc(sizeof(blob_list));
            isNull = false;
            blob = HeapGetAttr(tuple, atts->attrs[c]->attnum, atts, &isNull);

            if (isNull)
                continue;

            item->attnum = atts->attrs[c]->attnum;
            item->data = blob;
            item->next = NULL;
            if (header == NULL) {
                    header = item;
            } else {
                    blob_list      *pointer = header;
                    blob_list      *prev = NULL;
                    while (pointer != NULL && VARSIZE(item->data) < VARSIZE(pointer->data)) {
                            prev = pointer;
                            pointer = pointer->next;
                    }
                    item->next = prev->next;
                    prev->next = item;
                    t_length -= (VARSIZE(item->data) - sizeof(blob_header));
            }
        }

	return header;
}

int
delete_tuple_blob(Relation rel, HeapTuple tuple, HeapTuple newtup)
{
    bool            isNull = false;
    TupleDesc       atts = rel->rd_att;
    Relation        storerel;
    int             c = 0;
    int             count =0;

    for (c = 0; c < atts->natts; c++) {
        if (atts->attrs[c]->attstorage == 'e') {
            Datum           blob = HeapGetAttr(tuple, atts->attrs[c]->attnum, atts, &isNull);
            if (!isNull && ISINDIRECT(blob)) {
                ItemPointerData    latest_data;
                blob_header     header;
                ItemPointer latest = &latest_data;
/*  if the tuples of old and new point to the same blob, don't erase it  */
                if ( newtup != NULL ) {
                    Datum           checkblob = HeapGetAttr(tuple, atts->attrs[c]->attnum, atts, &isNull);
                    if ( !isNull && ISINDIRECT(checkblob)) {
                        if ( memcmp(DatumGetPointer(blob),DatumGetPointer(checkblob),sizeof(blob_header)) == 0 ) {
                            continue;
                        }
                    }
                }

                memmove(&header,DatumGetPointer(blob),sizeof(blob_header));
                storerel = RelationIdGetRelation(header.relid,DEFAULTDBOID);
                LockRelation(storerel,AccessShareLock);
                ItemPointerCopy(&header.forward_pointer,latest);
                count += delete_blob_segments(storerel, &header.forward_pointer,(tuple->t_data->t_infomask & HEAP_MOVED_OUT));
                UnlockRelation(storerel,AccessShareLock);
                RelationClose(storerel);
            }
        }
    }
    return count;
}

int
delete_blob_segments(Relation rel, ItemPointer first, bool moved)
{
	int pos = 0;
        ItemPointerData link;
        
        ItemPointerCopy(first,&link);
        
	while (ItemPointerIsValid(&link)) {
            pos+=delete_segment(rel, &link, moved);
	}
        return pos;
}

Relation
find_storage_relation(Relation relation, HeapTuple tuple,int16 attnum) {
    bool isNull = false;
    Relation checkrel = RelationNameGetRelation(ExtStoreRelationName,DEFAULTDBOID);
    if ( RelationIsValid(checkrel) ) {
         HeapTuple storetuple = SearchSysCacheTuple(EXTSTORE,ObjectIdGetDatum(relation->rd_id),Int16GetDatum(attnum),0,0);
         if ( storetuple != NULL ) {
            Datum storeid = SysCacheGetAttr(EXTSTORE,storetuple,Anum_pg_extstore_extstore,&isNull);
            relation = RelationIdGetRelation(storeid,DEFAULTDBOID);
         } else {
            RelationIncrementReferenceCount(relation);
         }
         RelationClose(checkrel);
    } else {
         RelationIncrementReferenceCount(relation);
    }
     return relation;
}

bool
lock_segment_for_update(Relation relation, Buffer * buf, HeapTuple tuple) {
    
    int  result = LockHeapTupleForUpdate(relation, buf, tuple, SnapshotNow);
    
    bool valid = (result == HeapTupleMayBeUpdated);
    if ( !valid ) {
        if ( BufferIsValid(*buf) ) {
            UnlockHeapTuple(relation,*buf,tuple);
            ReleaseBuffer(relation,*buf);
            *buf = InvalidBuffer;
        }
    }
    
    return valid;
}

void 
unlock_segment(Relation relation, Buffer buf, HeapTuple tuple) {
    UnlockHeapTuple(relation,buf,tuple);
}

bool
vacuum_check_update_pointer(Relation relation, Buffer checkbuffer, HeapTuple check, ItemPointer forward)
{
        Buffer              buffer = InvalidBuffer;
        HeapTupleData       target;
        bool                valid = FALSE;
        bool                save = FALSE;
        bool                exit = FALSE;
        
        target.t_self = *forward;

        if ( !ItemPointerIsValid(forward) ) return FALSE;
 /*  internal tuple checks */       
        if ( ItemPointerEquals(&check->t_self,&target.t_self) ) return false;

	if ( check->t_data->t_infomask & HEAP_UPDATED ) {
            valid = TRUE;
        } else if ( check->t_data->t_infomask & HEAP_MARKED_FOR_UPDATE ) {
            if ( check->t_data->t_infomask & HEAP_XMAX_INVALID ) {
                valid = FALSE;
            } else if ( check->t_data->t_infomask & HEAP_XMAX_COMMITTED ) {
                valid = TRUE;
            } else if ( TransactionIdDidCommit(check->t_data->t_xmax) ) {
                check->t_data->t_infomask |= (HEAP_UPDATED);
                if ( TransactionIdDidHardCommit(check->t_data->t_xmax) ) {
                    check->t_data->t_infomask |= HEAP_XMAX_COMMITTED;
                }
                save = TRUE;
                valid = TRUE;
            } else if ( TransactionIdDidAbort(check->t_data->t_xmax) ) {
                check->t_data->t_infomask &= ~(HEAP_UPDATED);
                check->t_data->t_infomask |= HEAP_XMAX_INVALID;
                save = TRUE;
                valid = FALSE;
            }
	}

  /* open the forward tuple and see if it matches by brute force (crc) */
        if ( save ) WriteNoReleaseBuffer(relation,checkbuffer);
        return valid;
}


#ifdef NOTUSED
HeapTuple
rebuild_tuple_blob(Relation rel, HeapTuple tuple)
{
	TupleDesc       atts = rel->rd_att;

	int             c = 0;
	blob_header    *header;
	Datum          *values;
	char           *nulls;
	char           *replaces;
	bool            isNull = false;
	Datum           blob;
	HeapTuple       ret_tuple;
	MemoryContext   parent = MemoryContextGetCurrentContext();
	MemoryContext   blob_context = SubSetContextCreate(parent, "RebuildBlobContext");

	MemoryContextSwitchTo(blob_context);

	values = palloc(atts->natts * sizeof(Datum));
	nulls = palloc(atts->natts * sizeof(char));
	replaces = palloc(atts->natts * sizeof(char));

	for (c = 0; c < atts->natts; c++) {
		replaces[c] = ' ';
		nulls[c] = ' ';
		if (atts->attrs[c]->attstorage == 'e') {
			blob = HeapGetAttr(tuple, atts->attrs[c]->attnum, rel->rd_att, &isNull);
			if (!isNull && ISINDIRECT(blob)) {
				values[c] = PointerGetDatum(rebuild_indirect_blob(blob));
				replaces[c] = 'r';
			}
		}
	}

	MemoryContextSwitchTo(parent);

	ret_tuple = heap_modifytuple(tuple, rel, values, nulls, replaces);
        ret_tuple->t_data->t_infomask |= tuple->t_data->t_infomask;
	/* delete all the memory allocations */
	MemoryContextDelete(blob_context);
	tuple->t_data = ret_tuple->t_data;
	/*
	 * if this is a memory allocated tuple delete the old memory segment
	 */
	if (tuple->t_datasrc) {
		pfree(tuple->t_datasrc);
	}
	tuple->t_datasrc = ret_tuple;
        tuple->t_info = 0;
	tuple->t_len = ret_tuple->t_len;
        ItemPointerSetInvalid(&tuple->t_self);  /* memory tuple */

	tuple->t_data->t_infomask &= ~(HEAP_BLOBINDIRECT & HEAP_BLOBLINKED);

	return ret_tuple;
}
#endif

void  blob_log(Relation rel, char* pattern, ...) {
    char            msg[256];
    va_list         args;

    va_start(args, pattern);
    vsprintf(msg,pattern,args);
#ifdef SUNOS
    DTRACE_PROBE3(mtpg,blob__msg,msg,RelationGetRelid(rel),GetDatabaseId());  
#endif
#ifdef DEBUGLOGS
    elog(DEBUG,"blob: %d/%d %s",RelationGetRelid(rel),GetDatabaseId(),msg);
#endif
    va_end(args);
}


