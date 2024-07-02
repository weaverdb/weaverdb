/*-------------------------------------------------------------------------
 *
 * tuptable.h
 *	  tuple table support stuff
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 *
 * NOTES
 *	  The tuple table interface is getting pretty ugly.
 *	  It should be redesigned soon.
 *
 *-------------------------------------------------------------------------
 */
#ifndef TUPTABLE_H
#define TUPTABLE_H

#include "access/htup.h"

/* ----------------
 *		The executor tuple table is managed and manipulated by special
 *		code in executor/execTuples.c and tupTable.h
 *
 *		TupleTableSlot information
 *
 *			shouldFree			boolean - should we call pfree() on tuple
 *			descIsNew			boolean - true when tupleDescriptor changes
 *			tupleDescriptor		type information kept regarding the tuple data
 *			buffer				the buffer for tuples pointing to disk pages
 *
 *		The executor stores pointers to tuples in a ``tuple table''
 *		which is composed of TupleTableSlot's.  Some of the tuples
 *		are pointers to buffer pages and others are pointers to
 *		palloc'ed memory and the shouldFree variable tells us when
 *		we may call pfree() on a tuple.  -cim 9/23/90
 *
 *		In the implementation of nested-dot queries such as
 *		"retrieve (EMP.hobbies.all)", a single scan may return tuples
 *		of many types, so now we return pointers to tuple descriptors
 *		along with tuples returned via the tuple table.  -cim 1/18/90
 *
 *		Tuple table macros are all excised from the system now.
 *		See executor.h for decls of functions defined in execTuples.c
 *		-jolly
 *
 * ----------------
 */
typedef struct TupleTableSlot
{
	NodeTag		type;
	HeapTuple	val;
	TupleDesc	ttc_tupleDescriptor;
        MemoryContext   ttc_cxt;
	bool		ttc_descIsNew;
	bool            ttc_shouldfree;
	int		ttc_whichplan;
} TupleTableSlot;

/* ----------------
 *		tuple table data structure
 * ----------------
 */
typedef struct TupleTableData
{
	int			size;			/* size of the table */
	int			next;			/* next available slot number */
	TupleTableSlot *array;		/* array of TupleTableSlot's */
        MemoryContext           cxt;
} TupleTableData;

typedef TupleTableData *TupleTable;

#endif	 /* TUPTABLE_H */
