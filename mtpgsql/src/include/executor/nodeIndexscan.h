/*-------------------------------------------------------------------------
 *
 * nodeIndexscan.h
 *
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * $Id: nodeIndexscan.h,v 1.1.1.1 2006/08/12 00:22:18 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */
#ifndef NODEINDEXSCAN_H
#define NODEINDEXSCAN_H

#include "nodes/plannodes.h"

PG_EXTERN TupleTableSlot *ExecIndexScan(IndexScan *node);
PG_EXTERN void ExecIndexReScan(IndexScan *node, ExprContext *exprCtxt);
PG_EXTERN void ExecEndIndexScan(IndexScan *node);
PG_EXTERN void ExecIndexMarkPos(IndexScan *node);
PG_EXTERN void ExecIndexRestrPos(IndexScan *node);
PG_EXTERN void ExecUpdateIndexScanKeys(IndexScan *node, ExprContext *econtext);
PG_EXTERN bool ExecInitIndexScan(IndexScan *node, EState *estate);
PG_EXTERN int	ExecCountSlotsIndexScan(IndexScan *node);
PG_EXTERN void ExecIndexReScan(IndexScan *node, ExprContext *exprCtxt);

#endif	 /* NODEINDEXSCAN_H */
