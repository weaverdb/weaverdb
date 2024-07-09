 /*-------------------------------------------------------------------------
 *
 * htup.h
 *	  POSTGRES heap tuple definitions.
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
#ifndef HTUP_H
#define HTUP_H

#include "storage/bufpage.h"


#define MinHeapTupleBitmapSize	32		/* 8 * 4 */

/* check these, they are likely to be more severely limited by t_hoff */

#define MaxHeapAttributeNumber	1600	/* 8 * 200 */

/*
 * to avoid wasting space, the attributes should be layed out in such a
 * way to reduce structure padding.
 */
typedef struct HeapTupleHeaderData
{
	Oid             t_oid;			/* OID of this tuple -- 4 bytes */
        union {
           TransactionId   t_vtran;
			struct {
            CommandId	t_cmin;			/* insert CID stamp -- 4 bytes each */
            CommandId	t_cmax;			/* delete CommandId stamp */
			} cmd;
        } progress;
        
	TransactionId t_xmin;		/* insert XID stamp -- 4 bytes each */
	TransactionId t_xmax;		/* delete XID stamp */

	ItemPointerData t_ctid;		/* current TID of this or newer tuple */

	int16		t_natts;		/* number of attributes */

	uint16		t_infomask;		/* various infos */

	uint8		t_hoff;			/* sizeof tuple header */

        bits8		t_bits[MinHeapTupleBitmapSize / 8];
	/* bit map of domains */

	/* MORE DATA FOLLOWS AT END OF STRUCT */
} HeapTupleHeaderData;

typedef HeapTupleHeaderData *HeapTupleHeader;

#define MinTupleSize	(MAXALIGN(sizeof (PageHeaderData)) + \
						 MAXALIGN(sizeof(HeapTupleHeaderData)) + \
						 MAXALIGN(sizeof(char)))

#define MaxTupleSize	(BLCKSZ - MinTupleSize)

#define MaxAttrSize		(MaxTupleSize - MAXALIGN(sizeof(HeapTupleHeaderData)))

#define SelfItemPointerAttributeNumber			(-1)
#define ObjectIdAttributeNumber					(-2)
#define MinTransactionIdAttributeNumber			(-3)
#define MinCommandIdAttributeNumber				(-4)
#define MaxTransactionIdAttributeNumber			(-5)
#define MaxCommandIdAttributeNumber				(-6)
#define MoveTransactionIdAttributeNumber		(-7)
#define FirstLowInvalidHeapAttributeNumber		(-8)


/* If you make any changes above, the order off offsets in this must change */
 extern  long heap_sysoffset[];

/*
 * This new HeapTuple for version >= 6.5 and this is why it was changed:
 *
 * 1. t_len moved off on-disk tuple data - ItemIdData is used to get len;
 * 2. t_ctid above is not self tuple TID now - it may point to
 *	  updated version of tuple (required by MVCC);
 * 3. someday someone let tuple to cross block boundaries -
 *	  he have to add something below...
 *
 * Change for 7.0:
 *	  Up to now t_data could be NULL, the memory location directly following
 *	  HeapTupleData or pointing into a buffer. Now, it could also point to
 *	  a separate allocation that was done in the t_datamcxt memory context.
 */
typedef struct HeapTupleData
{
	uint32              t_len;			/* length of *t_data */
	ItemPointerData     t_self;		/* SelfItemPointer */
	MemoryContext       t_datamcxt;	/* */
	void*               t_datasrc;		/* real datasource in the case of a reconstructed blob */
        int                 t_info;             /*  additional info about processing this HeapTuple  */
	HeapTupleHeader     t_data;		/* */
} HeapTupleData;

typedef HeapTupleData *HeapTuple;

#define HEAPTUPLESIZE	MAXALIGN(sizeof(HeapTupleData))


/* ----------------
 *		support macros
 * ----------------
 */
#define GETSTRUCT(TUP) (((char *)((HeapTuple)(TUP))->t_data) + \
						((HeapTuple)(TUP))->t_data->t_hoff)


/*
 * BITMAPLEN(NATTS) -
 *		Computes minimum size of bitmap given number of domains.
 */
#define BITMAPLEN(NATTS) \
		((((((int)(NATTS) - 1) >> 3) + 4 - (MinHeapTupleBitmapSize >> 3)) \
		  & ~03) + (MinHeapTupleBitmapSize >> 3))

/*
 * HeapTupleIsValid
 *		True iff the heap tuple is valid.
 */
#define HeapTupleIsValid(tuple) PointerIsValid(tuple)

/*  Tuple runtime info  */
#define TUPLE_HASINDIRECT               0x0001
#define TUPLE_HASBUFFERED               0x0002
#define TUPLE_READONLY                  0x0004
#define TUPLE_DIDHARDCOMMIT             0x0008
                  
/*
 * information stored in t_infomask:
 */
#define HEAP_HASNULL			0x0001	/* has null attribute(s) */
#define HEAP_HASVARLENA			0x0002	/* has variable length
										 * attribute(s) */
#define HEAP_BLOBINDIRECT		0x0004	/* blob is scattered in relation */
#define HEAP_BLOBDUPEDHEAD		0x0004	/* must be coupled with HEAP_BLOB_SEGMENT, means that the head is dupped but not picked up */
 /* attribute(s) */
#define HEAP_BLOBLINKED                 0x0008	/* blob is linked in a series */
#define HEAP_BLOBHEAD                   0x0008  /* must be coupled with HEAP_BLOB_SEGMENT, is the front of a blob stream  */
 /* attribute(s) */
#define HEAP_HASBLOB                    0x000C	/* the two above combined */

#define HEAP_MOVED_IN                   0x0010  	/*  vacuum moved tuple in */
#define HEAP_MOVED_OUT                  0x0020		/*  vacuum moved tuple out */
#define HEAP_FRAG_SCANNED               0x0040		/*  vacuum has seen tuple before */
#define HEAP_BLOB_SEGMENT               0x0080		/*  tuple is a section of a blob */

#define HEAP_XMIN_COMMITTED		0x0100	/* t_xmin committed */
#define HEAP_XMIN_INVALID		0x0200	/* t_xmin invalid/aborted */
#define HEAP_XMAX_COMMITTED		0x0400	/* t_xmax committed */
#define HEAP_XMAX_INVALID		0x0800	/* t_xmax invalid/aborted */
#define HEAP_MARKED_FOR_UPDATE          0x1000	/* marked for UPDATE */
#define HEAP_UPDATED			0x2000	/* this is UPDATEd version of row */


#define HEAP_XACT_MASK			0xFF00	/* */

#define HeapTupleNoNulls(tuple) \
		(!(((HeapTuple) (tuple))->t_data->t_infomask & HEAP_HASNULL))

#define HeapTupleAllFixed(tuple) \
		(!(((HeapTuple) (tuple))->t_data->t_infomask & HEAP_HASVARLENA))

#define HeapTupleHasIndirectBlob(tuple) \
		((((HeapTuple)(tuple))->t_data->t_infomask & HEAP_BLOBINDIRECT) != 0)

#define HeapTupleHasLinkBlob(tuple) \
		((((HeapTuple)(tuple))->t_data->t_infomask & HEAP_BLOBLINKED) != 0)

#define HeapTupleHasBlob(tuple) \
		((((HeapTuple)(tuple))->t_data->t_infomask & HEAP_HASBLOB) != 0)
#define HeapTupleHeaderHasBlob(tupleheader) \
		((((HeapTupleHeader)tupleheader)->t_infomask & HEAP_HASBLOB) != 0)
#endif	 /* HTUP_H */
