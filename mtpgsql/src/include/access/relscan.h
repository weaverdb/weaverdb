/*-------------------------------------------------------------------------
 *
 * relscan.h
 *	  POSTGRES internal relation scan descriptor definitions.
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 *
 *-------------------------------------------------------------------------
 */
#ifndef RELSCAN_H
#define RELSCAN_H

#include "utils/tqual.h"

typedef ItemPointerData MarkData;

typedef struct HeapScanDescData
{
	Relation	rs_rd;			/* pointer to relation descriptor */
	HeapTupleData rs_ctup;		/* current tuple in scan */
	Buffer		rs_cbuf;		/* current buffer in scan */
	ItemPointerData rs_mctid;	/* marked current tid */
	ItemPointerData rs_mcd;		/* marked current delta XXX ??? */
	Snapshot	rs_snapshot;	/* snapshot to see */
	uint16		rs_cdelta;		/* current delta in chain */
	uint16		rs_nkeys;		/* number of attributes in keys */
	ScanKey		rs_key;			/* key descriptors */
} HeapScanDescData;

typedef HeapScanDescData *HeapScanDesc;

typedef struct IndexScanDescData
{
	Relation	relation;		/* relation descriptor */
	Relation	heapRelation;		/* relation descriptor */
	void	   	*opaque;			/* am-specific slot */

	ItemPointerData currentItemData;	/* current index pointer */
	MarkData	currentMarkData;/* marked current  pointer */

	uint8		flags;			/* scan position flags */
	bool		scanFromEnd;	/* restart scan at end? */
	uint16		numberOfKeys;	/* number of key attributes */
	ScanKey		keyData;		/* key descriptor */
	
        HeapTupleData 	xs_ctup;		/* current heap tuple, if any */
        
        FmgrInfo	fn_getnext;		/* cached lookup info for am's getnext fn */
        bool		keys_are_unique;
} IndexScanDescData;

typedef IndexScanDescData *IndexScanDesc;

/* ----------------
 *		IndexScanDescPtr is used in the executor where we have to
 *		keep track of several index scans when using several indices
 *		- cim 9/10/89
 * ----------------
 */
typedef IndexScanDesc *IndexScanDescPtr;

/*
 * HeapScanIsValid
 *		True iff the heap scan is valid.
 */
#define HeapScanIsValid(scan) PointerIsValid(scan)

/*
 * IndexScanIsValid
 *		True iff the index scan is valid.
 */
#define IndexScanIsValid(scan) PointerIsValid(scan)

#endif	 /* RELSCAN_H */
