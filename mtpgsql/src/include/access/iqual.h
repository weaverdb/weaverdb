/*-------------------------------------------------------------------------
 *
 * iqual.h
 *	  Index scan key qualification definitions.
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * $Id: iqual.h,v 1.1.1.1 2006/08/12 00:22:09 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */
#ifndef IQUAL_H
#define IQUAL_H

#include "access/itup.h"
#include "access/skey.h"


/* ----------------
 *		index tuple qualification support
 * ----------------
 */

/* extern */ int	NIndexTupleProcessed;

/* extern */ bool index_keytest(IndexTuple tuple, TupleDesc tupdesc,
			  int scanKeySize, ScanKey key);

#endif	 /* IQUAL_H */
