/*-------------------------------------------------------------------------
 *
 * nodeSeqscan.h
 *
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * $Id: nodeSeqscan.h,v 1.1.1.1 2006/08/12 00:22:18 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */
#ifndef NODESEQSCAN_H
#define NODESEQSCAN_H

#include "nodes/plannodes.h"

PG_EXTERN TupleTableSlot *ExecSeqScan(SeqScan *node);
PG_EXTERN bool ExecInitSeqScan(SeqScan *node, EState *estate);
PG_EXTERN int	ExecCountSlotsSeqScan(SeqScan *node);
PG_EXTERN void ExecEndSeqScan(SeqScan *node);
PG_EXTERN void ExecSeqReScan(SeqScan *node, ExprContext *exprCtxt);
PG_EXTERN void ExecSeqMarkPos(SeqScan *node);
PG_EXTERN void ExecSeqRestrPos(SeqScan *node);

#endif	 /* NODESEQSCAN_H */
