/*-------------------------------------------------------------------------
 *
 * nodeUnique.h
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
#ifndef NODEUNIQUE_H
#define NODEUNIQUE_H

#include "nodes/plannodes.h"

PG_EXTERN TupleTableSlot *ExecUnique(Unique *node);
PG_EXTERN bool ExecInitUnique(Unique *node, EState *estate);
PG_EXTERN int	ExecCountSlotsUnique(Unique *node);
PG_EXTERN void ExecEndUnique(Unique *node);
PG_EXTERN void ExecReScanUnique(Unique *node, ExprContext *exprCtxt);

#endif	 /* NODEUNIQUE_H */
