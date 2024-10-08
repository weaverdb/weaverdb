/*-------------------------------------------------------------------------
 *
 * nodeNestloop.h
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
#ifndef NODENESTLOOP_H
#define NODENESTLOOP_H

#include "nodes/plannodes.h"

PG_EXTERN TupleTableSlot *ExecNestLoop(NestLoop *node);
PG_EXTERN bool ExecInitNestLoop(NestLoop *node, EState *estate);
PG_EXTERN int	ExecCountSlotsNestLoop(NestLoop *node);
PG_EXTERN void ExecEndNestLoop(NestLoop *node);
PG_EXTERN void ExecReScanNestLoop(NestLoop *node, ExprContext *exprCtxt);

#endif	 /* NODENESTLOOP_H */
