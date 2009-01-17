/*-------------------------------------------------------------------------
 *
 * nodeDelegatedIndexscan.h
 *
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * $Id: nodeDelegatedIndexscan.h,v 1.1.1.1 2006/08/12 00:22:18 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */
#ifndef NODEDELEGATEDINDEXSCAN_H
#define NODEDELEGATEDINDEXSCAN_H

#include "nodes/plannodes.h"

PG_EXTERN TupleTableSlot *ExecDelegatedIndexScan(DelegatedIndexScan *node);
PG_EXTERN void ExecDelegatedIndexReScan(DelegatedIndexScan *node, ExprContext *exprCtxt);
PG_EXTERN void ExecEndDelegatedIndexScan(DelegatedIndexScan *node);
PG_EXTERN void ExecDelegatedIndexMarkPos(DelegatedIndexScan *node);
PG_EXTERN void ExecDelegatedIndexRestrPos(DelegatedIndexScan *node);
PG_EXTERN bool ExecInitDelegatedIndexScan(DelegatedIndexScan *node, EState *estate);
PG_EXTERN int	ExecCountSlotsDelegatedIndexScan(DelegatedIndexScan *node);
PG_EXTERN void ExecDelegatedIndexReScan(DelegatedIndexScan *node, ExprContext *exprCtxt);

#endif	 /* NODEDELEGATEDINDEXSCAN_H */
