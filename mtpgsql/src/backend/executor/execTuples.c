/*-------------------------------------------------------------------------
 *
 * execTuples.c
 *	  Routines dealing with the executor tuple tables.	These are used to
 *	  ensure that the executor frees copies of tuples (made by
 *	  ExecTargetList) properly.
 *
 *	  Routines dealing with the type information for tuples. Currently,
 *	  the type information for a tuple is an array of FormData_pg_attribute.
 *	  This information is needed by routines manipulating tuples
 *	  (getattribute, formtuple, etc.).
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
 *
 *	 TABLE CREATE/DELETE
 *		ExecCreateTupleTable	- create a new tuple table
 *		ExecDropTupleTable	- destroy a table
 *
 *	 SLOT RESERVERATION
 *		ExecAllocTableSlot		- find an available slot in the table
 *
 *	 SLOT ACCESSORS
 *		ExecStoreTuple			- store a tuple in the table
 *		ExecFetchTuple			- fetch a tuple from the table
 *		ExecClearTuple			- clear contents of a table slot
 *		ExecSlotPolicy			- return slot's tuple pfree policy
 *		ExecSetSlotPolicy		- diddle the slot policy
 *		ExecSlotDescriptor		- type of tuple in a slot
 *		ExecSetSlotDescriptor	- set a slot's tuple descriptor
 *		ExecSetSlotDescriptorIsNew - diddle the slot-desc-is-new flag
 *		ExecSetNewSlotDescriptor - set a desc and the is-new-flag all at once
 *
 *	 SLOT STATUS PREDICATES
 *		TupIsNull				- true when slot contains no tuple(Macro)
 *		ExecSlotDescriptorIsNew - true if we're now storing a different
 *								  type of tuple in a slot
 *
 *	 CONVENIENCE INITIALIZATION ROUTINES
 *		ExecInitResultTupleSlot    \	convience routines to initialize
 *		ExecInitScanTupleSlot		\	the various tuple slots for nodes
 *		ExecInitMarkedTupleSlot		/  which store copies of tuples.
 *		ExecInitOuterTupleSlot	   /
 *		ExecInitHashTupleSlot	  /
 *
 *	 old routines:
 *		ExecGetTupType			- get type of tuple returned by this node
 *		ExecTypeFromTL			- form a TupleDesc from a target list
 *
 *	 EXAMPLE OF HOW TABLE ROUTINES WORK
 *		Suppose we have a query such as retrieve (EMP.name) and we have
 *		a single SeqScan node in the query plan.
 *
 *		At ExecStart()
 *		----------------
 *		- InitPlan() calls ExecCreateTupleTable() to create the tuple
 *		  table which will hold tuples processed by the executor.
 *
 *		- ExecInitSeqScan() calls ExecInitScanTupleSlot() and
 *		  ExecInitResultTupleSlot() to reserve places in the tuple
 *		  table for the tuples returned by the access methods and the
 *		  tuples resulting from preforming target list projections.
 *
 *		During ExecRun()
 *		----------------
 *		- SeqNext() calls ExecStoreTuple() to place the tuple returned
 *		  by the access methods into the scan tuple slot.
 *
 *		- ExecSeqScan() calls ExecStoreTuple() to take the result
 *		  tuple from ExecTargetList() and place it into the result tuple
 *		  slot.
 *
 *		- ExecutePlan() calls ExecRetrieve() which gets the tuple out of
 *		  the slot passed to it by calling ExecFetchTuple().  this tuple
 *		  is then returned.
 *
 *		At ExecEnd()
 *		----------------
 *		- EndPlan() calls ExecDropTupleTable() to clean up any remaining
 *		  tuples left over from executing the query.
 *
 *		The important thing to watch in the executor code is how pointers
 *		to the slots containing tuples are passed instead of the tuples
 *		themselves.  This facilitates the communication of related information
 *		(such as whether or not a tuple should be pfreed, what buffer contains
 *		this tuple, the tuple's tuple descriptor, etc).   Note that much of
 *		this information is also kept in the ExprContext of each node.
 *		Soon the executor will be redesigned and ExprContext's will contain
 *		only slot pointers.  -cim 3/14/91
 *
 *	 NOTES
 *		The tuple table stuff is relatively new, put here to alleviate
 *		the process growth problems in the executor.  The other routines
 *		are old (from the original lisp system) and may someday become
 *		obsolete.  -cim 6/23/90
 *
 *		In the implementation of nested-dot queries such as
 *		"retrieve (EMP.hobbies.all)", a single scan may return tuples
 *		of many types, so now we return pointers to tuple descriptors
 *		along with tuples returned via the tuple table.  This means
 *		we now have a bunch of routines to diddle the slot descriptors
 *		too.  -cim 1/18/90
 *
 *		The tuple table stuff depends on the executor/tuptable.h macros,
 *		and the TupleTableSlot node in execnodes.h.
 *
 */


#include "postgres.h"
#include "env/env.h"
#include "executor/executor.h"

#undef ExecStoreTuple

#include "catalog/pg_type.h"
#include "access/heapam.h"

static TupleTableSlot *NodeGetResultTupleSlot(Plan *node);


/* ----------------------------------------------------------------
 *				  tuple table create/delete functions
 * ----------------------------------------------------------------
 */
/* --------------------------------
 *		ExecCreateTupleTable
 *
 *		This creates a new tuple table of the specified initial
 *		size.  If the size is insufficient, ExecAllocTableSlot()
 *		will grow the table as necessary.
 *
 *		This should be used by InitPlan() to allocate the table.
 *		The table's address will be stored in the EState structure.
 * --------------------------------
 */
TupleTable						/* return: address of table */
ExecCreateTupleTable(int initialSize)	/* initial number of slots in
										 * table */
{
	TupleTable	newtable;		/* newly allocated table */
	TupleTableSlot *array;		/* newly allocated slot array */

	/* ----------------
	 *	sanity checks
	 * ----------------
	 */
	Assert(initialSize >= 1);

	/* ----------------
	 *	Now allocate our new table along with space for the pointers
	 *	to the tuples.
	 */

	newtable = (TupleTable) palloc(sizeof(TupleTableData));
	array = (TupleTableSlot *) palloc(initialSize * sizeof(TupleTableSlot));

	/* ----------------
	 *	clean out the slots we just allocated
	 * ----------------
	 */
	MemSet(array, 0, initialSize * sizeof(TupleTableSlot));

	/* ----------------
	 *	initialize the new table and return it to the caller.
	 * ----------------
	 */
	newtable->size = initialSize;
	newtable->next = 0;
	newtable->array = array;

        newtable->cxt = MemoryContextGetCurrentContext();

	return newtable;
}

/* --------------------------------
 *		ExecDropTupleTable
 *
 *		This pfrees the storage assigned to the tuple table and
 *		optionally pfrees the contents of the table also.
 *		It is expected that this routine be called by EndPlan().
 * --------------------------------
 */
void
ExecDropTupleTable(TupleTable table,	/* tuple table */
				   bool shouldFree)		/* true if we should free slot
										 * contents */
{
	int			next;			/* next available slot */
	TupleTableSlot *array;		/* start of table array */
	int			i;				/* counter */

	/* ----------------
	 *	sanity checks
	 * ----------------
	 */
	Assert(table != NULL);

	/* ----------------
	 *	get information from the table
	 * ----------------
	 */
	array = table->array;
	next = table->next;

	/* ----------------
	 *	first free all the valid pointers in the tuple array
	 *	and drop refcounts of any referenced buffers,
	 *	if that's what the caller wants.  (There is probably
	 *	no good reason for the caller ever not to want it!)
	 *
	 *	Note: we do nothing about the Tuple Descriptor's
	 *	we store in the slots.	This may have to change (ex: we should
	 *	probably worry about pfreeing tuple descs too) -cim 3/14/91
	 *
	 *	Right now, the handling of tuple pointers and buffer refcounts
	 *	is clean, but the handling of tuple descriptors is NOT; they
	 *	are copied around with wild abandon.  It would take some work
	 *	to make tuple descs pfree'able.  Fortunately, since they're
	 *	normally only made once per scan, it's probably not worth
	 *	worrying about...  tgl 9/21/99
	 * ----------------
	 */
	if (shouldFree)
	{
		for (i = 0; i < next; i++)
			ExecClearTuple(&array[i]);
	}

	/* ----------------
	 *	finally free the tuple array and the table itself.
	 * ----------------
	 */
	pfree(array);
	pfree(table);

}


/* ----------------------------------------------------------------
 *				  tuple table slot reservation functions
 * ----------------------------------------------------------------
 */
/* --------------------------------
 *		ExecAllocTableSlot
 *
 *		This routine is used to reserve slots in the table for
 *		use by the various plan nodes.	It is expected to be
 *		called by the node init routines (ex: ExecInitNestLoop).
 *		once per slot needed by the node.  Not all nodes need
 *		slots (some just pass tuples around).
 * --------------------------------
 */
TupleTableSlot *				/* return: the slot allocated in the tuple
								 * table */
ExecAllocTableSlot(TupleTable table)
{
	int			slotnum;		/* new slot number */
	TupleTableSlot *slot;

	/* ----------------
	 *	sanity checks
	 * ----------------
	 */
	Assert(table != NULL);

	/* ----------------
	 *	if our table is full we have to allocate a larger
	 *	size table.  Since ExecAllocTableSlot() is only called
	 *	before the table is ever used to store tuples, we don't
	 *	have to worry about the contents of the old table.
	 *	If this changes, then we will have to preserve the contents.
	 *	-cim 6/23/90
	 *
	 *	Unfortunately, we *cannot* do this.  All of the nodes in
	 *	the plan that have already initialized their slots will have
	 *	pointers into _freed_ memory.  This leads to bad ends.	We
	 *	now count the number of slots we will need and create all the
	 *	slots we will need ahead of time.  The if below should never
	 *	happen now.  Give a WARN if it does.  -mer 4 Aug 1992
	 * ----------------
	 */
	if (table->next >= table->size)
	{

		/*
		 * int newsize = NewTableSize(table->size);
		 *
		 * pfree(table->array); table->array = (Pointer) palloc(newsize *
		 * TableSlotSize); bzero(table->array, newsize * TableSlotSize);
		 * table->size =  newsize;
		 */
		elog(NOTICE, "Plan requires more slots than are available");
		elog(ERROR, "send mail to your local executor guru to fix this");
	}

	/* ----------------
	 *	at this point, space in the table is guaranteed so we
	 *	reserve the next slot, initialize and return it.
	 * ----------------
	 */
	slotnum = table->next;
	table->next++;

	slot = &(table->array[slotnum]);

	/* Make sure the allocated slot is valid (and empty) */
	slot->type = T_TupleTableSlot;
	slot->val = (HeapTuple) NULL;
	slot->ttc_descIsNew = true;
	slot->ttc_tupleDescriptor = (TupleDesc) NULL;
	slot->ttc_whichplan = -1;
        slot->ttc_cxt = table->cxt;
	
	return slot;
}

TupleTableSlot *
ExecCreateTableSlot() {
        TupleTableSlot* slot = makeNode(TupleTableSlot);

	slot->val = (HeapTuple) NULL;
	slot->ttc_descIsNew = true;
	slot->ttc_tupleDescriptor = (TupleDesc) NULL;
	slot->ttc_whichplan = -1;
        slot->ttc_cxt = MemoryContextGetCurrentContext();
        
    return slot;
}

/* ----------------------------------------------------------------
 *				  tuple table slot accessor functions
 * ----------------------------------------------------------------
 */

/* --------------------------------
 *		ExecStoreTuple
 *
 *		This function is used to store a tuple into a specified
 *		slot in the tuple table.
 *
 *		tuple:	tuple to store
 *		slot:	slot to store it in
 *		buffer: disk buffer if tuple is in a disk page, else InvalidBuffer
 *		shouldFree: true if ExecClearTuple should pfree() the tuple
 *					when done with it
 *
 * If 'buffer' is not InvalidBuffer, the tuple table code acquires a pin
 * on the buffer which is held until the slot is cleared, so that the tuple
 * won't go away on us.
 *
 * shouldFree is normally set 'true' for tuples constructed on-the-fly.
 * It must always be 'false' for tuples that are stored in disk pages,
 * since we don't want to try to pfree those.
 *
 * Another case where it is 'false' is when the referenced tuple is held
 * in a tuple table slot belonging to a lower-level executor Proc node.
 * In this case the lower-level slot retains ownership and responsibility
 * for eventually releasing the tuple.	When this method is used, we must
 * be certain that the upper-level Proc node will lose interest in the tuple
 * sooner than the lower-level one does!  If you're not certain, copy the
 * lower-level tuple with heap_copytuple and let the upper-level table
 * slot assume ownership of the copy!
 *
 * Return value is just the passed-in slot pointer.
 * --------------------------------
 */
TupleTableSlot *
ExecStoreTuple(HeapTuple tuple, TupleTableSlot *slot, bool transfer)
{
	if ( tuple == NULL || ( !transfer && tuple->t_datamcxt != NULL ) ) {
            slot->val = tuple;
            slot->ttc_shouldfree = false;
        } else if ( tuple->t_datamcxt == slot->ttc_cxt ) {
            slot->val = tuple;
            slot->ttc_shouldfree = transfer;
        } else {
            MemoryContext cxt = MemoryContextSwitchTo(slot->ttc_cxt);
            slot->val = heap_copytuple(tuple);
            slot->ttc_shouldfree = true;
            MemoryContextSwitchTo(cxt);
            if ( transfer && tuple->t_datamcxt != NULL ) heap_freetuple(tuple);
        }

	return slot;
}

/* --------------------------------
 *		ExecClearTuple
 *
 *		This function is used to clear out a slot in the tuple table.
 * --------------------------------
 */
TupleTableSlot *				/* return: slot passed */
ExecClearTuple(TupleTableSlot *slot)	/* slot in which to store tuple */
{
        if (slot->val != NULL && slot->ttc_shouldfree ) {
            heap_freetuple(slot->val);
        }
        slot->val = NULL;
	slot->ttc_shouldfree = false;

        return slot;
}

/* --------------------------------
 *		ExecSetSlotDescriptor
 *
 *		This function is used to set the tuple descriptor associated
 *		with the slot's tuple.
 * --------------------------------
 */
TupleDesc						/* return: old slot tuple descriptor */
ExecSetSlotDescriptor(TupleTableSlot *slot,		/* slot to change */
                        TupleDesc tupdesc)		/* tuple descriptor */
{
	TupleDesc	old_tupdesc = slot->ttc_tupleDescriptor;
        
        slot->ttc_tupleDescriptor = tupdesc;
        
	return old_tupdesc;
}

/* --------------------------------
 *		ExecSetSlotDescriptorIsNew
 *
 *		This function is used to change the setting of the "isNew" flag
 * --------------------------------
 */
void
ExecSetSlotDescriptorIsNew(TupleTableSlot *slot,		/* slot to change */
						   bool isNew)	/* "isNew" setting */
{
	slot->ttc_descIsNew = isNew;
}


/* ----------------------------------------------------------------
 *				convenience initialization routines
 * ----------------------------------------------------------------
 */
/* --------------------------------
 *		ExecInit{Result,Scan,Raw,Marked,Outer,Hash}TupleSlot
 *
 *		These are convenience routines to initialize the specfied slot
 *		in nodes inheriting the appropriate state.
 * --------------------------------
 */
#define INIT_SLOT_DEFS \
	TupleTable	   tupleTable; \
	TupleTableSlot*   slot

#define INIT_SLOT_ALLOC \
	tupleTable = (TupleTable) estate->es_tupleTable; \
	slot =		 ExecAllocTableSlot(tupleTable);

/* ----------------
 *		ExecInitResultTupleSlot
 * ----------------
 */
void
ExecInitResultTupleSlot(EState *estate, CommonState *commonstate)
{
	INIT_SLOT_DEFS;
	INIT_SLOT_ALLOC;
	commonstate->cs_ResultTupleSlot = (TupleTableSlot *) slot;
}

/* ----------------
 *		ExecInitScanTupleSlot
 * ----------------
 */
void
ExecInitScanTupleSlot(EState *estate, CommonScanState *commonscanstate)
{
	INIT_SLOT_DEFS;
	INIT_SLOT_ALLOC;
	commonscanstate->css_ScanTupleSlot = (TupleTableSlot *) slot;
}

/* ----------------
 *		ExecInitOuterTupleSlot
 * ----------------
 */
void
ExecInitOuterTupleSlot(EState *estate, HashJoinState *hashstate)
{
	INIT_SLOT_DEFS;
	INIT_SLOT_ALLOC;
	hashstate->hj_OuterTupleSlot = slot;
}

static TupleTableSlot *
NodeGetResultTupleSlot(Plan *node)
{
	TupleTableSlot *slot;

	switch (nodeTag(node))
	{

		case T_Result:
			{
				ResultState *resstate = ((Result *) node)->resstate;

				slot = resstate->cstate.cs_ResultTupleSlot;
			}
			break;

		case T_SeqScan:
			{
				CommonScanState *scanstate = ((SeqScan *) node)->scanstate;

				slot = scanstate->cstate.cs_ResultTupleSlot;
			}
			break;

		case T_DelegatedSeqScan:
			{
				CommonScanState *scanstate = ((DelegatedSeqScan *) node)->scan.scanstate;

				slot = scanstate->cstate.cs_ResultTupleSlot;
			}
			break;

		case T_NestLoop:
			{
				NestLoopState *nlstate = ((NestLoop *) node)->nlstate;

				slot = nlstate->jstate.cs_ResultTupleSlot;
			}
			break;

		case T_Append:
			{
				Append	   *n = (Append *) node;
				AppendState *appendstate;
				List	   *appendplans;
				int			whichplan;
				Plan	   *subplan;

				appendstate = n->appendstate;
				appendplans = n->appendplans;
				whichplan = appendstate->as_whichplan;

				subplan = (Plan *) nth(whichplan, appendplans);
				slot = NodeGetResultTupleSlot(subplan);
				break;
			}

		case T_IndexScan:
			{
				CommonScanState *scanstate = ((IndexScan *) node)->scan.scanstate;

				slot = scanstate->cstate.cs_ResultTupleSlot;
			}
			break;

		case T_DelegatedIndexScan:
			{
				CommonScanState *scanstate = ((DelegatedIndexScan *) node)->scan.scanstate;

				slot = scanstate->cstate.cs_ResultTupleSlot;
			}
			break;

		case T_Material:
			{
				MaterialState *matstate = ((Material *) node)->matstate;

				slot = matstate->csstate.css_ScanTupleSlot;
			}
			break;

		case T_Sort:
			{
				SortState  *sortstate = ((Sort *) node)->sortstate;

				slot = sortstate->csstate.css_ScanTupleSlot;
			}
			break;

		case T_Agg:
			{
				AggState   *aggstate = ((Agg *) node)->aggstate;

				slot = aggstate->csstate.cstate.cs_ResultTupleSlot;
			}
			break;

		case T_Group:
			{
				GroupState *grpstate = ((Group *) node)->grpstate;

				slot = grpstate->csstate.cstate.cs_ResultTupleSlot;
			}
			break;

		case T_Hash:
			{
				HashState  *hashstate = ((Hash *) node)->hashstate;

				slot = hashstate->cstate.cs_ResultTupleSlot;
			}
			break;

		case T_Unique:
			{
				UniqueState *uniquestate = ((Unique *) node)->uniquestate;

				slot = uniquestate->cstate.cs_ResultTupleSlot;
			}
			break;

		case T_MergeJoin:
			{
				MergeJoinState *mergestate = ((MergeJoin *) node)->mergestate;

				slot = mergestate->jstate.cs_ResultTupleSlot;
			}
			break;

		case T_HashJoin:
			{
				HashJoinState *hashjoinstate = ((HashJoin *) node)->hashjoinstate;

				slot = hashjoinstate->jstate.cs_ResultTupleSlot;
			}
			break;

		case T_TidScan:
			{
				CommonScanState *scanstate = ((IndexScan *) node)->scan.scanstate;

				slot = scanstate->cstate.cs_ResultTupleSlot;
			}
			break;

		default:
			/* ----------------
			 *	  should never get here
			 * ----------------
			 */
			elog(ERROR, "NodeGetResultTupleSlot: node not yet supported: %d ",
				 nodeTag(node));

			return NULL;
	}
	return slot;
}

/* ----------------------------------------------------------------
 *		ExecGetTupType
 *
 *		this gives you the tuple descriptor for tuples returned
 *		by this node.  I really wish I could ditch this routine,
 *		but since not all nodes store their type info in the same
 *		place, we have to do something special for each node type.
 *
 *		Soon, the system will have to adapt to deal with changing
 *		tuple descriptors as we deal with dynamic tuple types
 *		being returned from procedure nodes.  Perhaps then this
 *		routine can be retired.  -cim 6/3/91
 *
 * old comments
 *		This routine just gets the type information out of the
 *		node's state.  If you already have a node's state, you
 *		can get this information directly, but this is a useful
 *		routine if you want to get the type information from
 *		the node's inner or outer subplan easily without having
 *		to inspect the subplan.. -cim 10/16/89
 *
 * ----------------------------------------------------------------
 */

TupleDesc
ExecGetTupType(Plan *node)
{
	TupleTableSlot *slot;
	TupleDesc	tupType;

	if (node == NULL)
		return NULL;

	slot = NodeGetResultTupleSlot(node);
	tupType = slot->ttc_tupleDescriptor;
	return tupType;
}

/*
TupleDesc
ExecCopyTupType(TupleDesc td, int natts)
{
	TupleDesc newTd;
	int				i;

	newTd = CreateTemplateTupleDesc(natts);
	i = 0;
	while (i < natts)
		{
			newTd[i] = (Form_pg_attribute)palloc(sizeof(FormData_pg_attribute));
			memmove(newTd[i], td[i], sizeof(FormData_pg_attribute));
			i++;
		}
	return newTd;
}
*/

/* ----------------------------------------------------------------
 *		ExecTypeFromTL
 *
 *		Currently there are about 4 different places where we create
 *		TupleDescriptors.  They should all be merged, or perhaps
 *		be rewritten to call BuildDesc().
 *
 *	old comments
 *		Forms attribute type info from the target list in the node.
 *		It assumes all domains are individually specified in the target list.
 *		It fails if the target list contains something like Emp.all
 *		which represents all the attributes from EMP relation.
 *
 *		Conditions:
 *			The inner and outer subtrees should be initialized because it
 *			might be necessary to know the type infos of the subtrees.
 * ----------------------------------------------------------------
 */
TupleDesc
ExecTypeFromTL(List *targetList)
{
	List	   *tlcdr;
	TupleDesc	typeInfo;
	Resdom	   *resdom;
	Oid			restype;
	int			len;

	/* ----------------
	 *	examine targetlist - if empty then return NULL
	 * ----------------
	 */
	len = ExecTargetListLength(targetList);

	if (len == 0)
		return NULL;

	/* ----------------
	 *	allocate a new typeInfo
	 * ----------------
	 */
	typeInfo = CreateTemplateTupleDesc(len);

	/* ----------------
	 * notes: get resdom from (resdom expr)
	 *		  get_typbyval comes from src/lib/l-lisp/lsyscache.c
	 * ----------------
	 */
	tlcdr = targetList;
	while (tlcdr != NIL)
	{
		TargetEntry *tle = lfirst(tlcdr);

		if (tle->resdom != NULL)
		{
			resdom = tle->resdom;
			restype = resdom->restype;

			TupleDescInitEntry(typeInfo,
							   resdom->resno,
							   resdom->resname,
			/* fix for SELECT NULL ... */
							   (restype ? restype : UNKNOWNOID),
							   resdom->restypmod,
							   0,
							   false);

/*
			ExecSetTypeInfo(resdom->resno - 1,
							typeInfo,
							(Oid) restype,
							resdom->resno,
							resdom->reslen,
							NameStr(*resdom->resname),
							get_typbyval(restype),
							get_typalign(restype));
*/
		}
		else
		{
			Resdom	   *fjRes;
			List	   *fjTlistP;
			List	   *fjList = lfirst(tlcdr);

#ifdef SETS_FIXED
			TargetEntry *tle;
			Fjoin	   *fjNode = ((TargetEntry *) lfirst(fjList))->fjoin;

			tle = fjNode->fj_innerNode; /* ??? */
#endif
			fjRes = tle->resdom;
			restype = fjRes->restype;

			TupleDescInitEntry(typeInfo,
							   fjRes->resno,
							   fjRes->resname,
							   restype,
							   fjRes->restypmod,
							   0,
							   false);
/*
			ExecSetTypeInfo(fjRes->resno - 1,
							typeInfo,
							(Oid) restype,
							fjRes->resno,
							fjRes->reslen,
							(char *) fjRes->resname,
							get_typbyval(restype),
							get_typalign(restype));
*/

			foreach(fjTlistP, lnext(fjList))
			{
				TargetEntry *fjTle = lfirst(fjTlistP);

				fjRes = fjTle->resdom;

				TupleDescInitEntry(typeInfo,
								   fjRes->resno,
								   fjRes->resname,
								   restype,
								   fjRes->restypmod,
								   0,
								   false);

/*
				ExecSetTypeInfo(fjRes->resno - 1,
								typeInfo,
								(Oid) fjRes->restype,
								fjRes->resno,
								fjRes->reslen,
								(char *) fjRes->resname,
								get_typbyval(fjRes->restype),
								get_typalign(fjRes->restype));
*/
			}
		}

		tlcdr = lnext(tlcdr);
	}

	return typeInfo;
}
