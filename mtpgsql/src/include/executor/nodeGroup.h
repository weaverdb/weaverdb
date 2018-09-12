/*-------------------------------------------------------------------------
 *
 * nodeGroup.h
 *	  prototypes for nodeGroup.c
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * $Id: nodeGroup.h,v 1.1.1.1 2006/08/12 00:22:18 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */
#ifndef NODEGROUP_H
#define NODEGROUP_H

#include "nodes/plannodes.h"

PG_EXTERN TupleTableSlot *ExecGroup(Group *node);
PG_EXTERN bool ExecInitGroup(Group *node, EState *estate);
PG_EXTERN int	ExecCountSlotsGroup(Group *node);
PG_EXTERN void ExecEndGroup(Group *node);
PG_EXTERN void ExecReScanGroup(Group *node, ExprContext *exprCtxt);

PG_EXTERN bool execTuplesMatch(HeapTuple tuple1,
				HeapTuple tuple2,
				TupleDesc tupdesc,
				int numCols,
				AttrNumber *matchColIdx,
				FmgrInfo *eqfunctions);
PG_EXTERN FmgrInfo *execTuplesMatchPrepare(TupleDesc tupdesc,
					   int numCols,
					   AttrNumber *matchColIdx);

#endif	 /* NODEGROUP_H */
