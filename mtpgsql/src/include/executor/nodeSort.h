/*-------------------------------------------------------------------------
 *
 * nodeSort.h
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
#ifndef NODESORT_H
#define NODESORT_H

#include "nodes/plannodes.h"

PG_EXTERN TupleTableSlot *ExecSort(Sort *node);
PG_EXTERN bool ExecInitSort(Sort *node, EState *estate);
PG_EXTERN int	ExecCountSlotsSort(Sort *node);
PG_EXTERN void ExecEndSort(Sort *node);
PG_EXTERN void ExecSortMarkPos(Sort *node);
PG_EXTERN void ExecSortRestrPos(Sort *node);
PG_EXTERN void ExecReScanSort(Sort *node, ExprContext *exprCtxt);

#endif	 /* NODESORT_H */
