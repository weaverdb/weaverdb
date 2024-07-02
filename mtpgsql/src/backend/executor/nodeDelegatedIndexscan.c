/*-------------------------------------------------------------------------
 *
 * nodeDelegatedIndexscan.c
 *	  Routines to support indexes and indexed scans of relations
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *
 *
 *-------------------------------------------------------------------------
 */


#include "postgres.h"

#include "access/genam.h"
#include "access/heapam.h"
#include "executor/execdebug.h"
#include "executor/executor.h"
#include "executor/nodeDelegatedIndexscan.h"
#include "nodes/nodeFuncs.h"
#include "optimizer/clauses.h"
#include "parser/parsetree.h"
#include "utils/relcache.h"
/* ----------------
 *		Misc stuff to move to executor.h soon -cim 6/5/90
 * ----------------
 */
#define NO_OP			0
#define LEFT_OP			1
#define RIGHT_OP		2

typedef struct IndexScanArgs {
    Oid         heap;
    Oid         index;
    ScanKey     scankey;
    int         keycount;
    int         counter;
    ScanDirection   dir;
    ItemPointer     items;
    bool        done;
    bool        ordered;
} IndexScanArgs;

static TupleTableSlot *DelegatedIndexNext(DelegatedIndexScan *node);
static ScanKey BuildScanKey(List* indxqual, ExprContext* expr_cxt);
static void* DolIndexDelegation(Delegate arg);
static int IndexPointerTranfer(Relation rel, void*  args);

static int compare_blocks(ItemPointer c1, ItemPointer c2);

typedef int (*comparator)(const void*,const void*);

static TupleTableSlot *
DelegatedIndexNext(DelegatedIndexScan *node)
{
	CommonScanState *   scanstate;
	EState	   *        estate;
	TupleTableSlot *    slot;
        Scan*               scan = &node->scan;
        bool                valid = true;
        ItemPointerData      item;
        bool                timevalid = false;

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

        ExecClearTuple(slot);

        do {
            valid = DelegatedScanNext(node->delegate,&item);

            if ( !valid ) {
     /*  we are done  */       
                return slot;
            }
     /*  grab the tuple from the pointer and store it in the slot */   
            timevalid = DelegatedGetTuple(node->delegate,scanstate->css_currentRelation,
                    estate->es_snapshot,slot,&item,&node->current);
            if ( timevalid ) {
                return slot;
            }
        } while ( !timevalid && valid );

        return slot;
}

/* ----------------------------------------------------------------
 *		ExecDelegatedIndexScan(node)
 *
 * old comments:
 *		Scans the relation using primary or secondary indices and returns
 *		   the next qualifying tuple in the direction specified.
 *		It calls ExecScan() and passes it the access methods which returns
 *		the next tuple using the indices.
 *
 *		Conditions:
 *		  -- the "cursor" maintained by the AMI is positioned at the tuple
 *			 returned previously.
 *
 *		Initial States:
 *		  -- the relation indicated is opened for scanning so that the
 *			 "cursor" is positioned before the first qualifying tuple.
 *		  -- all index realtions are opened for scanning.
 *		  -- indexPtr points to the first index.
 *		  -- state variable ruleFlag = nil.
 * ----------------------------------------------------------------
 */
TupleTableSlot *
ExecDelegatedIndexScan(DelegatedIndexScan *node)
{
	/* ----------------
	 *	use IndexNext as access method
	 * ----------------
	 */
	return ExecScan((Scan*)node, DelegatedIndexNext);
}

/* ----------------------------------------------------------------
 *		ExecDelegatedIndexReScan(node)
 *
 *		Recalculates the value of the scan keys whose value depends on
 *		information known at runtime and rescans the indexed relation.
 *		Updating the scan key was formerly done separately in
 *		ExecUpdateIndexScanKeys. Integrating it into ReScan
 *		makes rescans of indices and
 *		relations/general streams more uniform.
 *
 * ----------------------------------------------------------------
 */
void
ExecDelegatedIndexReScan(DelegatedIndexScan *node, ExprContext *exprCtxt)
{
    elog(ERROR,"delegated indx rescan not implemented");
}

void
ExecEndDelegatedIndexScan(DelegatedIndexScan *node)
{
	CommonScanState *scanstate;
	Plan	   *outerPlan;
	
/*  free the scan arguments */	
	DelegatedScanEnd(node->delegate);
        
        pfree(((IndexScanArgs*)node->scanargs)->scankey);
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
 *		ExecDelegatedIndexMarkPos
 *
 * old comments
 *		Marks scan position by marking the current index.
 *		Returns nothing.
 * ----------------------------------------------------------------
 */
void
ExecDelegatedIndexMarkPos(DelegatedIndexScan *node)
{
    elog(ERROR,"delegated mark position not implemented");
}


void
ExecDelegatedIndexRestrPos(DelegatedIndexScan *node)
{
    elog(ERROR,"delegated restore position not implemented");
}

static ScanKey
BuildScanKey(List* indxqual, ExprContext* expr_cxt) {
        int             j;
        List	   *    qual;
        int             n_keys;
        ScanKey		scan_keys;

        qual = lfirst(indxqual);
        indxqual = lnext(indxqual);
        n_keys = length(qual);
        scan_keys = (n_keys <= 0) ? NULL :
                (ScanKey) palloc(n_keys * sizeof(ScanKeyData));

        /* ----------------
         *	for each opclause in the given qual,
         *	convert each qual's opclause into a single scan key
         * ----------------
         */
        for (j = 0; j < n_keys; j++)
        {
                Expr	   *clause; /* one clause of index qual */
                Oper	   *op;		/* operator used in clause */
                Node	   *leftop; /* expr on lhs of operator */
                Node	   *rightop;/* expr on rhs ... */
                bits16		flags = 0;

                int			scanvar;/* which var identifies varattno */
                AttrNumber	varattno = 0;	/* att number used in scan */
                Oid			opid;	/* operator id used in scan */
                Datum		scanvalue = 0;	/* value used in scan (if const) */

                /* ----------------
                 *	extract clause information from the qualification
                 * ----------------
                 */
                clause = nth(j, qual);

                op = (Oper *) clause->oper;
                if (!IsA(clause, Expr) ||!IsA(op, Oper))
                        elog(ERROR, "ExecInitDelegatedIndexScan: BuildScanKey: indxqual not an opclause!");

                opid = op->opid;

                scanvar = NO_OP;

                /* ----------------
                 *	determine information in leftop
                 * ----------------
                 */
                leftop = (Node *) get_leftop(clause);

                Assert(leftop != NULL);

                if (IsA(leftop, Var) && var_is_rel((Var *) leftop))
                {
                        /* ----------------
                         *	if the leftop is a "rel-var", then it means
                         *	that it is a var node which tells us which
                         *	attribute to use for our scan key.
                         * ----------------
                         */
                        varattno = ((Var *) leftop)->varattno;
                        scanvar = LEFT_OP;
                }
                else if (IsA(leftop, Const))
                {
                        /* ----------------
                         *	if the leftop is a const node then it means
                         *	it identifies the value to place in our scan key.
                         * ----------------
                         */
                        scanvalue = ((Const *) leftop)->constvalue;
                        if (((Const *) leftop)->constisnull)
                                flags |= SK_ISNULL;
                }
                else if (IsA(leftop, Param))
                {
                        bool		isnull;

                        /* ----------------
                         *	if the leftop is a Param node then it means
                         *	it identifies the value to place in our scan key.
                         * ----------------
                         */

                        /* Life was so easy before ... subselects */
                        if (((Param *) leftop)->paramkind == PARAM_EXEC)
                        {
                            elog(ERROR,"delegated index scan runtime keys not implemented -- left side");
                        }
                        else
                        {
                                /* treat Param like a constant */
                                scanvalue = ExecEvalParam((Param *) leftop,
                                                            expr_cxt,
                                                            &isnull);
                                if (isnull)
                                        flags |= SK_ISNULL;
                        }
                }
                else
                {
                        elog(ERROR,"delegated index scan runtime keys not implemented");
                }

                /* ----------------
                 *	now determine information in rightop
                 * ----------------
                 */
                rightop = (Node *) get_rightop(clause);

                Assert(rightop != NULL);

                if (IsA(rightop, Var) && var_is_rel((Var *) rightop))
                {
                        /* ----------------
                         *	here we make sure only one op identifies the
                         *	scan-attribute...
                         * ----------------
                         */
                        if (scanvar == LEFT_OP)
                                elog(ERROR, "ExecInitIndexScan: %s",
                                         "both left and right op's are rel-vars");

                        /* ----------------
                         *	if the rightop is a "rel-var", then it means
                         *	that it is a var node which tells us which
                         *	attribute to use for our scan key.
                         * ----------------
                         */
                        varattno = ((Var *) rightop)->varattno;
                        scanvar = RIGHT_OP;
                }
                else if (IsA(rightop, Const))
                {
                        /* ----------------
                         *	if the rightop is a const node then it means
                         *	it identifies the value to place in our scan key.
                         * ----------------
                         */
                        scanvalue = ((Const *) rightop)->constvalue;
                        if (((Const *) rightop)->constisnull)
                                flags |= SK_ISNULL;
                }
                else if (IsA(rightop, Param))
                {
                        bool		isnull;

                        /* ----------------
                         *	if the rightop is a Param node then it means
                         *	it identifies the value to place in our scan key.
                         * ----------------
                         */

                        /* Life was so easy before ... subselects */
                        if (((Param *) rightop)->paramkind == PARAM_EXEC)
                        {
                        elog(ERROR,"delegated index scan runtime keys not implemented -- right side");
                        }
                        else
                        {
                                /* treat Param like a constant */
                                scanvalue = ExecEvalParam((Param *) rightop,
                                                                   expr_cxt,&isnull);
                                if (isnull)
                                        flags |= SK_ISNULL;
                        }
                }
                else
                {
                        elog(ERROR,"delegated index scan runtime keys not implemented -- right side");
                }

                /* ----------------
                 *	now check that at least one op tells us the scan
                 *	attribute...
                 * ----------------
                 */
                if (scanvar == NO_OP)
                        elog(ERROR, "ExecInitIndexScan: %s",
                                 "neither leftop nor rightop refer to scan relation");

                /* ----------------
                 *	initialize the scan key's fields appropriately
                 * ----------------
                 */
                ScanKeyEntryInitialize(&scan_keys[j],
                                                           flags,
                                                           varattno,	/* attribute number to
                                                                                         * scan */
                                                           (RegProcedure) opid, /* reg proc to use */
                                                           scanvalue);	/* constant */
        }

        /* ----------------
         *	store the key information into our array.
         * ----------------
         */
        return scan_keys;

}


bool
ExecInitDelegatedIndexScan(DelegatedIndexScan *node, EState *estate)
{
	CommonScanState *       scanstate;
	List	   *            rangeTable;
	RangeTblEntry *         rtentry;
	Index                   relid;
	Oid                     reloid;

	int			baseid;
        IndexScanArgs*          scanargs;

	/* ----------------
	 *	assign execution state to node
	 * ----------------
	 */
	node->scan.plan.state = estate;

	/* --------------------------------
	 *	Part 1)  initialize scan state
	 *
	 *	create new CommonScanState for node
	 * --------------------------------
	 */
	scanstate = makeNode(CommonScanState);

	node->scan.scanstate = scanstate;

	/* ----------------
	 *	assign node's base_id .. we don't use AssignNodeBaseid() because
	 *	the increment is done later on after we assign the index scan's
	 *	scanstate.	see below.
	 * ----------------
	 */
	baseid = estate->es_BaseId;
	scanstate->cstate.cs_base_id = baseid;

	/* ----------------
	 *	create expression context for node
	 * ----------------
	 */
	ExecAssignExprContext(estate, &scanstate->cstate);

#define INDEXSCAN_NSLOTS 3
	/* ----------------
	 *	tuple table initialization
	 * ----------------
	 */
	ExecInitResultTupleSlot(estate, &scanstate->cstate);
	ExecInitScanTupleSlot(estate, scanstate);

	/* ----------------
	 *	initialize projection info.  result type comes from scan desc
	 *	below..
	 * ----------------
	 */
	ExecAssignProjectionInfo((Plan *) node, &scanstate->cstate);

	/* ----------------
	 *	assign base id to index scan state also
	 * ----------------
	 */
	scanstate->cstate.cs_base_id = baseid;
	baseid++;
	estate->es_BaseId = baseid;

	/* ----------------
	 *	build the index scan keys from the index qualification
	 * ----------------
	 */
         scanargs = palloc(sizeof(IndexScanArgs));
	scanargs->scankey = BuildScanKey(node->indxqual, scanstate->cstate.cs_ExprContext);
        scanargs->keycount = length(node->indxqual);
/* if it is no direction then set to forward 
    flag that we don't care what order 
    the pointers come in
*/
        if (ScanDirectionIsNoMovement(node->indxorderdir)) {
            scanargs->dir = ForwardScanDirection;
            scanargs->ordered = false;
        } else {
            scanargs->dir = node->indxorderdir;
            scanargs->ordered = true;
        }

        scanargs->index = node->indexid;

	/* ----------------
	 *	get the range table and direction information
	 *	from the execution state (these are needed to
	 *	open the relations).
	 * ----------------
	 */
	rangeTable = estate->es_range_table;

	/* ----------------
	 *	open the base relation
	 * ----------------
	 */
	relid = node->scan.scanrelid;
	rtentry = rt_fetch(relid, rangeTable);
	reloid = rtentry->relid;
        scanstate->css_currentRelation = heap_open(reloid,AccessShareLock);
        scanstate->css_currentScanDesc = NULL;

        scanargs->heap = reloid;

	if (!RelationGetForm(scanstate->css_currentRelation)->relhasindex)
		elog(ERROR, "indexes of the relation %lu was inactivated", reloid);

	/* ----------------
	 *	get the scan type from the relation descriptor.
	 * ----------------
	 */
	ExecAssignScanType(scanstate, RelationGetDescr(scanstate->css_currentRelation));
	ExecAssignResultTypeFromTL((Plan *) node, &scanstate->cstate);

        node->scanargs = scanargs;
	node->delegate = DelegatedScanStart(DolIndexDelegation,scanargs);
	node->current = InvalidBuffer;

	return TRUE;
}

static void* 
DolIndexDelegation(Delegate arg) {
    BufferTrigger       trigger;
    int TransferMax = DelegatedGetTransferMax();

    IndexScanArgs* scan_args = (IndexScanArgs*)DelegatedScanArgs(arg);
    
    trigger.when = TRIGGER_READ;
    trigger.call = (trigger_func)IndexPointerTranfer;
    trigger.args = arg;
    
    scan_args->items = palloc(sizeof(ItemPointerData) * TransferMax);
    scan_args->counter = 0;
    scan_args->done = false;

    Relation heap = heap_open(scan_args->heap,AccessShareLock);	

    Relation rel = index_open(scan_args->index);

    IndexScanDesc scan = index_beginscan(rel,ScanDirectionIsBackward(scan_args->dir),scan_args->keycount,scan_args->scankey);
    
    RelationSetTrigger(rel,&trigger);
    
    while ( !scan_args->done ) {
        scan_args->done = !index_getnext(scan,scan_args->dir);
        
        if ( !scan_args->done ) {
            scan_args->items[scan_args->counter++] = scan->xs_ctup.t_self;
        }

       if ( scan_args->done || (scan_args->counter == TransferMax) ) {
/*  
        sort the pointers by block number 
        if the query does not require ordered
        items.  hopefully this will reduce seek times
*/
            if ( !scan_args->ordered ) {
                BlockNumber check = InvalidBlockNumber;
                int  move;

                qsort(scan_args->items,scan_args->counter,sizeof(ItemPointerData),(comparator)compare_blocks);
/*  open up the buffers the main thread is going to need */
                for ( move=0;move < scan_args->counter;move++ ) {
                    BlockNumber next = ItemPointerGetBlockNumber(&scan_args->items[move]);
                    if ( next != check ) {
                        Buffer buf = ReadBuffer(heap,next);
                        if ( !BufferIsValid(buf) ) {
                            BiasBuffer(heap,buf);
                            ReleaseBuffer(heap,buf);
                            check = next;
                        }
                    }
                }
            } else {

            }

            if ( !DelegatedTransferPointers(arg,scan_args->items,scan_args->counter) ) {
                scan_args->done = true;
            }

            scan_args->counter = 0;
        }
    }

    RelationClearTrigger(rel);
    
    index_endscan(scan);
    index_close(rel);

    heap_close(heap,AccessShareLock);
    
    pfree(scan_args->items);

    DelegatedDone(arg);

    return NULL;
}

static int
IndexPointerTranfer(Relation rel, void*  args) {
    Delegate delegate = (Delegate)args;
    IndexScanArgs* scan_args = (IndexScanArgs*)DelegatedScanArgs(delegate);
    
    if ( DelegatedCollectorWaiting(delegate) ) {
        if ( !DelegatedTransferPointers(delegate,scan_args->items,scan_args->counter) ) {
            scan_args->done = true;
        }

        scan_args->counter = 0;
    }
    
    return 0;
}

int compare_blocks(ItemPointer c1, ItemPointer c2) {
    BlockNumber b1 = ItemPointerGetBlockNumber(c1);
    BlockNumber b2 = ItemPointerGetBlockNumber(c2);

    if ( b1 == b2 ) return 0;
    else if ( b1 < b2 ) return -1;
    else return 1;
}

int
ExecCountSlotsDelegatedIndexScan(DelegatedIndexScan *node) {
	return ExecCountSlotsNode(outerPlan((Plan *) node)) +
	ExecCountSlotsNode(innerPlan((Plan *) node)) + INDEXSCAN_NSLOTS;
}

