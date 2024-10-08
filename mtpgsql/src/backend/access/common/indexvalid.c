/*-------------------------------------------------------------------------
 *
 * indexvalid.c
 *	  index tuple qualification validity checking code
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include "access/heapam.h"
#include "access/iqual.h"
#include "executor/execdebug.h"


/* ----------------
 *		index_keytest
 *
 * old comments
 *		May eventually combine with other tests (like timeranges)?
 *		Should have Buffer buffer; as an argument and pass it to amgetattr.
 * ----------------
 */
bool
index_keytest(IndexTuple tuple,
			  TupleDesc tupdesc,
			  int scanKeySize,
			  ScanKey key)
{
	bool		isNull;
	Datum		datum;
	int			test;

	IncrIndexProcessed();

	while (scanKeySize > 0)
	{
		datum = index_getattr(tuple,
							  key[0].sk_attno,
							  tupdesc,
							  &isNull);

		if (isNull)
		{
			/* XXX eventually should check if SK_ISNULL */
			return false;
		}

		if (key[0].sk_flags & SK_ISNULL)
			return false;

		if (key[0].sk_flags & SK_COMMUTE)
		{
			test = DatumGetChar(FMGR_PTR2(&key[0].sk_func,
				key[0].sk_argument,datum));
		}
		else
		{
			test = DatumGetChar(FMGR_PTR2(&key[0].sk_func,
				datum,key[0].sk_argument));
		}

		if (!test == !(key[0].sk_flags & SK_NEGATE))
			return false;

		scanKeySize -= 1;
		key++;
	}

	return true;
}
