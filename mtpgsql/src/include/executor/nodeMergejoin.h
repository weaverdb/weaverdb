/*-------------------------------------------------------------------------
 *
 * nodeMergejoin.h
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
#ifndef NODEMERGEJOIN_H
#define NODEMERGEJOIN_H

#include "nodes/plannodes.h"

PG_EXTERN TupleTableSlot *ExecMergeJoin(MergeJoin *node);
PG_EXTERN bool ExecInitMergeJoin(MergeJoin *node, EState *estate);
PG_EXTERN int	ExecCountSlotsMergeJoin(MergeJoin *node);
PG_EXTERN void ExecEndMergeJoin(MergeJoin *node);
PG_EXTERN void ExecReScanMergeJoin(MergeJoin *node, ExprContext *exprCtxt);

#endif	 /* NODEMERGEJOIN_H; */
