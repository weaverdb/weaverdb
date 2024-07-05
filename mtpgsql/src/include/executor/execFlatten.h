/*-------------------------------------------------------------------------
 *
 * execFlatten.h
 *	  prototypes for execFlatten.c.
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
#ifndef EXECFLATTEN_H
#define EXECFLATTEN_H

#include "nodes/execnodes.h"
#include "nodes/relation.h"

PG_EXTERN Datum ExecEvalIter(Iter *iterNode, ExprContext *econtext, bool *resultIsNull, bool *iterIsDone);

PG_EXTERN void ExecEvalFjoin(TargetEntry *tlist, ExprContext *econtext, bool *isNullVect, bool *fj_isDone);


#endif	 /* EXECFLATTEN_H */
