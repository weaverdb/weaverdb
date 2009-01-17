/*-------------------------------------------------------------------------
 *
 * functions.h
 *
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * $Id: functions.h,v 1.1.1.1 2006/08/12 00:22:17 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */
#ifndef FUNCTIONS_H
#define FUNCTIONS_H

#include "nodes/parsenodes.h"
#include "utils/syscache.h"

PG_EXTERN Datum ProjectAttribute(TupleDesc TD, TargetEntry *tlist,
				 HeapTuple tup, bool *isnullP);

PG_EXTERN Datum postquel_function(Func *funcNode, char **args,
				  bool *isNull, bool *isDone);

#endif	 /* FUNCTIONS_H */
