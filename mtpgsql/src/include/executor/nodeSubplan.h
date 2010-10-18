/*-------------------------------------------------------------------------
 *
 * nodeSubplan.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef NODESUBPLAN_H
#define NODESUBPLAN_H

#include "nodes/plannodes.h"

PG_EXTERN Datum ExecSubPlan(SubPlan *node, List *pvar, ExprContext *econtext,
			bool *isNull);
PG_EXTERN bool ExecInitSubPlan(SubPlan *node, EState *estate);
PG_EXTERN void ExecReScanSetParamPlan(SubPlan *node, Plan* upper);
PG_EXTERN void ExecSetParamPlan(SubPlan *node);
PG_EXTERN void ExecEndSubPlan(SubPlan *node);

#endif	 /* NODESUBPLAN_H */
