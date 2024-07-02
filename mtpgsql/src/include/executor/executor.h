/*-------------------------------------------------------------------------
 *
 * executor.h
 *	  support for the POSTGRES executor module
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 *
 *-------------------------------------------------------------------------
 */
#ifndef EXECUTOR_H
#define EXECUTOR_H

#include "executor/execdesc.h"

/* ----------------
 *		TupIsNull
 *
 *		This is used mainly to detect when there are no more
 *		tuples to process.
 * ----------------
 */
/* return: true if tuple in slot is NULL, slot is slot to test */
#define TupIsNull(slot) \
	((slot) == NULL || (slot)->val == NULL)

/*
 * prototypes from functions in execAmi.c
 */
 #ifdef __cplusplus
extern "C" {
 #endif
 
 
PG_EXTERN void ExecOpenScanR(Oid relOid, int nkeys, ScanKey skeys, bool isindex,
			  ScanDirection dir, Snapshot snapshot,
			  Relation *returnRelation, Pointer *returnScanDesc);
PG_EXTERN void ExecCloseR(Plan *node);
PG_EXTERN void ExecReScan(Plan *node, ExprContext *exprCtxt);
PG_EXTERN HeapScanDesc ExecReScanR(Relation relDesc, HeapScanDesc scanDesc,
			ScanDirection direction, int nkeys, ScanKey skeys);
PG_EXTERN void ExecMarkPos(Plan *node);
PG_EXTERN void ExecRestrPos(Plan *node);
PG_EXTERN Relation ExecCreatR(TupleDesc tupType, Oid relationOid);

/*
 * prototypes from functions in execJunk.c
 */
PG_EXTERN JunkFilter *ExecInitJunkFilter(List *targetList, TupleDesc tupType);
PG_EXTERN bool ExecGetJunkAttribute(JunkFilter *junkfilter, TupleTableSlot *slot,
					 char *attrName, Datum *value, bool *isNull);
PG_EXTERN HeapTuple ExecRemoveJunk(JunkFilter *junkfilter, TupleTableSlot *slot);


/*
 * prototypes from functions in execMain.c
 */
PG_EXTERN TupleDesc ExecutorStart(QueryDesc *queryDesc, EState *estate);
PG_EXTERN TupleTableSlot *ExecutorRun(QueryDesc *queryDesc, EState *estate,
			int feature, Node* start,Node* limcount);
PG_EXTERN void ExecutorEnd(QueryDesc *queryDesc, EState *estate);
PG_EXTERN void ExecConstraints(char *caller, Relation rel, HeapTuple tuple,
				EState *estate);


PG_EXTERN void ExecAppend(TupleTableSlot *slot, ItemPointer tupleid,
		   EState *estate);
PG_EXTERN int ExecPut(TupleTableSlot *slot, ItemPointer tupleid,
		   EState *estate);
PG_EXTERN void ExecDelete(TupleTableSlot *slot, ItemPointer tupleid,
		   EState *estate);
PG_EXTERN void ExecReplace(TupleTableSlot *slot, ItemPointer tupleid,
			EState *estate);

/*
 * prototypes from functions in execProcnode.c
 */
PG_EXTERN bool ExecInitNode(Plan *node, EState *estate);
PG_EXTERN TupleTableSlot *ExecProcNode(Plan *node);
PG_EXTERN int	ExecCountSlotsNode(Plan *node);
PG_EXTERN void ExecEndNode(Plan *node);

/*
 * prototypes from functions in execQual.c
 */
PG_EXTERN Datum ExecEvalVar(Var * variable, ExprContext * econtext, 
                        bool * isNull,bool * byval, int * length);

PG_EXTERN Datum ExecEvalParam(Param *expression, ExprContext *econtext,
			  bool *isNull);

PG_EXTERN Datum ExecEvalExpr(Node *expression, ExprContext *econtext, Oid* returnType, bool *isNull,
			 bool *isDone);
PG_EXTERN bool ExecQual(List *qual, ExprContext *econtext, bool resultForNull);
PG_EXTERN int	ExecTargetListLength(List *targetlist);
PG_EXTERN TupleTableSlot *ExecProject(ProjectionInfo *projInfo, bool *isDone);

/*
 * prototypes from functions in execScan.c
 */
PG_EXTERN TupleTableSlot *ExecScan(Scan *node, TupleTableSlot *(*accessMtd) ());

/*
 * prototypes from functions in execTuples.c
 */
PG_EXTERN TupleTable ExecCreateTupleTable(int initialSize);
PG_EXTERN void ExecDropTupleTable(TupleTable table, bool shouldFree);
PG_EXTERN TupleTableSlot *ExecAllocTableSlot(TupleTable table);
PG_EXTERN TupleTableSlot *ExecCreateTableSlot(void);
PG_EXTERN TupleTableSlot *ExecStoreTuple(HeapTuple tuple,TupleTableSlot *slot, bool transfer);
PG_EXTERN TupleTableSlot *ExecClearTuple(TupleTableSlot *slot);
PG_EXTERN TupleDesc ExecSetSlotDescriptor(TupleTableSlot *slot,
					  TupleDesc tupdesc);
PG_EXTERN void ExecSetSlotDescriptorIsNew(TupleTableSlot *slot, bool isNew);
PG_EXTERN void ExecInitResultTupleSlot(EState *estate, CommonState *commonstate);
PG_EXTERN void ExecInitScanTupleSlot(EState *estate,
					  CommonScanState *commonscanstate);
PG_EXTERN void ExecInitOuterTupleSlot(EState *estate, HashJoinState *hashstate);

PG_EXTERN TupleDesc ExecGetTupType(Plan *node);
PG_EXTERN TupleDesc ExecTypeFromTL(List *targetList);
PG_EXTERN void SetChangedParamList(Plan *node, List *newchg);

/*
 * prototypes from functions in execUtils.c
 */
PG_EXTERN void ResetTupleCount(void);
PG_EXTERN void ExecAssignNodeBaseInfo(EState *estate, CommonState *basenode);
PG_EXTERN void ExecAssignExprContext(EState *estate, CommonState *commonstate);
PG_EXTERN void ExecAssignResultType(CommonState *commonstate,
					 TupleDesc tupDesc);
PG_EXTERN void ExecAssignResultTypeFromOuterPlan(Plan *node,
								  CommonState *commonstate);
PG_EXTERN void ExecAssignResultTypeFromTL(Plan *node, CommonState *commonstate);
PG_EXTERN TupleDesc ExecGetResultType(CommonState *commonstate);
PG_EXTERN void ExecAssignProjectionInfo(Plan *node, CommonState *commonstate);
PG_EXTERN void ExecFreeProjectionInfo(CommonState *commonstate);
PG_EXTERN void ExecFreeExprContext(CommonState *commonstate);
PG_EXTERN void ExecFreeTypeInfo(CommonState *commonstate);
PG_EXTERN TupleDesc ExecGetScanType(CommonScanState *csstate);
PG_EXTERN void ExecAssignScanType(CommonScanState *csstate,
				   TupleDesc tupDesc);
PG_EXTERN void ExecAssignScanTypeFromOuterPlan(Plan *node,
								CommonScanState *csstate);
PG_EXTERN Form_pg_attribute ExecGetTypeInfo(Relation relDesc);

PG_EXTERN void ExecOpenIndices(Oid resultRelationOid,
				RelationInfo *resultRelationInfo);
PG_EXTERN void ExecCloseIndices(RelationInfo *resultRelationInfo);
PG_EXTERN void ExecInsertIndexTuples(TupleTableSlot *slot, ItemPointer tupleid,
					  EState *estate, bool is_put);
 #ifdef __cplusplus
}
 #endif

#endif	 /* EXECUTOR_H	*/
