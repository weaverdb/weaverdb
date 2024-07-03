/*
 * Copyright (c) 2024 Myron Scott <myron@weaverdb.org> All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 */

/*-------------------------------------------------------------------------
 *
 * nodeDelegatedSeqscan.h
 *
 *
 *-------------------------------------------------------------------------
 */
#ifndef NODEDELEGATEDSEQSCAN_H
#define NODEDELEGATEDSEQSCAN_H

#include "nodes/plannodes.h"

PG_EXTERN TupleTableSlot *ExecDelegatedSeqScan(DelegatedSeqScan *node);
PG_EXTERN bool ExecInitDelegatedSeqScan(DelegatedSeqScan *node, EState *estate);
PG_EXTERN void ExecEndDelegatedSeqScan(DelegatedSeqScan *node);
PG_EXTERN void ExecDelegatedSeqReScan(DelegatedSeqScan *node, ExprContext *exprCtxt);
PG_EXTERN void ExecDelegatedSeqMarkPos(DelegatedSeqScan *node);
PG_EXTERN void ExecDelegatedSeqRestrPos(DelegatedSeqScan *node);

#endif	 /* NODESEQSCAN_H */
