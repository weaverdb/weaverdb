/*-------------------------------------------------------------------------
 *
 * nodeAppend.h
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
#ifndef NODEAPPEND_H
#define NODEAPPEND_H

#include "nodes/plannodes.h"

PG_EXTERN bool ExecInitAppend(Append *node, EState *estate);
PG_EXTERN int	ExecCountSlotsAppend(Append *node);
PG_EXTERN TupleTableSlot *ExecProcAppend(Append *node);
PG_EXTERN void ExecEndAppend(Append *node);
PG_EXTERN void ExecReScanAppend(Append *node, ExprContext *exprCtxt);

#endif	 /* NODEAPPEND_H */
