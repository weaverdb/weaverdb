/*-------------------------------------------------------------------------
 *
 * hio.h
 *	  POSTGRES heap access method input/output definitions.
 *
 *
 * Portions Copyright (c) 2000-2024, Myron Scott  <myron@weaverdb.org>
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 *
 *-------------------------------------------------------------------------
 */
#ifndef HIO_H
#define HIO_H

#include "access/htup.h"

#define TUPLE_LOCK_UNLOCK			BUFFER_LOCK_UNLOCK
#define TUPLE_LOCK_READ				BUFFER_LOCK_SHARE
#define TUPLE_LOCK_WRITE			BUFFER_LOCK_EXCLUSIVE
#define TUPLE_LOCK_VACUUM			BUFFER_LOCK_REF_EXCLUSIVE
#define TUPLE_LOCK_UPDATE			BUFFER_LOCK_READ_EXCLUSIVE


PG_EXTERN void RelationPutHeapTuple(Relation relation, Buffer buffer,
					 HeapTuple tuple);

PG_EXTERN BlockNumber RelationPutHeapTupleAtFreespace
		(Relation relation, HeapTuple tuple, BlockNumber limit);

PG_EXTERN Buffer
RelationGetHeapTuple(Relation rel, HeapTuple tuple);
PG_EXTERN Buffer
RelationGetHeapTupleWithBuffer(Relation rel, HeapTuple tuple, Buffer tagbuffer);
PG_EXTERN void
LockHeapTuple(Relation rel, Buffer buf, HeapTuple tuple, int mode);
PG_EXTERN int
LockHeapTupleForUpdate(Relation rel, Buffer * buf, HeapTuple tuple, Snapshot mode);
PG_EXTERN void
UnlockHeapTuple(Relation rel, Buffer buf, HeapTuple tuple);
#endif	 /* HIO_H */
