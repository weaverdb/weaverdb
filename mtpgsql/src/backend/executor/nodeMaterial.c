/*-------------------------------------------------------------------------
 *
 * nodeMaterial.c
 *	  Routines to handle materialization nodes.
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
 * INTERFACE ROUTINES
 *		ExecMaterial			- generate a temporary relation
 *		ExecInitMaterial		- initialize node and subnodes..
 *		ExecEndMaterial			- shutdown node and subnodes
 *
 */
#include "postgres.h"

#include "env/env.h"
#include "access/heapam.h"
#include "catalog/heap.h"
#include "executor/executor.h"
#include "executor/nodeMaterial.h"
#include "optimizer/internal.h"

/* ----------------------------------------------------------------
 *		ExecMaterial
 *
 *		The first time this is called, ExecMaterial retrieves tuples
 *		from this node's outer subplan and inserts them into a temporary
 *		relation.  After this is done, a flag is set indicating that
 *		the subplan has been materialized.	Once the relation is
 *		materialized, the first tuple is then returned.  Successive
 *		calls to ExecMaterial return successive tuples from the temp
 *		relation.
 *
 *		Initial State:
 *
 *		ExecMaterial assumes the temporary relation has been
 *		created and opened by ExecInitMaterial during the prior
 *		InitPlan() phase.
 *
 * ----------------------------------------------------------------
 */
TupleTableSlot *				/* result tuple from subplan */
ExecMaterial(Material *node)
{
	EState	   *estate;
	MaterialState *matstate;
	Plan	   *outerNode;
	ScanDirection dir;
	Relation	tempRelation;
	Relation	currentRelation;
	HeapScanDesc currentScanDesc;
	HeapTuple	heapTuple;
	TupleTableSlot *slot;

	/* ----------------
	 *	get state info from node
	 * ----------------
	 */
	matstate = node->matstate;
	estate = node->plan.state;
	dir = estate->es_direction;

	/* ----------------
	 *	the first time we call this, we retrieve all tuples
	 *	from the subplan into a temporary relation and then
	 *	we sort the relation.  Subsequent calls return tuples
	 *	from the temporary relation.
	 * ----------------
	 */

	if (matstate->mat_Flag == false)
	{
		/* ----------------
		 *	set all relations to be scanned in the forward direction
		 *	while creating the temporary relation.
		 * ----------------
		 */
		estate->es_direction = ForwardScanDirection;

		/* ----------------
		 *	 if we couldn't create the temp relation then
		 *	 we print a warning and return NULL.
		 * ----------------
		 */
		tempRelation = matstate->mat_TempRelation;
		if (tempRelation == NULL)
		{
			elog(DEBUG, "ExecMaterial: temp relation is NULL! aborting...");
			return NULL;
		}

		/* ----------------
		 *	 retrieve tuples from the subplan and
		 *	 insert them in the temporary relation
		 * ----------------
		 */
		outerNode = outerPlan((Plan *) node);
		for (;;)
		{
			slot = ExecProcNode(outerNode);

			if (TupIsNull(slot))
				break;

			heap_insert(tempRelation, slot->val);

			ExecClearTuple(slot);
		}

		/* ----------------
		 *	 restore to user specified direction
		 * ----------------
		 */
		estate->es_direction = dir;

		/* ----------------
		 *	 now initialize the scan descriptor to scan the
		 *	 sorted relation and update the sortstate information
		 * ----------------
		 */
		currentRelation = tempRelation;
		currentScanDesc = heap_beginscan(currentRelation,		/* relation */
										 SnapshotSelf,	/* seeself */
										 0,		/* num scan keys */
										 NULL); /* scan keys */
		matstate->csstate.css_currentRelation = currentRelation;
		matstate->csstate.css_currentScanDesc = currentScanDesc;

		ExecAssignScanType(&matstate->csstate,
						   RelationGetDescr(currentRelation));

		/* ----------------
		 *	finally set the sorted flag to true
		 * ----------------
		 */
		matstate->mat_Flag = true;
	}

	/* ----------------
	 *	at this point we know we have a sorted relation so
	 *	we perform a simple scan on it with amgetnext()..
	 * ----------------
	 */
	currentScanDesc = matstate->csstate.css_currentScanDesc;

	heapTuple = heap_getnext(currentScanDesc);

	/* ----------------
	 *	put the tuple into the scan tuple slot and return the slot.
	 *	Note: since the tuple is really a pointer to a page, we don't want
	 *	to call pfree() on it..
	 * ----------------
	 */
	slot = (TupleTableSlot *) matstate->csstate.css_ScanTupleSlot;
        ExecClearTuple(slot);
	slot = ExecStoreTuple(heapTuple,slot,true);		/* don't pfree this pointer */
        return slot;
}

/* ----------------------------------------------------------------
 *		ExecInitMaterial
 * ----------------------------------------------------------------
 */
bool							/* initialization status */
ExecInitMaterial(Material *node, EState *estate)
{
	MaterialState *matstate;
	Plan	   *outerPlan;
	TupleDesc	tupType;
	Relation	tempDesc;

	/* int						len; */

	/* ----------------
	 *	assign the node's execution state
	 * ----------------
	 */
	node->plan.state = estate;

	/* ----------------
	 * create state structure
	 * ----------------
	 */
	matstate = makeNode(MaterialState);
	matstate->mat_Flag = false;
	matstate->mat_TempRelation = NULL;
	node->matstate = matstate;

	/* ----------------
	 *	Miscellanious initialization
	 *
	 *		 +	assign node's base_id
	 *		 +	assign debugging hooks and
	 *		 +	assign result tuple slot
	 *
	 *	Materialization nodes don't need ExprContexts because
	 *	they never call ExecQual or ExecTargetList.
	 * ----------------
	 */
	ExecAssignNodeBaseInfo(estate, &matstate->csstate.cstate);

#define MATERIAL_NSLOTS 1
	/* ----------------
	 * tuple table initialization
	 * ----------------
	 */
	ExecInitScanTupleSlot(estate, &matstate->csstate);

	/* ----------------
	 * initializes child nodes
	 * ----------------
	 */
	outerPlan = outerPlan((Plan *) node);
	ExecInitNode(outerPlan, estate);

	/* ----------------
	 *	initialize matstate information
	 * ----------------
	 */
	matstate->mat_Flag = false;

	/* ----------------
	 *	initialize tuple type.	no need to initialize projection
	 *	info because this node doesn't do projections.
	 * ----------------
	 */
	ExecAssignScanTypeFromOuterPlan((Plan *) node, &matstate->csstate);
	matstate->csstate.cstate.cs_ProjInfo = NULL;

	/* ----------------
	 *	get type information needed for ExecCreatR
	 * ----------------
	 */
	tupType = ExecGetScanType(&matstate->csstate);

	/* ----------------
	 *	ExecCreatR wants its second argument to be an object id of
	 *	a relation in the range table or a _NONAME_RELATION_ID
	 *	indicating that the relation is not in the range table.
	 *
	 *	In the second case ExecCreatR creates a temp relation.
	 *	(currently this is the only case we support -cim 10/16/89)
	 * ----------------
	 */
	/* ----------------
	 *	create the temporary relation
	 * ----------------
	 */
	tempDesc = ExecCreatR(tupType, _NONAME_RELATION_ID_);

	/* ----------------
	 *	save the relation descriptor in the sortstate
	 * ----------------
	 */
	matstate->mat_TempRelation = tempDesc;
	matstate->csstate.css_currentRelation = NULL;

	/* ----------------
	 *	return relation oid of temporary relation in a list
	 *	(someday -- for now we return LispTrue... cim 10/12/89)
	 * ----------------
	 */
	return TRUE;
}

int
ExecCountSlotsMaterial(Material *node)
{
	return ExecCountSlotsNode(outerPlan((Plan *) node)) +
	ExecCountSlotsNode(innerPlan((Plan *) node)) +
	MATERIAL_NSLOTS;
}

/* ----------------------------------------------------------------
 *		ExecEndMaterial
 *
 * old comments
 *		destroys the temporary relation.
 * ----------------------------------------------------------------
 */
void
ExecEndMaterial(Material *node)
{
	MaterialState *matstate;
	Relation	tempRelation;
	Plan	   *outerPlan;

	/* ----------------
	 *	get info from the material state
	 * ----------------
	 */
	matstate = node->matstate;
	tempRelation = matstate->mat_TempRelation;

	/* ----------------
	 *	shut down the scan, but don't close the temp relation
	 * ----------------
	 */
	matstate->csstate.css_currentRelation = NULL;
	ExecCloseR((Plan *) node);

	/* ----------------
	 *	shut down the subplan
	 * ----------------
	 */
	outerPlan = outerPlan((Plan *) node);
	ExecEndNode(outerPlan);


	/* ----------------
	 *	delete the temp relation
	 * ----------------
	 */
	if (tempRelation != NULL)
		heap_drop(tempRelation);
}

/* ----------------------------------------------------------------
 *		ExecMaterialReScan
 *
 *		Rescans the temporary relation.
 * ----------------------------------------------------------------
 */
void
ExecMaterialReScan(Material *node, ExprContext *exprCtxt)
{
	MaterialState *matstate = node->matstate;

	if (matstate->mat_Flag == false)
		return;

	matstate->csstate.css_currentScanDesc = ExecReScanR(matstate->csstate.css_currentRelation,
								   matstate->csstate.css_currentScanDesc,
								node->plan.state->es_direction, 0, NULL);

}

#ifdef NOT_USED					/* not used */
/* ----------------------------------------------------------------
 *		ExecMaterialMarkPos
 * ----------------------------------------------------------------
 */
List							/* nothing of interest */
ExecMaterialMarkPos(Material node)
{
	MaterialState matstate;
	HeapScanDesc scan;

	/* ----------------
	 *	if we haven't materialized yet, just return NIL.
	 * ----------------
	 */
	matstate = get_matstate(node);
	if (get_mat_Flag(matstate) == false)
		return NIL;

	/* ----------------
	 *	XXX access methods don't return positions yet so
	 *		for now we return NIL.	It's possible that
	 *		they will never return positions for all I know -cim 10/16/89
	 * ----------------
	 */
	scan = get_css_currentScanDesc((CommonScanState) matstate);
	heap_markpos(scan);

	return NIL;
}

/* ----------------------------------------------------------------
 *		ExecMaterialRestrPos
 * ----------------------------------------------------------------
 */
void
ExecMaterialRestrPos(Material node)
{
	MaterialState matstate;
	HeapScanDesc scan;

	/* ----------------
	 *	if we haven't materialized yet, just return.
	 * ----------------
	 */
	matstate = get_matstate(node);
	if (get_mat_Flag(matstate) == false)
		return;

	/* ----------------
	 *	restore the scan to the previously marked position
	 * ----------------
	 */
	scan = get_css_currentScanDesc((CommonScanState) matstate);
	heap_restrpos(scan);
}

#endif
