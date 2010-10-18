/*-------------------------------------------------------------------------
 *
 * nodeTidscan.h
 *
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * $Id: nodeTidscan.h,v 1.1.1.1 2006/08/12 00:22:18 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */
#ifndef NODETIDSCAN_H
#define NODETIDSCAN_H

#include "nodes/plannodes.h"

PG_EXTERN TupleTableSlot *ExecTidScan(TidScan *node);
PG_EXTERN void ExecTidReScan(TidScan *node, ExprContext *exprCtxt);
PG_EXTERN void ExecEndTidScan(TidScan *node);
PG_EXTERN void ExecTidMarkPos(TidScan *node);
PG_EXTERN void ExecTidRestrPos(TidScan *node);
PG_EXTERN bool ExecInitTidScan(TidScan *node, EState *estate);
PG_EXTERN int	ExecCountSlotsTidScan(TidScan *node);
PG_EXTERN void ExecTidReScan(TidScan *node, ExprContext *exprCtxt);

#endif	 /* NODETIDSCAN_H */
