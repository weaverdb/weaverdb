/*-------------------------------------------------------------------------
 *
 * blobstorage.h
 *	  POSTGRES heap tuple definitions.
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * $Id: blobstorage.h,v 1.1.1.1 2006/08/12 00:22:08 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */
#ifndef BLOBSTORAGE_H
#define BLOBSTORAGE_H

#include "storage/itemptr.h"
#include "utils/rel.h"
#include "access/htup.h"

#define SIZE_SPAN 0  /*  span the blobs based on size, greatest to smallest  */
#define LOC_SPAN -1  /*  span the blobs based on location, only the ones that should be stored locally in the same relation  */

typedef struct blobindex {
    ItemPointerData   pointer;
    int32         length;
} BlobIndex;

Datum index_blob(Datum item);
Datum seek_blob(Relation rel, Datum blob, Datum index, uint32 seek);

int delete_tuple_blob(Relation rel, HeapTuple direct, HeapTuple newtup);
BlockNumber store_tuple_blob(Relation rel, HeapTuple direct, int16 attnum);
BlockNumber span_buffered_blob(Relation rel,HeapTuple direct);

HeapTuple vacuum_respan_tuple_blob(Relation rel, HeapTuple tuple, bool exclude_self);

Size sizeof_tuple_blob(Relation rel, HeapTuple tuple);
Size sizeof_max_tuple_blob();

bytea* rebuild_indirect_blob(Datum item);

Datum
open_read_pipeline_blob(Datum pointer, bool read_only);
Datum
open_write_pipeline_blob(Relation rel);
void 
close_read_pipeline_blob(Datum pipe);
Datum 
close_write_pipeline_blob(Datum pipe);
uint32 
sizeof_indirect_blob(Datum pipe);
bool 
read_pipeline_segment_blob(Datum pipe, char* target,int * length, int limit);
bool
write_pipeline_segment_blob(Datum pipe, bytea * data);

#endif  /* BLOBSTORAGE_H */
