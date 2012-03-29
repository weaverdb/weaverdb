/*-------------------------------------------------------------------------
 *
 * nodeDelegatedSeqscan.c
 *
 * IDENTIFICATION
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include "env/delegatedscan.h"
#include "access/heapam.h"
#include "executor/execdebug.h"
#include "executor/executor.h"
#include "executor/nodeDelegatedSeqscan.h"
#include "parser/parsetree.h"
#include "utils/mcxt.h"
#include "utils/relcache.h"

float          DelegatedBufferMax = 0.2;


typedef struct HeapScanArgs {
	Oid			relation;
	Snapshot		snapshot;
        ItemPointer             items;
        int                     counter;
        bool                    done;
} HeapScanArgs;


static Oid InitScanRelation(SeqScan *node, EState *estate, CommonScanState* scanstate, Plan *outerPlan);
static TupleTableSlot *DelegatedSeqNext(DelegatedSeqScan *node);
static void* DolHeapDelegation(Delegate args);
static int HeapPointerTranfer(Relation rel, void*  args);

static TupleTableSlot *
DelegatedSeqNext(DelegatedSeqScan *node)
{
	CommonScanState *   scanstate;
	EState	   *        estate;
	ScanDirection       direction;
	TupleTableSlot *    slot;
        Scan*               scan = &node->scan;
        bool                valid = true;
        ItemPointerData      item;

	/* ----------------
	 *	get information from the estate and scan state
	 * ----------------
	 */
	estate = scan->plan.state;
	scanstate = scan->scanstate;
	slot = scanstate->css_ScanTupleSlot;

	/*
	 * Check if we are evaluating PlanQual for tuple of this relation.
	 * Additional checking is not good, but no other way for now. We could
	 * introduce new nodes for this case and handle SeqScan --> NewNode
	 * switching in Init/ReScan plan...
	 */
	if (estate->es_evTuple != NULL &&
		estate->es_evTuple[scan->scanrelid - 1] != NULL)
	{
		ExecClearTuple(slot);
		if (estate->es_evTupleNull[scan->scanrelid - 1])
			return slot;		/* return empty slot */

		/* probably ought to use ExecStoreTuple here... */
		ExecStoreTuple(estate->es_evTuple[scan->scanrelid - 1],slot,false);

		/*
		 * Note that unlike IndexScan, SeqScan never use keys in
		 * heap_beginscan (and this is very bad) - so, here we do not
		 * check are keys ok or not.
		 */

		/* Flag for the next call that no more tuples */
		estate->es_evTupleNull[scan->scanrelid - 1] = true;
		return (slot);
	}

        
        valid = DelegatedScanNext(node->delegate,&item);
        ExecClearTuple(slot);

        if ( !valid ) {
 /*  we are done  */       
            return slot;
        }
 /*  grab the tuple from the pointer and store it in the slot */       
 /*  time qual checked by sub-thread  */
	DelegatedGetTuple(node->delegate,scanstate->css_currentRelation,NULL,slot,&item,&node->current);		
/* don't pfree this pointer */        
        return slot;
}

TupleTableSlot *
ExecDelegatedSeqScan(DelegatedSeqScan *node)
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
		slot = ExecScan(&node->scan, DelegatedSeqNext);

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
InitScanRelation(SeqScan *node, EState *estate,CommonScanState* scanstate, 
				  Plan *outerPlan)
{
	Index		relid;
	List	   *rangeTable;
	RangeTblEntry *rtentry;
	Oid			reloid;
	Relation	currentRelation;

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
		node->scanrelid = 0;
		scanstate->css_currentRelation = heap_open(reloid,AccessShareLock);
		scanstate->css_currentScanDesc = NULL;
		ExecAssignScanType(scanstate, RelationGetDescr(scanstate->css_currentRelation));
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
ExecInitDelegatedSeqScan(DelegatedSeqScan *node, EState *estate)
{
	CommonScanState*        scanstate;
	Plan*                   outerPlan;
	Oid			reloid;
	HeapScanArgs*			scan_args;
        SeqScan*                scan = &node->scan;

	/* ----------------
	 *	assign the node's execution state
	 * ----------------
	 */
	scan->plan.state = estate;

	/* ----------------
	 *	 create new CommonScanState for node
	 * ----------------
	 */
	scanstate = makeNode(CommonScanState);
	scan->scanstate = scanstate;

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

	reloid = InitScanRelation(&node->scan, estate, scanstate, outerPlan);

	scanstate->cstate.cs_TupFromTlist = false;

	/* ----------------
	 *	initialize tuple type
	 * ----------------
	 */
	ExecAssignResultTypeFromTL((Plan *) node, &scanstate->cstate);
	ExecAssignProjectionInfo((Plan *) node, &scanstate->cstate);

	scan_args = (HeapScanArgs*)palloc(sizeof(HeapScanArgs));
	scan_args->relation = reloid;
	scan_args->snapshot = estate->es_snapshot;
        scan_args->items = NULL;
        scan_args->counter = 0;
        scan_args->done = false;
	
        node->scanargs = scan_args;
	node->delegate = DelegatedScanStart(DolHeapDelegation,scan_args);
	node->current = InvalidBuffer;

	return TRUE;
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
ExecEndDelegatedSeqScan(DelegatedSeqScan *node)
{
	CommonScanState *scanstate;
	Plan	   *outerPlan;
	
	DelegatedScanEnd(node->delegate);

/*  free the scan arguments */	
	pfree(node->scanargs);		
        
	/* ----------------
	 *	get information from node
	 * ----------------
	 */
	scanstate = node->scan.scanstate;

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

        if ( BufferIsValid(node->current) ) {
            ReleaseBuffer(scanstate->css_currentRelation, node->current);
        }

	if ( scanstate->css_currentRelation != NULL ) {
		heap_close(scanstate->css_currentRelation,AccessShareLock);
		scanstate->css_currentRelation = NULL;
	}
        
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
 *		ExecSeqReScan
 *
 *		Rescans the relation.
 * ----------------------------------------------------------------
 */
void
ExecDelegatedSeqReScan(DelegatedSeqScan *dnode, ExprContext *exprCtxt)
{
	CommonScanState *scanstate;
	EState	   *estate;
	Plan	   *outerPlan;
	Relation	rel;
	HeapScanDesc scan;
	ScanDirection direction;
        SeqScan* node = &dnode->scan;

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
		HeapScanArgs* scan_args;
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
		
		scanstate->css_currentScanDesc = NULL;
/*  free scan args */
		pfree(dnode->scanargs);
		DelegatedScanEnd(dnode->delegate);
                
		ReleaseBuffer(rel, dnode->current);
		scan_args = palloc(sizeof(HeapScanArgs));
		scan_args->relation = rel->rd_id;
		scan_args->snapshot = estate->es_snapshot;
		dnode->delegate = DelegatedScanStart(DolHeapDelegation,scan_args);
	}
}

static void* 
DolHeapDelegation(Delegate arg) {
    HeapTuple  htup;
    BlockNumber blk = InvalidBlockNumber;
    int TransferMax = DelegatedGetTransferMax();
    int buf_count = 0;
    int start_blk = 0;
    BufferTrigger       trigger;


    HeapScanArgs* scan_args = (HeapScanArgs*)DelegatedScanArgs(arg);
    
    scan_args->items = palloc(sizeof(ItemPointerData) * TransferMax);
    scan_args->counter = 0;
    scan_args->done = false;
    
    trigger.when = TRIGGER_READ;
    trigger.call = (trigger_func)HeapPointerTranfer;
    trigger.args = arg;

    Relation rel = heap_open(scan_args->relation,AccessShareLock);

    HeapScanDesc scan = heap_beginscan(rel, scan_args->snapshot, 0, NULL);

    RelationSetTrigger(rel,&trigger);
    
    while ( !scan_args->done ) {
        htup = heap_getnext(scan);

        if ( HeapTupleIsValid(htup) ) {
            scan_args->items[scan_args->counter++] = htup->t_self;
            /*  
                check to see if this is a new block and record 
                if it is  
            */
            if ( ItemPointerGetBlockNumber(&htup->t_self) != blk ) {
                buf_count++;
                blk = ItemPointerGetBlockNumber(&htup->t_self);
                BiasBuffer(rel, scan->rs_cbuf);
            }
        } else {
            scan_args->done = true;
        }
        /* 
            if the counter is at the array limit
            or if we have pulled in more that
            10 % of the buffers go ahead and transfer the pointers
        */
       if (scan_args->done || (scan_args->counter == TransferMax) ) {
            if ( !DelegatedTransferPointers(arg,scan_args->items,scan_args->counter) ) {
                scan_args->done = true;
            }

            scan_args->counter = 0;
            buf_count = 0;
            start_blk = blk;
        }
    }

    RelationClearTrigger(rel);
   
    heap_endscan(scan);
    heap_close(rel,AccessShareLock);

    
    DelegatedDone(arg);
    
    pfree(scan_args->items);
    
    return NULL;
}

static int
HeapPointerTranfer(Relation rel,void*  args) {
    Delegate delegate = (Delegate)args;
    HeapScanArgs* scan_args = (HeapScanArgs*)DelegatedScanArgs(delegate);
    
    if ( DelegatedCollectorWaiting(delegate) && scan_args->counter > 0 ) {
        if ( !DelegatedTransferPointers(delegate,scan_args->items,scan_args->counter) ) {
            scan_args->done = true;
        }

        scan_args->counter = 0;
    }
    
    return 0;
}

/* ----------------------------------------------------------------
 *		ExecSeqMarkPos(node)
 *
 *		Marks scan position.
 * ----------------------------------------------------------------
 */
void
ExecDelegatedSeqMarkPos(DelegatedSeqScan *node)
{

	return;
}

/* ----------------------------------------------------------------
 *		ExecSeqRestrPos
 *
 *		Restores scan position.
 * ----------------------------------------------------------------
 */
void
ExecDelegatedSeqRestrPos(DelegatedSeqScan *node)
{

}

