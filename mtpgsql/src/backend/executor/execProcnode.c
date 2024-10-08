/*-------------------------------------------------------------------------
 *
 * execProcnode.c
 *	 contains dispatch functions which call the appropriate "initialize",
 *	 "get a tuple", and "cleanup" routines for the given node type.
 *	 If the node has children, then it will presumably call ExecInitNode,
 *	 ExecProcNode, or ExecEndNode on its subnodes and do the appropriate
 *	 processing..
 *
 * Portions Copyright (c) 2000-2024, Myron Scott  <myron@weaverdb.org>
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *
 *
 *-------------------------------------------------------------------------
 */
/*
 *	 INTERFACE ROUTINES
 *		ExecInitNode	-		initialize a plan node and its subplans
 *		ExecProcNode	-		get a tuple by executing the plan node
 *		ExecEndNode		-		shut down a plan node and its subplans
 *
 *	 NOTES
 *		This used to be three files.  It is now all combined into
 *		one file so that it is easier to keep ExecInitNode, ExecProcNode,
 *		and ExecEndNode in sync when new nodes are added.
 *
 *	 EXAMPLE
 *		suppose we want the age of the manager of the shoe department and
 *		the number of employees in that department.  so we have the query:
 *
 *				retrieve (DEPT.no_emps, EMP.age)
 *				where EMP.name = DEPT.mgr and
 *					  DEPT.name = "shoe"
 *
 *		Suppose the planner gives us the following plan:
 *
 *						Nest Loop (DEPT.mgr = EMP.name)
 *						/		\
 *					   /		 \
 *				   Seq Scan		Seq Scan
 *					DEPT		  EMP
 *				(name = "shoe")
 *
 *		ExecStart() is called first.
 *		It calls InitPlan() which calls ExecInitNode() on
 *		the root of the plan -- the nest loop node.
 *
 *	  * ExecInitNode() notices that it is looking at a nest loop and
 *		as the code below demonstrates, it calls ExecInitNestLoop().
 *		Eventually this calls ExecInitNode() on the right and left subplans
 *		and so forth until the entire plan is initialized.
 *
 *	  * Then when ExecRun() is called, it calls ExecutePlan() which
 *		calls ExecProcNode() repeatedly on the top node of the plan.
 *		Each time this happens, ExecProcNode() will end up calling
 *		ExecNestLoop(), which calls ExecProcNode() on its subplans.
 *		Each of these subplans is a sequential scan so ExecSeqScan() is
 *		called.  The slots returned by ExecSeqScan() may contain
 *		tuples which contain the attributes ExecNestLoop() uses to
 *		form the tuples it returns.
 *
 *	  * Eventually ExecSeqScan() stops returning tuples and the nest
 *		loop join ends.  Lastly, ExecEnd() calls ExecEndNode() which
 *		calls ExecEndNestLoop() which in turn calls ExecEndNode() on
 *		its subplans which result in ExecEndSeqScan().
 *
 *		This should show how the executor works by having
 *		ExecInitNode(), ExecProcNode() and ExecEndNode() dispatch
 *		their work to the appopriate node support routines which may
 *		in turn call these routines themselves on their subplans.
 *
 */

#include "postgres.h"
#include "env/env.h"
#include "executor/executor.h"
#include "executor/nodeAgg.h"
#include "executor/nodeAppend.h"
#include "executor/nodeGroup.h"
#include "executor/nodeHash.h"
#include "executor/nodeHashjoin.h"
#include "executor/nodeIndexscan.h"
#include "executor/nodeDelegatedIndexscan.h"
#include "executor/nodeTidscan.h"
#include "executor/nodeMaterial.h"
#include "executor/nodeMergejoin.h"
#include "executor/nodeNestloop.h"
#include "executor/nodeResult.h"
#include "executor/nodeSeqscan.h"
#include "executor/nodeDelegatedSeqscan.h"
#include "executor/nodeSort.h"
#include "executor/nodeSubplan.h"
#include "executor/nodeUnique.h"
#include "miscadmin.h"
#include "tcop/tcopprot.h"

/* ------------------------------------------------------------------------
 *		ExecInitNode
 *
 *		Recursively initializes all the nodes in the plan rooted
 *		at 'node'.
 *
 *		Initial States:
 *		  'node' is the plan produced by the query planner
 *
 *		returns TRUE/FALSE on whether the plan was successfully initialized
 * ------------------------------------------------------------------------
 */
bool
ExecInitNode(Plan *node, EState *estate)
{
	bool		result;
	List	   *subp;

	/* ----------------
	 *	do nothing when we get to the end
	 *	of a leaf on tree.
	 * ----------------
	 */
	if (node == NULL)
		return FALSE;

	foreach(subp, node->initPlan)
	{
		result = ExecInitSubPlan((SubPlan *) lfirst(subp), estate);
		if (result == FALSE)
			return FALSE;
	}
        DTRACE_PROBE1(mtpg,initplan,nodeTag(node));
	switch (nodeTag(node))
	{
			/* ----------------
			 *		control nodes
			 * ----------------
			 */
		case T_Result:
			result = ExecInitResult((Result *) node, estate);
			break;

		case T_Append:
			result = ExecInitAppend((Append *) node, estate);
			break;

			/* ----------------
			 *		scan nodes
			 * ----------------
			 */
		case T_SeqScan:
			result = ExecInitSeqScan((SeqScan *) node, estate);
			break;

		case T_DelegatedSeqScan:
			result = ExecInitDelegatedSeqScan((DelegatedSeqScan *) node, estate);
			break;

		case T_IndexScan:
			result = ExecInitIndexScan((IndexScan *) node, estate);
			break;

		case T_DelegatedIndexScan:
			result = ExecInitDelegatedIndexScan((DelegatedIndexScan *) node, estate);
			break;

			/* ----------------
			 *		join nodes
			 * ----------------
			 */
		case T_NestLoop:
			result = ExecInitNestLoop((NestLoop *) node, estate);
			break;

		case T_MergeJoin:
			result = ExecInitMergeJoin((MergeJoin *) node, estate);
			break;

			/* ----------------
			 *		materialization nodes
			 * ----------------
			 */
		case T_Material:
			result = ExecInitMaterial((Material *) node, estate);
			break;

		case T_Sort:
			result = ExecInitSort((Sort *) node, estate);
			break;

		case T_Unique:
			result = ExecInitUnique((Unique *) node, estate);
			break;

		case T_Group:
			result = ExecInitGroup((Group *) node, estate);
			break;

		case T_Agg:
			result = ExecInitAgg((Agg *) node, estate);
			break;

		case T_Hash:
			result = ExecInitHash((Hash *) node, estate);
			break;

		case T_HashJoin:
			result = ExecInitHashJoin((HashJoin *) node, estate);
			break;

		case T_TidScan:
			result = ExecInitTidScan((TidScan *) node, estate);
			break;

		default:
			elog(ERROR, "ExecInitNode: node %d unsupported", nodeTag(node));
			result = FALSE;
	}

	if (result != FALSE)
	{
		foreach(subp, node->subPlan)
		{
			result = ExecInitSubPlan((SubPlan *) lfirst(subp), estate);
			if (result == FALSE)
				return FALSE;
		}
	}

	return result;
}


/* ----------------------------------------------------------------
 *		ExecProcNode
 *
 *		Initial States:
 *		  the query tree must be initialized once by calling ExecInit.
 * ----------------------------------------------------------------
 */
TupleTableSlot *
ExecProcNode(Plan *node)
{
	TupleTableSlot *result;

	/* ----------------
	 *	deal with NULL nodes..
	 * ----------------
	 */

        if ( CheckForCancel() ) {
            elog(ERROR,"Query Cancelled");
        }

	if (node == NULL)
		return NULL;

	if (node->chgParam != NULL) /* something changed */
		ExecReScan(node, NULL); /* let ReScan handle this */

	switch (nodeTag(node))
	{
			/* ----------------
			 *	control nodes
			 * ----------------
			 */
		case T_Result:
			result = ExecResult((Result *) node);
			break;

		case T_Append:
			result = ExecProcAppend((Append *) node);
			break;

			/* ----------------
			 *		scan nodes
			 * ----------------
			 */
		case T_SeqScan:
			result = ExecSeqScan((SeqScan *) node);
			break;

		case T_DelegatedSeqScan:
			result = ExecDelegatedSeqScan((DelegatedSeqScan *) node);
			break;

		case T_IndexScan:
			result = ExecIndexScan((IndexScan *) node);
			break;

		case T_DelegatedIndexScan:
			result = ExecDelegatedIndexScan((DelegatedIndexScan *) node);
			break;
			/* ----------------
			 *		join nodes
			 * ----------------
			 */
		case T_NestLoop:
			result = ExecNestLoop((NestLoop *) node);
			break;

		case T_MergeJoin:
			result = ExecMergeJoin((MergeJoin *) node);
			break;

			/* ----------------
			 *		materialization nodes
			 * ----------------
			 */
		case T_Material:
			result = ExecMaterial((Material *) node);
			break;

		case T_Sort:
			result = ExecSort((Sort *) node);
			break;

		case T_Unique:
			result = ExecUnique((Unique *) node);
			break;

		case T_Group:
			result = ExecGroup((Group *) node);
			break;

		case T_Agg:
			result = ExecAgg((Agg *) node);
			break;

		case T_Hash:
			result = ExecHash((Hash *) node);
			break;

		case T_HashJoin:
			result = ExecHashJoin((HashJoin *) node);
			break;

		case T_TidScan:
			result = ExecTidScan((TidScan *) node);
			break;

		default:
			elog(ERROR, "ExecProcNode: node %d unsupported", nodeTag(node));
			result = NULL;
	}

	return result;
}

int
ExecCountSlotsNode(Plan *node)
{
	if (node == (Plan *) NULL)
		return 0;

	switch (nodeTag(node))
	{
			/* ----------------
			 *		control nodes
			 * ----------------
			 */
		case T_Result:
			return ExecCountSlotsResult((Result *) node);

		case T_Append:
			return ExecCountSlotsAppend((Append *) node);

			/* ----------------
			 *		scan nodes
			 * ----------------
			 */
		case T_SeqScan:
			return ExecCountSlotsSeqScan((SeqScan *) node);

		case T_DelegatedSeqScan:
			return ExecCountSlotsSeqScan(&((DelegatedSeqScan *) node)->scan);

		case T_IndexScan:
			return ExecCountSlotsIndexScan((IndexScan *) node);

		case T_DelegatedIndexScan:
			return ExecCountSlotsDelegatedIndexScan((DelegatedIndexScan *) node);
			/* ----------------
			 *		join nodes
			 * ----------------
			 */
		case T_NestLoop:
			return ExecCountSlotsNestLoop((NestLoop *) node);

		case T_MergeJoin:
			return ExecCountSlotsMergeJoin((MergeJoin *) node);

			/* ----------------
			 *		materialization nodes
			 * ----------------
			 */
		case T_Material:
			return ExecCountSlotsMaterial((Material *) node);

		case T_Sort:
			return ExecCountSlotsSort((Sort *) node);

		case T_Unique:
			return ExecCountSlotsUnique((Unique *) node);

		case T_Group:
			return ExecCountSlotsGroup((Group *) node);

		case T_Agg:
			return ExecCountSlotsAgg((Agg *) node);

		case T_Hash:
			return ExecCountSlotsHash((Hash *) node);

		case T_HashJoin:
			return ExecCountSlotsHashJoin((HashJoin *) node);

		case T_TidScan:
			return ExecCountSlotsTidScan((TidScan *) node);

		default:
			elog(ERROR, "ExecCountSlotsNode: node not yet supported: %d",
				 nodeTag(node));
			break;
	}
	return 0;
}

/* ----------------------------------------------------------------
 *		ExecEndNode
 *
 *		Recursively cleans up all the nodes in the plan rooted
 *		at 'node'.
 *
 *		After this operation, the query plan will not be able to
 *		processed any further.	This should be called only after
 *		the query plan has been fully executed.
 * ----------------------------------------------------------------
 */
void
ExecEndNode(Plan *node)
{
	List	   *subp;

	/* ----------------
	 *	do nothing when we get to the end
	 *	of a leaf on tree.
	 * ----------------
	 */
	if (node == NULL)
		return;

	foreach(subp, node->initPlan)
		ExecEndSubPlan((SubPlan *) lfirst(subp));
	foreach(subp, node->subPlan)
		ExecEndSubPlan((SubPlan *) lfirst(subp));
	if (node->chgParam != NULL)
	{
		freeList(node->chgParam);
		node->chgParam = NULL;
	}

	switch (nodeTag(node))
	{
			/* ----------------
			 *	control nodes
			 * ----------------
			 */
		case T_Result:
			ExecEndResult((Result *) node);
			break;

		case T_Append:
			ExecEndAppend((Append *) node);
			break;

			/* ----------------
			 *		scan nodes
			 * ----------------
			 */
		case T_SeqScan:
			ExecEndSeqScan((SeqScan *) node);
			break;

		case T_DelegatedSeqScan:
			ExecEndDelegatedSeqScan((DelegatedSeqScan *) node);
			break;

		case T_IndexScan:
			ExecEndIndexScan((IndexScan *) node);
			break;

		case T_DelegatedIndexScan:
			ExecEndDelegatedIndexScan((DelegatedIndexScan *) node);
			break;
			/* ----------------
			 *		join nodes
			 * ----------------
			 */
		case T_NestLoop:
			ExecEndNestLoop((NestLoop *) node);
			break;

		case T_MergeJoin:
			ExecEndMergeJoin((MergeJoin *) node);
			break;

			/* ----------------
			 *		materialization nodes
			 * ----------------
			 */
		case T_Material:
			ExecEndMaterial((Material *) node);
			break;

		case T_Sort:
			ExecEndSort((Sort *) node);
			break;

		case T_Unique:
			ExecEndUnique((Unique *) node);
			break;

		case T_Group:
			ExecEndGroup((Group *) node);
			break;

		case T_Agg:
			ExecEndAgg((Agg *) node);
			break;

			/* ----------------
			 *		XXX add hooks to these
			 * ----------------
			 */
		case T_Hash:
			ExecEndHash((Hash *) node);
			break;

		case T_HashJoin:
			ExecEndHashJoin((HashJoin *) node);
			break;

		case T_TidScan:
			ExecEndTidScan((TidScan *) node);
			break;

		default:
			elog(ERROR, "ExecEndNode: node %d unsupported", nodeTag(node));
			break;
	}
}
