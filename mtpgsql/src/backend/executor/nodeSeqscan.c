/*-------------------------------------------------------------------------
 *
 * nodeSeqscan.c
 *	  Support routines for sequential scans of relations.
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
 *		ExecSeqScan				sequentially scans a relation.
 *		ExecSeqNext				retrieve next tuple in sequential order.
 *		ExecInitSeqScan			creates and initializes a seqscan node.
 *		ExecEndSeqScan			releases any storage allocated.
 *		ExecSeqReScan			rescans the relation
 *		ExecMarkPos				marks scan position
 *		ExecRestrPos			restores scan position
 *
 */
#include "postgres.h"
#include "env/env.h"
#include "access/heapam.h"
#include "executor/execdebug.h"
#include "executor/executor.h"
#include "executor/nodeSeqscan.h"
#include "parser/parsetree.h"

static Oid InitScanRelation(SeqScan *node, EState *estate,
				 CommonScanState *scanstate, Plan *outerPlan);

static TupleTableSlot *SeqNext(SeqScan *node);

/* ----------------------------------------------------------------
 *						Scan Support
 * ----------------------------------------------------------------
 */
/* ----------------------------------------------------------------
 *		SeqNext
 *
 *		This is a workhorse for ExecSeqScan
 * ----------------------------------------------------------------
 */
static TupleTableSlot *
SeqNext(SeqScan *node)
{
	HeapTuple	tuple;
	HeapScanDesc scandesc;
	CommonScanState *scanstate;
	EState	   *estate;
	TupleTableSlot *slot;

	/* ----------------
	 *	get information from the estate and scan state
	 * ----------------
	 */
	estate = node->plan.state;
	scanstate = node->scanstate;
	scandesc = scanstate->css_currentScanDesc;
	slot = scanstate->css_ScanTupleSlot;

	/*
	 * Check if we are evaluating PlanQual for tuple of this relation.
	 * Additional checking is not good, but no other way for now. We could
	 * introduce new nodes for this case and handle SeqScan --> NewNode
	 * switching in Init/ReScan plan...
	 */
	if (estate->es_evTuple != NULL &&
		estate->es_evTuple[node->scanrelid - 1] != NULL)
	{
		ExecClearTuple(slot);
		if (estate->es_evTupleNull[node->scanrelid - 1])
			return slot;		/* return empty slot */

		/* probably ought to use ExecStoreTuple here... */
		ExecStoreTuple(estate->es_evTuple[node->scanrelid - 1],slot,false);

		/*
		 * Note that unlike IndexScan, SeqScan never use keys in
		 * heap_beginscan (and this is very bad) - so, here we do not
		 * check are keys ok or not.
		 */

		/* Flag for the next call that no more tuples */
		estate->es_evTupleNull[node->scanrelid - 1] = true;
		return (slot);
	}

	/* ----------------
	 *	get the next tuple from the access methods
	 * ----------------
	 */
	tuple = heap_getnext(scandesc);

	/* ----------------
	 *	save the tuple and the buffer returned to us by the access methods
	 *	in our scan tuple slot and return the slot.  Note: we pass 'false'
	 *	because tuples returned by heap_getnext() are pointers onto
	 *	disk pages and were not created with palloc() and so should not
	 *	be pfree()'d.  Note also that ExecStoreTuple will increment the
	 *	refcount of the buffer; the refcount will not be dropped until
	 *	the tuple table slot is cleared.
	 * ----------------
	 */
         
        ExecClearTuple(slot);
	slot = ExecStoreTuple(tuple,slot,true);		/* don't pfree this pointer */
        return slot;
}

/* ----------------------------------------------------------------
 *		ExecSeqScan(node)
 *
 *		Scans the relation sequentially and returns the next qualifying
 *		tuple.
 *		It calls the ExecScan() routine and passes it the access method
 *		which retrieve tuples sequentially.
 *
 */

TupleTableSlot *
ExecSeqScan(SeqScan *node)
{
	TupleTableSlot *slot;
	Plan	   *outerPlan;
/*
	S_printf("ExecSeqScan: scanning node: ");
	S_nodeDisplay(node);
*/
	/* ----------------
	 * if there is an outer subplan, get a tuple from it
	 * else, scan the relation
	 * ----------------
	 */
	if ((outerPlan = outerPlan((Plan *) node)) != NULL)
		slot = ExecProcNode(outerPlan);
	else
		slot = ExecScan(node, SeqNext);

	S1_printf("ExecSeqScan: returned tuple slot: %d\n", slot);

	return slot;
}

/* ----------------------------------------------------------------
 *		InitScanRelation
 *
 *		This does the initialization for scan relations and
 *		subplans of scans.
 * ----------------------------------------------------------------
 */
static Oid
InitScanRelation(SeqScan *node, EState *estate,
				 CommonScanState *scanstate, Plan *outerPlan)
{
	Index		relid;
	List	   *rangeTable;
	RangeTblEntry *rtentry;
	Oid			reloid;
	ScanDirection direction;
	Relation	currentRelation;
	HeapScanDesc currentScanDesc;

	if (outerPlan == NULL)
	{
		/* ----------------
		 * if the outer node is nil then we are doing a simple
		 * sequential scan of a relation...
		 *
		 * get the relation object id from the relid'th entry
		 * in the range table, open that relation and initialize
		 * the scan state...
		 * ----------------
		 */
		relid = node->scanrelid;
		rangeTable = estate->es_range_table;
		rtentry = rt_fetch(relid, rangeTable);
		reloid = rtentry->relid;
		direction = estate->es_direction;

		ExecOpenScanR(reloid,	/* relation */
					  0,		/* nkeys */
					  NULL,		/* scan key */
					  0,		/* is index */
					  direction,/* scan direction */
					  estate->es_snapshot,
					  &currentRelation, /* return: rel desc */
					  (Pointer *) &currentScanDesc);	/* return: scan desc */

		scanstate->css_currentRelation = currentRelation;
		scanstate->css_currentScanDesc = currentScanDesc;

		ExecAssignScanType(scanstate,
						   RelationGetDescr(currentRelation));
	}
	else
	{
		/* ----------------
		 *	 otherwise we are scanning tuples from the
		 *	 outer subplan so we initialize the outer plan
		 *	 and nullify
		 * ----------------
		 */
		ExecInitNode(outerPlan, estate);

		node->scanrelid = 0;
		scanstate->css_currentRelation = NULL;
		scanstate->css_currentScanDesc = NULL;
		ExecAssignScanType(scanstate, NULL);
		reloid = InvalidOid;
	}

	/* ----------------
	 *	return the relation
	 * ----------------
	 */
	return reloid;
}


/* ----------------------------------------------------------------
 *		ExecInitSeqScan
 *
 * old comments
 *		Creates the run-time state information for the seqscan node
 *		and sets the relation id to contain relevant descriptors.
 *
 *		If there is a outer subtree (sort), the outer subtree
 *		is initialized and the relation id is set to the descriptors
 *		returned by the subtree.
 * ----------------------------------------------------------------
 */
bool
ExecInitSeqScan(SeqScan *node, EState *estate)
{
	CommonScanState *scanstate;
	Plan	   *outerPlan;

	/* ----------------
	 *	assign the node's execution state
	 * ----------------
	 */
	node->plan.state = estate;

	/* ----------------
	 *	 create new CommonScanState for node
	 * ----------------
	 */
	scanstate = makeNode(CommonScanState);
	node->scanstate = scanstate;

	/* ----------------
	 *	Miscellanious initialization
	 *
	 *		 +	assign node's base_id
	 *		 +	create expression context for node
	 * ----------------
	 */
	ExecAssignNodeBaseInfo(estate, &scanstate->cstate);
	ExecAssignExprContext(estate, &scanstate->cstate);

#define SEQSCAN_NSLOTS 3
	/* ----------------
	 *	tuple table initialization
	 * ----------------
	 */
	ExecInitResultTupleSlot(estate, &scanstate->cstate);
	ExecInitScanTupleSlot(estate, scanstate);

	/* ----------------
	 *	initialize scan relation or outer subplan
	 * ----------------
	 */
	outerPlan = outerPlan((Plan *) node);

	InitScanRelation(node, estate, scanstate, outerPlan);

	scanstate->cstate.cs_TupFromTlist = false;

	/* ----------------
	 *	initialize tuple type
	 * ----------------
	 */
	ExecAssignResultTypeFromTL((Plan *) node, &scanstate->cstate);
	ExecAssignProjectionInfo((Plan *) node, &scanstate->cstate);

	return TRUE;
}

int
ExecCountSlotsSeqScan(SeqScan *node)
{
	return ExecCountSlotsNode(outerPlan(node)) +
	ExecCountSlotsNode(innerPlan(node)) +
	SEQSCAN_NSLOTS;
}

/* ----------------------------------------------------------------
 *		ExecEndSeqScan
 *
 *		frees any storage allocated through C routines.
 *|		...and also closes relations and/or shuts down outer subplan
 *|		-cim 8/14/89
 * ----------------------------------------------------------------
 */
void
ExecEndSeqScan(SeqScan *node)
{
	CommonScanState *scanstate;
	Plan	   *outerPlan;

	/* ----------------
	 *	get information from node
	 * ----------------
	 */
	scanstate = node->scanstate;

	/* ----------------
	 *	Free the projection info and the scan attribute info
	 *
	 *	Note: we don't ExecFreeResultType(scanstate)
	 *		  because the rule manager depends on the tupType
	 *		  returned by ExecMain().  So for now, this
	 *		  is freed at end-transaction time.  -cim 6/2/91
	 * ----------------
	 */
	ExecFreeProjectionInfo(&scanstate->cstate);

	/* ----------------
	 * close scan relation
	 * ----------------
	 */
	ExecCloseR((Plan *) node);

	/* ----------------
	 * clean up outer subtree (does nothing if there is no outerPlan)
	 * ----------------
	 */
	outerPlan = outerPlan((Plan *) node);
	ExecEndNode(outerPlan);

	/* ----------------
	 *	clean out the tuple table
	 * ----------------
	 */
	ExecClearTuple(scanstate->cstate.cs_ResultTupleSlot);
	ExecClearTuple(scanstate->css_ScanTupleSlot);
}

/* ----------------------------------------------------------------
 *						Join Support
 * ----------------------------------------------------------------
 */
/* ----------------------------------------------------------------
 *		ExecSeqReScan
 *
 *		Rescans the relation.
 * ----------------------------------------------------------------
 */
void
ExecSeqReScan(SeqScan *node, ExprContext *exprCtxt)
{
	CommonScanState *scanstate;
	EState	   *estate;
	Plan	   *outerPlan;
	Relation	rel;
	HeapScanDesc scan;
	ScanDirection direction;

	scanstate = node->scanstate;
	estate = node->plan.state;

	if ((outerPlan = outerPlan((Plan *) node)) != NULL)
	{
		/* we are scanning a subplan */
		outerPlan = outerPlan((Plan *) node);
		ExecReScan(outerPlan, exprCtxt);
	}
	else
/* otherwise, we are scanning a relation */
	{
		/* If this is re-scanning of PlanQual ... */
		if (estate->es_evTuple != NULL &&
			estate->es_evTuple[node->scanrelid - 1] != NULL)
		{
			estate->es_evTupleNull[node->scanrelid - 1] = false;
			return;
		}
		rel = scanstate->css_currentRelation;
		scan = scanstate->css_currentScanDesc;
		direction = estate->es_direction;
		scan = ExecReScanR(rel, scan, direction, 0, NULL);
		scanstate->css_currentScanDesc = scan;
	}
}

/* ----------------------------------------------------------------
 *		ExecSeqMarkPos(node)
 *
 *		Marks scan position.
 * ----------------------------------------------------------------
 */
void
ExecSeqMarkPos(SeqScan *node)
{
	CommonScanState *scanstate;
	Plan	   *outerPlan;
	HeapScanDesc scan;

	scanstate = node->scanstate;

	/* ----------------
	 *	if we are scanning a subplan then propagate
	 *	the ExecMarkPos() request to the subplan
	 * ----------------
	 */
	outerPlan = outerPlan((Plan *) node);
	if (outerPlan)
	{
		ExecMarkPos(outerPlan);
		return;
	}

	/* ----------------
	 *	otherwise we are scanning a relation so mark the
	 *	position using the access methods..
	 *
	 * ----------------
	 */
	scan = scanstate->css_currentScanDesc;
	heap_markpos(scan);

	return;
}

/* ----------------------------------------------------------------
 *		ExecSeqRestrPos
 *
 *		Restores scan position.
 * ----------------------------------------------------------------
 */
void
ExecSeqRestrPos(SeqScan *node)
{
	CommonScanState *scanstate;
	Plan	   *outerPlan;
	HeapScanDesc scan;

	scanstate = node->scanstate;

	/* ----------------
	 *	if we are scanning a subplan then propagate
	 *	the ExecRestrPos() request to the subplan
	 * ----------------
	 */
	outerPlan = outerPlan((Plan *) node);
	if (outerPlan)
	{
		ExecRestrPos(outerPlan);
		return;
	}

	/* ----------------
	 *	otherwise we are scanning a relation so restore the
	 *	position using the access methods..
	 * ----------------
	 */
	scan = scanstate->css_currentScanDesc;
	heap_restrpos(scan);
}
