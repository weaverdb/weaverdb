/*-------------------------------------------------------------------------
 *
 * nodeHashjoin.h
 *
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 *
 *-------------------------------------------------------------------------
 */
#ifndef NODEHASHJOIN_H
#define NODEHASHJOIN_H

#include "nodes/plannodes.h"
#include "storage/buffile.h"

PG_EXTERN TupleTableSlot *ExecHashJoin(HashJoin *node);
PG_EXTERN bool ExecInitHashJoin(HashJoin *node, EState *estate);
PG_EXTERN int	ExecCountSlotsHashJoin(HashJoin *node);
PG_EXTERN void ExecEndHashJoin(HashJoin *node);
PG_EXTERN void ExecHashJoinSaveTuple( HeapTuple heapTuple, BufFile *file);
PG_EXTERN void ExecReScanHashJoin(HashJoin *node, ExprContext *exprCtxt);

#endif	 /* NODEHASHJOIN_H */
