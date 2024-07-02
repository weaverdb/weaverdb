/*-------------------------------------------------------------------------
 *
 * nodeResult.h
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
#ifndef NODERESULT_H
#define NODERESULT_H

#include "nodes/plannodes.h"

PG_EXTERN TupleTableSlot *ExecResult(Result *node);
PG_EXTERN bool ExecInitResult(Result *node, EState *estate);
PG_EXTERN int	ExecCountSlotsResult(Result *node);
PG_EXTERN void ExecEndResult(Result *node);
PG_EXTERN void ExecReScanResult(Result *node, ExprContext *exprCtxt);

#endif	 /* NODERESULT_H */
