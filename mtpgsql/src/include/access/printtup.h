/*-------------------------------------------------------------------------
 *
 * printtup.h
 *
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
#ifndef PRINTTUP_H
#define PRINTTUP_H

#include "tcop/dest.h"

#ifdef __cplusplus
/* extern */ "C" {
#endif

/* extern */ DestReceiver *printtup_create_DR(bool);
/* extern */ void showatts(char *name, TupleDesc attinfo);
/* extern */ void debugtup(HeapTuple tuple, TupleDesc typeinfo,
		 DestReceiver *self);
/* extern */ void printtup_internal(HeapTuple tuple, TupleDesc typeinfo,
				  DestReceiver *self);

/* XXX this one is really in executor/spi.c */
/* extern */ void spi_printtup(HeapTuple tuple, TupleDesc tupdesc,
			 DestReceiver *self);

/* extern */ int	getTypeOutAndElem(Oid type, Oid *typOutput, Oid *typElem);
#ifdef __cplusplus
}
#endif

#endif	 /* PRINTTUP_H */
