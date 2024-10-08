/*-------------------------------------------------------------------------
 *
 * nodeAgg.h
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
#ifndef NODEAGG_H
#define NODEAGG_H

#include "nodes/plannodes.h"

PG_EXTERN TupleTableSlot *ExecAgg(Agg *node);
PG_EXTERN bool ExecInitAgg(Agg *node, EState *estate);
PG_EXTERN int	ExecCountSlotsAgg(Agg *node);
PG_EXTERN void ExecEndAgg(Agg *node);
PG_EXTERN void ExecReScanAgg(Agg *node, ExprContext *exprCtxt);

#endif	 /* NODEAGG_H */
