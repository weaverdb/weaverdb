/*-------------------------------------------------------------------------
 *
 * functions.h
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
#ifndef FUNCTIONS_H
#define FUNCTIONS_H

#include "nodes/parsenodes.h"
#include "utils/syscache.h"

PG_EXTERN Datum ProjectAttribute(TupleDesc TD, TargetEntry *tlist,
				 HeapTuple tup, bool *isnullP);

PG_EXTERN Datum postquel_function(Func *funcNode, char **args,
				  bool *isNull, bool *isDone);

#endif	 /* FUNCTIONS_H */
