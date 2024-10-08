/*-------------------------------------------------------------------------
 *
 * execMain.c
 *	  top level executor interface routines
 *
 * INTERFACE ROUTINES
 *	ExecutorStart()
 *	ExecutorRun()
 *	ExecutorEnd()
 *
 *	The old ExecutorMain() has been replaced by ExecutorStart(),
 *	ExecutorRun() and ExecutorEnd()
 *
 *	These three procedures are the external interfaces to the executor.
 *	In each case, the query descriptor and the execution state is required
 *	 as arguments
 *
 *	ExecutorStart() must be called at the beginning of any execution of any
 *	query plan and ExecutorEnd() should always be called at the end of
 *	execution of a plan.
 *
 *	ExecutorRun accepts 'feature' and 'count' arguments that specify whether
 *	the plan is to be executed forwards, backwards, and for how many tuples.
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
#include "postgres.h"
#include "env/env.h"
#include "access/heapam.h"
#include "catalog/heap.h"
#include "commands/trigger.h"
#include "executor/execdebug.h"
#include "executor/execdefs.h"
#include "executor/executor.h"
#include "miscadmin.h"
#include "optimizer/var.h"
#include "parser/parsetree.h"
#ifdef USEACL
#include "utils/acl.h"
#endif
#include "utils/builtins.h"
#include "utils/syscache.h"

/* XXX no points for style */
extern TupleTableSlot *EvalPlanQual(EState *estate, Index rti,
			 ItemPointer tid);

/* decls for local routines only used within this module */
static TupleDesc InitPlan(CmdType operation,
		 Query *parseTree,
		 Plan *plan,
		 EState *estate);
static void EndPlan(Plan *plan,
		EState *estate);
static TupleTableSlot *ExecutePlan(EState *estate, Plan *plan,
			CmdType operation,
			int offsetTuples,
			int numberTuples,
			ScanDirection direction,
			DestReceiver *destfunc);
static void ExecRetrieve(TupleTableSlot *slot,
			 DestReceiver *destfunc,
			 EState *estate);

static TupleTableSlot *EvalPlanQualNext(EState *estate);
static void EndEvalPlanQual(EState *estate);
#ifdef USEACL
static void ExecCheckQueryPerms(CmdType operation, Query *parseTree,
					Plan *plan);
static void ExecCheckPlanPerms(Plan *plan, CmdType operation,
				   int resultRelation, bool resultIsScanned);
static void ExecCheckRTPerms(List *rangeTable, CmdType operation,
				 int resultRelation, bool resultIsScanned);
static void ExecCheckRTEPerms(RangeTblEntry *rte, CmdType operation,
				  bool isResultRelation, bool resultIsScanned);
#endif
/* end of local decls */


/* ----------------------------------------------------------------
 *		ExecutorStart
 *
 *		This routine must be called at the beginning of any execution of any
 *		query plan
 *
 *		returns (AttrInfo*) which describes the attributes of the tuples to
 *		be returned by the query.
 *
 * ----------------------------------------------------------------
 */
TupleDesc
ExecutorStart(QueryDesc *queryDesc, EState *estate)
{
	TupleDesc	result;
        SnapshotHolder*  holder = GetSnapshotHolder();

	/* sanity checks */
	Assert(queryDesc != NULL);
	
	if (queryDesc->plantree->nParamExec > 0)
	{
		estate->es_param_exec_vals = (ParamExecData *)
			palloc(queryDesc->plantree->nParamExec * sizeof(ParamExecData));
		memset(estate->es_param_exec_vals, 0, queryDesc->plantree->nParamExec * sizeof(ParamExecData));
	}

	/*
	 * Make our own private copy of the current queries snapshot data
	 */
	if (holder->QuerySnapshot == NULL)
		estate->es_snapshot = NULL;
	else if (holder->QuerySnapshot == SnapshotAny)
		estate->es_snapshot = SnapshotAny;
	else
	{
		estate->es_snapshot = (Snapshot) palloc(sizeof(SnapshotData));
		memcpy(estate->es_snapshot, holder->QuerySnapshot, sizeof(SnapshotData));
		if (estate->es_snapshot->xcnt > 0)
		{
			estate->es_snapshot->xip = (TransactionId *)
				palloc(estate->es_snapshot->xcnt * sizeof(TransactionId));
			memcpy(estate->es_snapshot->xip, holder->QuerySnapshot->xip,
				   estate->es_snapshot->xcnt * sizeof(TransactionId));
		}
		estate->es_snapshot->isUser = holder->QuerySnapshot->isUser;
                estate->es_snapshot->nowait = queryDesc->parsetree->nowait;
	}

	/*
	 * Initialize the plan
	 */
	result = InitPlan(queryDesc->operation,
					  queryDesc->parsetree,
					  queryDesc->plantree,
					  estate);

	return result;
}

/* ----------------------------------------------------------------
 *		ExecutorRun
 *
 *		This is the main routine of the executor module. It accepts
 *		the query descriptor from the traffic cop and executes the
 *		query plan.
 *
 *		ExecutorStart must have been called already.
 *
 *		the different features supported are:
 *			 EXEC_RUN:	retrieve all tuples in the forward direction
 *			 EXEC_FOR:	retrieve 'count' number of tuples in the forward dir
 *			 EXEC_BACK: retrieve 'count' number of tuples in the backward dir
 *			 EXEC_RETONE: return one tuple but don't 'retrieve' it
 *						   used in postquel function processing
 *
 *
 * ----------------------------------------------------------------
 */
TupleTableSlot *
ExecutorRun(QueryDesc *queryDesc, EState *estate, int feature,Node* limoffset, Node* limcount)
{
	CmdType		operation;
	Plan	   *plan;
	TupleTableSlot *result;
	CommandDest dest;
	DestReceiver *destfunc;
	int			offset = 0;
	int			count = 0;

	/*
	 * sanity checks
	 */
	Assert(queryDesc != NULL);

	/*
	 * extract information from the query descriptor and the query
	 * feature.
	 */
	operation = queryDesc->operation;
	plan = queryDesc->plantree;
	dest = queryDesc->dest;
	destfunc = DestToFunction(dest);
	estate->es_processed = 0;
	estate->es_lastoid = InvalidOid;

	/*
	 * FIXME: the dest setup function ought to be handed the tuple desc
	 * for the tuples to be output, but I'm not quite sure how to get that
	 * info at this point.	For now, passing NULL is OK because no
	 * existing dest setup function actually uses the pointer.
	 */
	(*destfunc->setup) (destfunc, (TupleDesc) NULL);

	/*
	 * if given get the offset of the LIMIT clause
	 */
	if (limoffset != NULL)
	{
		Const	   *coffset;
		Param	   *poffset;
		ParamListInfo paramLI;
		int			i;

		switch (nodeTag(limoffset))
		{
			case T_Const:
				coffset = (Const *) limoffset;
				offset = (int) (coffset->constvalue);
				break;

			case T_Param:
				poffset = (Param *) limoffset;
				paramLI = estate->es_param_list_info;

				if (paramLI == NULL)
					elog(ERROR, "parameter for limit offset not in executor state");
				for (i = 0; paramLI[i].kind != PARAM_INVALID; i++)
				{
					if (paramLI[i].kind == PARAM_NUM && paramLI[i].id == poffset->paramid)
						break;
				}
				if (paramLI[i].kind == PARAM_INVALID)
					elog(ERROR, "parameter for limit offset not in executor state");
				if (paramLI[i].isnull)
					elog(ERROR, "limit offset cannot be NULL value");
				offset = (int) (paramLI[i].value);

				break;

			default:
				elog(ERROR, "unexpected node type %d as limit offset", nodeTag(limoffset));
		}

		if (offset < 0)
			elog(ERROR, "limit offset cannot be negative");
	}

	/*
	 * if given get the count of the LIMIT clause
	 */
	if (limcount != NULL)
	{
		Const	   *ccount;
		Param	   *pcount;
		ParamListInfo paramLI;
		int			i;

		switch (nodeTag(limcount))
		{
			case T_Const:
				ccount = (Const *) limcount;
				count = (int) (ccount->constvalue);
				break;

			case T_Param:
				pcount = (Param *) limcount;
				paramLI = estate->es_param_list_info;

				if (paramLI == NULL)
					elog(ERROR, "parameter for limit count not in executor state");
				for (i = 0; paramLI[i].kind != PARAM_INVALID; i++)
				{
					if (paramLI[i].kind == PARAM_NUM && paramLI[i].id == pcount->paramid)
						break;
				}
				if (paramLI[i].kind == PARAM_INVALID)
					elog(ERROR, "parameter for limit count not in executor state");
				if (paramLI[i].isnull)
					elog(ERROR, "limit count cannot be NULL value");
				count = (int) (paramLI[i].value);

				break;

			default:
				elog(ERROR, "unexpected node type %d as limit count", nodeTag(limcount));
		}

		if (count < 0)
			elog(ERROR, "limit count cannot be negative");
	}

	switch (feature)
	{

		case EXEC_RUN:
			result = ExecutePlan(estate,
								 plan,
								 operation,
								 offset,
								 count,
								 ForwardScanDirection,
								 destfunc);
			break;
		case EXEC_FOR:
			result = ExecutePlan(estate,
								 plan,
								 operation,
								 offset,
								 count,
								 ForwardScanDirection,
								 destfunc);
			break;

			/*
			 * retrieve next n "backward" tuples
			 */
		case EXEC_BACK:
			result = ExecutePlan(estate,
								 plan,
								 operation,
								 offset,
								 count,
								 BackwardScanDirection,
								 destfunc);
			break;

			/*
			 * return one tuple but don't "retrieve" it. (this is used by
			 * the rule manager..) -cim 9/14/89
			 */
		case EXEC_RETONE:
			result = ExecutePlan(estate,
								 plan,
								 operation,
								 0,
								 ONE_TUPLE,
								 ForwardScanDirection,
								 destfunc);
			break;
		default:
			result = NULL;
			elog(DEBUG, "ExecutorRun: Unknown feature %d", feature);
			break;
	}

	(*destfunc->cleanup) (destfunc);

	return result;
}

/* ----------------------------------------------------------------
 *		ExecutorEnd
 *
 *		This routine must be called at the end of any execution of any
 *		query plan
 *
 *		returns (AttrInfo*) which describes the attributes of the tuples to
 *		be returned by the query.
 *
 * ----------------------------------------------------------------
 */
void
ExecutorEnd(QueryDesc *queryDesc, EState *estate)
{
	/* sanity checks */
	Assert(queryDesc != NULL);

	EndPlan(queryDesc->plantree, estate);
}
/*
 * ExecCheckQueryPerms
 *		Check access permissions for all relations referenced in a query.
 */
#ifdef USEACL
static void
ExecCheckQueryPerms(CmdType operation, Query *parseTree, Plan *plan)
{
	List	   *rangeTable = parseTree->rtable;
	int			resultRelation = parseTree->resultRelation;
	bool		resultIsScanned = false;
	List	   *lp;

	/*
	 * If we have a result relation, determine whether the result rel is
	 * scanned or merely written.  If scanned, we will insist on read
	 * permission as well as modify permission.
	 */
	if (resultRelation > 0)
	{
		List	   *qvars = pull_varnos(parseTree->qual);
		List	   *tvars = pull_varnos((Node *) parseTree->targetList);

		resultIsScanned = (intMember(resultRelation, qvars) ||
						   intMember(resultRelation, tvars));
		freeList(qvars);
		freeList(tvars);
	}

	/*
	 * Check RTEs in the query's primary rangetable.
	 */
	ExecCheckRTPerms(rangeTable, operation, resultRelation, resultIsScanned);

	/*
	 * Check SELECT FOR UPDATE access rights.
	 */
	foreach(lp, parseTree->rowMark)
	{
		RowMark    *rm = lfirst(lp);

		if (!(rm->info & ROW_ACL_FOR_UPDATE))
			continue;
		ExecCheckRTEPerms(rt_fetch(rm->rti, rangeTable),
						  CMD_UPDATE, true, false);
	}

	/*
	 * Search for subplans and APPEND nodes to check their rangetables.
	 */
	ExecCheckPlanPerms(plan, operation, resultRelation, resultIsScanned);
}

/*
 * ExecCheckPlanPerms
 *		Recursively scan the plan tree to check access permissions in
 *		subplans.
 *
 * We also need to look at the local rangetables in Append plan nodes,
 * which is pretty bogus --- most likely, those tables should be mentioned
 * in the query's main rangetable.  But at the moment, they're not.
 */
static void
ExecCheckPlanPerms(Plan *plan, CmdType operation,
				   int resultRelation, bool resultIsScanned)
{
	List	   *subp;

	if (plan == NULL)
		return;

	/* Check subplans, which we assume are plain SELECT queries */

	foreach(subp, plan->initPlan)
	{
		SubPlan    *subplan = (SubPlan *) lfirst(subp);

		ExecCheckRTPerms(subplan->rtable, CMD_SELECT, 0, false);
		ExecCheckPlanPerms(subplan->plan, CMD_SELECT, 0, false);
	}
	foreach(subp, plan->subPlan)
	{
		SubPlan    *subplan = (SubPlan *) lfirst(subp);

		ExecCheckRTPerms(subplan->rtable, CMD_SELECT, 0, false);
		ExecCheckPlanPerms(subplan->plan, CMD_SELECT, 0, false);
	}

	/* Check lower plan nodes */

	ExecCheckPlanPerms(plan->lefttree, operation,
					   resultRelation, resultIsScanned);
	ExecCheckPlanPerms(plan->righttree, operation,
					   resultRelation, resultIsScanned);

	/* Do node-type-specific checks */

	switch (nodeTag(plan))
	{
		case T_Append:
			{
				Append	   *app = (Append *) plan;
				List	   *appendplans;

				if (app->inheritrelid > 0)
				{
					/*
					 * Append implements expansion of inheritance; all
					 * members of inheritrtable list will be plugged into
					 * same RTE slot. Therefore, they are either all
					 * result relations or none.
					 */
					List	   *rtable;
					foreach(rtable, app->inheritrtable)
					{
						ExecCheckRTEPerms((RangeTblEntry *) lfirst(rtable),
										  operation,
								   (app->inheritrelid == resultRelation),
										  resultIsScanned);
					}
				}
				else
				{
					/* Append implements UNION, which must be a SELECT */
					List	   *rtables;

					foreach(rtables, app->unionrtables)
					{
						ExecCheckRTPerms((List *) lfirst(rtables),
										 CMD_SELECT, 0, false);
					}
				}

				/* Check appended plans */
				foreach(appendplans, app->appendplans)
				{
					ExecCheckPlanPerms((Plan *) lfirst(appendplans),
									   operation,
									   resultRelation,
									   resultIsScanned);
				}
				break;
			}

		default:
			break;
	}
}

/*
 * ExecCheckRTPerms
 *		Check access permissions for all relations listed in a range table.
 *
 * If resultRelation is not 0, it is the RT index of the relation to be
 * treated as the result relation.	All other relations are assumed to be
 * read-only for the query.
 */
static void
ExecCheckRTPerms(List *rangeTable, CmdType operation,
				 int resultRelation, bool resultIsScanned)
{
	int			rtindex = 0;
	List	   *lp;

	foreach(lp, rangeTable)
	{
		RangeTblEntry *rte = lfirst(lp);

		++rtindex;

		ExecCheckRTEPerms(rte,
						  operation,
						  (rtindex == resultRelation),
						  resultIsScanned);
	}
}

/*
 * ExecCheckRTEPerms
 *		Check access permissions for a single RTE.
 */
static void
ExecCheckRTEPerms(RangeTblEntry *rte, CmdType operation,
				  bool isResultRelation, bool resultIsScanned)
{
	char	   *relName;
	char	   *userName;
	int32		aclcheck_result;
	if (rte->skipAcl)
	{

		/*
		 * This happens if the access to this table is due to a view query
		 * rewriting - the rewrite handler already checked the permissions
		 * against the view owner, so we just skip this entry.
		 */
		return;
	}

	relName = rte->relname;

	/*
	 * Note: GetPgUserName is presently fast enough that there's no harm
	 * in calling it separately for each RTE.  If that stops being true,
	 * we could call it once in ExecCheckQueryPerms and pass the userName
	 * down from there.  But for now, no need for the extra clutter.
	 */
	userName = GetPgUserName();

#define CHECK(MODE)		pg_aclcheck(relName, userName, MODE)

	if (isResultRelation)
	{
		if (resultIsScanned)
		{
			aclcheck_result = CHECK(ACL_RD);
			if (aclcheck_result != ACLCHECK_OK)
				elog(ERROR, "%s: %s",
					 relName, aclcheck_error_strings[aclcheck_result]);
		}
		switch (operation)
		{
			case CMD_INSERT:
			case CMD_PUT:
				/* Accept either APPEND or WRITE access for this */
				aclcheck_result = CHECK(ACL_AP);
				if (aclcheck_result != ACLCHECK_OK)
					aclcheck_result = CHECK(ACL_WR);
				break;
			case CMD_DELETE:
			case CMD_UPDATE:
				aclcheck_result = CHECK(ACL_WR);
				break;
			default:
				elog(ERROR, "ExecCheckRTEPerms: bogus operation %d",
					 operation);
				aclcheck_result = ACLCHECK_OK;	/* keep compiler quiet */
				break;
		}
	}
	else
		aclcheck_result = CHECK(ACL_RD);

	if (aclcheck_result != ACLCHECK_OK)
		elog(ERROR, "%s: %s",
			 relName, aclcheck_error_strings[aclcheck_result]);
}
#endif

/* ===============================================================
 * ===============================================================
						 static routines follow
 * ===============================================================
 * ===============================================================
 */

typedef struct execRowMark
{
	Relation	relation;
	Index		rti;
	char		resname[32];
} execRowMark;

typedef struct evalPlanQual
{
	Plan	   *plan;
	Index		rti;
	EState		estate;
	struct evalPlanQual *free;
} evalPlanQual;

/* ----------------------------------------------------------------
 *		InitPlan
 *
 *		Initializes the query plan: open files, allocate storage
 *		and start up the rule manager
 * ----------------------------------------------------------------
 */
static TupleDesc
InitPlan(CmdType operation, Query *parseTree, Plan *plan, EState *estate)
{
	List	   *rangeTable;
	int			resultRelation;
	Relation	intoRelationDesc;
	TupleDesc	tupType;
	List	   *targetList;

	/*
	 * Do permissions checks.
	 */
#ifdef USEACL
	ExecCheckQueryPerms(operation, parseTree, plan);
#endif
	/*
	 * get information from query descriptor
	 */
	rangeTable = parseTree->rtable;
	resultRelation = parseTree->resultRelation;

	/*
	 * initialize the node's execution state
	 */
	estate->es_range_table = rangeTable;

	/*
	 * initialize the BaseId counter so node base_id's are assigned
	 * correctly.  Someday baseid's will have to be stored someplace other
	 * than estate because they should be unique per query planned.
	 */
	estate->es_BaseId = 1;

	/*
	 * initialize result relation stuff
	 */

	if (resultRelation != 0 && operation != CMD_SELECT)
	{

		/*
		 * if we have a result relation, open it and initialize the result
		 * relation info stuff.
		 */
		RelationInfo *resultRelationInfo;
		Index		resultRelationIndex;
		RangeTblEntry *rtentry;
		Oid			resultRelationOid;
		Relation	resultRelationDesc;

		resultRelationIndex = resultRelation;
		rtentry = rt_fetch(resultRelationIndex, rangeTable);
		resultRelationOid = rtentry->relid;
		resultRelationDesc = heap_open(resultRelationOid, RowExclusiveLock);

		if (resultRelationDesc->rd_rel->relkind == RELKIND_SEQUENCE)
			elog(ERROR, "You can't change sequence relation %s",
				 RelationGetRelationName(resultRelationDesc));

		resultRelationInfo = makeNode(RelationInfo);
		resultRelationInfo->ri_RangeTableIndex = resultRelationIndex;
		resultRelationInfo->ri_RelationDesc = resultRelationDesc;
		resultRelationInfo->ri_NumIndices = 0;
		resultRelationInfo->ri_IndexRelationDescs = NULL;
		resultRelationInfo->ri_IndexRelationInfo = NULL;

		/*
		 * If there are indices on the result relation, open them and save
		 * descriptors in the result relation info, so that we can add new
		 * index entries for the tuples we add/update.	We need not do
		 * this for a DELETE, however, since deletion doesn't affect
		 * indexes.
		 */
		if (resultRelationDesc->rd_rel->relhasindex &&
			operation != CMD_DELETE)
			ExecOpenIndices(resultRelationOid, resultRelationInfo);

		estate->es_result_relation_info = resultRelationInfo;
	}
	else
	{

		/*
		 * if no result relation, then set state appropriately
		 */
		estate->es_result_relation_info = NULL;
	}

	/*
	 * Have to lock relations selected for update
	 */
	estate->es_rowMark = NULL;
	if (parseTree->rowMark != NULL)
	{
		List	   *l;

		foreach(l, parseTree->rowMark)
		{
			RowMark    *rm = lfirst(l);
			Oid			relid;
			Relation	relation;
			execRowMark *erm;

			if (!(rm->info & ROW_MARK_FOR_UPDATE))
				continue;
			relid = rt_fetch(rm->rti, rangeTable)->relid;
			relation = heap_open(relid, RowShareLock);
			erm = (execRowMark *) palloc(sizeof(execRowMark));
			erm->relation = relation;
			erm->rti = rm->rti;
			sprintf(erm->resname, "ctid%u", rm->rti);
			estate->es_rowMark = lappend(estate->es_rowMark, erm);
		}
	}

	/*
	 * initialize the executor "tuple" table.
	 */
	{
		int			nSlots = ExecCountSlotsNode(plan);
		TupleTable	tupleTable = ExecCreateTupleTable(nSlots + 10);		/* why add ten? - jolly */

		estate->es_tupleTable = tupleTable;
	}

	/*
	 * initialize the private state information for all the nodes in the
	 * query tree.	This opens files, allocates storage and leaves us
	 * ready to start processing tuples..
	 */
	ExecInitNode(plan, estate);

	/*
	 * get the tuple descriptor describing the type of tuples to return..
	 * (this is especially important if we are creating a relation with
	 * "retrieve into")
	 */
	tupType = ExecGetTupType(plan);		/* tuple descriptor */
	targetList = plan->targetlist;

	/*
	 * Now that we have the target list, initialize the junk filter if
	 * needed. SELECT and INSERT queries need a filter if there are any
	 * junk attrs in the tlist.  UPDATE and DELETE always need one, since
	 * there's always a junk 'ctid' attribute present --- no need to look
	 * first.
	 */
	{
		bool		junk_filter_needed = false;
		List	   *tlist;

		switch (operation)
		{
			case CMD_SELECT:
			case CMD_INSERT:
			case CMD_PUT:
				foreach(tlist, targetList)
				{
					TargetEntry *tle = (TargetEntry *) lfirst(tlist);

					if (tle->resdom->resjunk)
					{
						junk_filter_needed = true;
						break;
					}
				}
				break;
			case CMD_UPDATE:
			case CMD_DELETE:
				junk_filter_needed = true;
				break;
			default:
				break;
		}

		if (junk_filter_needed)
		{
			JunkFilter *j = ExecInitJunkFilter(targetList, tupType);

			estate->es_junkFilter = j;

			if (operation == CMD_SELECT)
				tupType = j->jf_cleanTupType;
		}
		else
			estate->es_junkFilter = NULL;
	}

	/*
	 * initialize the "into" relation
	 */
	intoRelationDesc = (Relation) NULL;

	if (operation == CMD_SELECT)
	{
		char	   *intoName;
		Oid			intoRelationId;
		TupleDesc	tupdesc;

		if (!parseTree->isPortal)
		{

			/*
			 * a select into table
			 */
			if (parseTree->into != NULL)
			{

				/*
				 * create the "into" relation
				 */
				intoName = parseTree->into;

				/*
				 * have to copy tupType to get rid of constraints
				 */
				tupdesc = CreateTupleDescCopy(tupType);

				intoRelationId = heap_create_with_catalog(intoName,
						   tupdesc, RELKIND_RELATION, parseTree->isTemp);

				FreeTupleDesc(tupdesc);

				/*
				 * Advance command counter so that the newly-created
				 * relation's catalog tuples will be visible to heap_open.
				 */
				CommandCounterIncrement();

				intoRelationDesc = heap_open(intoRelationId,
											 AccessExclusiveLock);
			}
		}
	}

	estate->es_into_relation_descriptor = intoRelationDesc;

	estate->es_origPlan = plan;
	estate->es_evalPlanQual = NULL;
	estate->es_evTuple = NULL;
	estate->es_useEvalPlan = false;

	return tupType;
}

/* ----------------------------------------------------------------
 *		EndPlan
 *
 *		Cleans up the query plan -- closes files and free up storages
 * ----------------------------------------------------------------
 */
static void
EndPlan(Plan *plan, EState *estate)
{
	RelationInfo *resultRelationInfo;
	Relation	intoRelationDesc;
	List	   *l;

	/*
	 * get information from state
	 */
	resultRelationInfo = estate->es_result_relation_info;
	intoRelationDesc = estate->es_into_relation_descriptor;

	/*
	 * shut down any PlanQual processing we were doing
	 */
	if (estate->es_evalPlanQual != NULL)
		EndEvalPlanQual(estate);

	/*
	 * shut down the query
	 */
	ExecEndNode(plan);

	/*
	 * destroy the executor "tuple" table.
	 */
	{
		TupleTable	tupleTable = (TupleTable) estate->es_tupleTable;

		ExecDropTupleTable(tupleTable, true);
		estate->es_tupleTable = NULL;
	}

	/*
	 * close the result relations if necessary, but hold locks on them
	 * until xact commit
	 */
	if (resultRelationInfo != NULL)
	{
		Relation	resultRelationDesc;

		resultRelationDesc = resultRelationInfo->ri_RelationDesc;
		heap_close(resultRelationDesc, NoLock);

		/*
		 * close indices on the result relation
		 */
		ExecCloseIndices(resultRelationInfo);
	}

	/*
	 * close the "into" relation if necessary, again keeping lock
	 */
	if (intoRelationDesc != NULL)
		heap_close(intoRelationDesc, NoLock);

	/*
	 * close any relations selected FOR UPDATE, again keeping locks
	 */
	foreach(l, estate->es_rowMark)
	{
		execRowMark *erm = lfirst(l);

		heap_close(erm->relation, NoLock);
	}
}

/* ----------------------------------------------------------------
 *		ExecutePlan
 *
 *		processes the query plan to retrieve 'tupleCount' tuples in the
 *		direction specified.
 *		Retrieves all tuples if tupleCount is 0
 *
 *		result is either a slot containing a tuple in the case
 *		of a RETRIEVE or NULL otherwise.
 *
 * ----------------------------------------------------------------
 */

/* the ctid attribute is a 'junk' attribute that is removed before the
   user can see it*/

static TupleTableSlot *
ExecutePlan(EState *estate,
			Plan *plan,
			CmdType operation,
			int offsetTuples,
			int numberTuples,
			ScanDirection direction,
			DestReceiver *destfunc)
{
	JunkFilter *junkfilter;
	TupleTableSlot *slot;
	ItemPointer tupleid = NULL;
	ItemPointerData tuple_ctid;
	int			current_tuple_count;
	TupleTableSlot *result;
    TransactionInfo* t_info = GetTransactionInfo();

	/*
	 * initialize local variables
	 */
	slot = NULL;
	current_tuple_count = 0;
	result = NULL;

	/*
	 * Set the direction.
	 */
	estate->es_direction = direction;

	/*
	 * Loop until we've processed the proper number of tuples from the
	 * plan..
	 */

	for (;;)
	{

		/*
		 * Execute the plan and obtain a tuple
		 */
		/* at the top level, the parent of a plan (2nd arg) is itself */
lnext:	;
		if (estate->es_useEvalPlan)
		{
			slot = EvalPlanQualNext(estate);
			if (TupIsNull(slot))
				slot = ExecProcNode(plan);
		}
		else
			slot = ExecProcNode(plan);

		/*
		 * if the tuple is null, then we assume there is nothing more to
		 * process so we just return null...
		 */
		if (TupIsNull(slot))
		{
			result = NULL;
			break;
		}

		/*
		 * For now we completely execute the plan and skip result tuples
		 * if requested by LIMIT offset. Finally we should try to do it in
		 * deeper levels if possible (during index scan) - Jan
		 */
		if (offsetTuples > 0)
		{
			--offsetTuples;
			continue;
		}

		/*
		 * if we have a junk filter, then project a new tuple with the
		 * junk removed.
		 *
		 * Store this new "clean" tuple in the place of the original tuple.
		 *
		 * Also, extract all the junk information we need.
		 */
		if ((junkfilter = estate->es_junkFilter) != (JunkFilter *) NULL)
		{
			Datum		datum;
			HeapTuple	newTuple;
			bool		isNull;

			/*
			 * extract the 'ctid' junk attribute.
			 */
			if (operation == CMD_UPDATE || operation == CMD_DELETE)
			{
				if (!ExecGetJunkAttribute(junkfilter,
										  slot,
										  "ctid",
										  &datum,
										  &isNull))
					elog(ERROR, "ExecutePlan: NO (junk) `ctid' was found!");

				if (isNull)
					elog(ERROR, "ExecutePlan: (junk) `ctid' is NULL!");

				tupleid = (ItemPointer) DatumGetPointer(datum);
				memcpy(&tuple_ctid,tupleid,sizeof(tuple_ctid));	/* make sure we don't free the
										 * ctid!! */
				tupleid = &tuple_ctid;
			}
			else if (estate->es_rowMark != NULL)
			{
				List	   *l;

		lmark:	;
				foreach(l, estate->es_rowMark)
				{
					execRowMark *erm = lfirst(l);
					Buffer		buffer;
					HeapTupleData tuple;
					TupleTableSlot *newSlot;
					int			test;

					if (!ExecGetJunkAttribute(junkfilter,
											  slot,
											  erm->resname,
											  &datum,
											  &isNull))
						elog(ERROR, "ExecutePlan: NO (junk) `%s' was found!", erm->resname);

					if (isNull)
						elog(ERROR, "ExecutePlan: (junk) `%s' is NULL!", erm->resname);

					tuple.t_self = *((ItemPointer) DatumGetPointer(datum));
					test = heap_mark4update(erm->relation, &buffer, &tuple, estate->es_snapshot);
					ReleaseBuffer(erm->relation, buffer);
					switch (test)
					{
						case HeapTupleSelfUpdated:
						case HeapTupleBeingUpdated:
                                                case HeapTupleMayBeUpdated:
							break;

						case HeapTupleUpdated:
							if (t_info->XactIsoLevel == XACT_SERIALIZABLE)
							{
								elog(ERROR, "Can't serialize access due to concurrent update");
								return (NULL);
							}
							else if (!(ItemPointerEquals(&(tuple.t_self),
								  (ItemPointer) DatumGetPointer(datum))))
							{
								newSlot = EvalPlanQual(estate, erm->rti, &(tuple.t_self));
								if (!(TupIsNull(newSlot)))
								{
									slot = newSlot;
									estate->es_useEvalPlan = true;
									goto lmark;
								}
							}

							/*
							 * if tuple was deleted or PlanQual failed for
							 * updated tuple - we have not return this
							 * tuple!
							 */
							goto lnext;

						default:
							elog(ERROR, "Unknown status %u from heap_mark4update", test);
							return (NULL);
					}
				}
			}

			/*
			 * Finally create a new "clean" tuple with all junk attributes
			 * removed
			 */
			newTuple = ExecRemoveJunk(junkfilter, slot);

			slot = ExecStoreTuple(newTuple,slot, false);
		}						/* if (junkfilter... */

		/*
		 * now that we have a tuple, do the appropriate thing with it..
		 * either return it to the user, add it to a relation someplace,
		 * delete it from a relation, or modify some of its attributes.
		 */

		switch (operation)
		{
			case CMD_SELECT:
				ExecRetrieve(slot,		/* slot containing tuple */
							 destfunc,	/* destination's tuple-receiver
										 * obj */
							 estate);	/* */
				result = slot;
				break;

			case CMD_PUT:
			case CMD_INSERT:
				ExecAppend(slot, tupleid, estate);
				result = NULL;
				break;

			case CMD_DELETE:
				ExecDelete(slot, tupleid, estate);
				result = NULL;
				break;

			case CMD_UPDATE:
				ExecReplace(slot, tupleid, estate);
				result = NULL;
				break;

			default:
				elog(DEBUG, "ExecutePlan: unknown operation in queryDesc");
				result = NULL;
				break;
		}

		/*
		 * check our tuple count.. if we've returned the proper number
		 * then return, else loop again and process more tuples..
		 */
		current_tuple_count += 1;
		if (numberTuples == current_tuple_count)
			break;
	}

	/*
	 * here, result is either a slot containing a tuple in the case of a
	 * RETRIEVE or NULL otherwise.
	 */
	return result;
}

/* ----------------------------------------------------------------
 *		ExecRetrieve
 *
 *		RETRIEVEs are easy.. we just pass the tuple to the appropriate
 *		print function.  The only complexity is when we do a
 *		"retrieve into", in which case we insert the tuple into
 *		the appropriate relation (note: this is a newly created relation
 *		so we don't need to worry about indices or locks.)
 * ----------------------------------------------------------------
 */
static void
ExecRetrieve(TupleTableSlot *slot,
			 DestReceiver *destfunc,
			 EState *estate)
{
	HeapTuple	tuple;
	TupleDesc	attrtype;

	/*
	 * get the heap tuple out of the tuple table slot
	 */
	tuple = slot->val;
	attrtype = slot->ttc_tupleDescriptor;

	/*
	 * insert the tuple into the "into relation"
	 */
	if (estate->es_into_relation_descriptor != NULL)
	{
		heap_insert(estate->es_into_relation_descriptor, tuple);
		IncrAppended();
	}

	/*
	 * send the tuple to the front end (or the screen)
	 */
	(*destfunc->receiveTuple) (tuple, attrtype, destfunc);
	IncrRetrieved();
	(estate->es_processed)++;
}
/* ----------------------------------------------------------------
 *		ExecPut
 *
 * ----------------------------------------------------------------
 */

int
ExecPut(TupleTableSlot *slot,
		   ItemPointer tupleid,
		   EState *estate)
{
	HeapTuple	tuple;
	int			result = 0;
	RelationInfo *resultRelationInfo;
	Relation	resultRelationDesc;
	int			numIndices;
	Oid			newId;
        ItemPointerData         ctid;
        ItemPointerSetInvalid(tupleid);
        TransactionInfo* t_info = GetTransactionInfo();

	/*
	 * get the heap tuple out of the tuple table slot
	 */
	tuple = slot->val;

	/*
	 * get information on the result relation
	 */
	resultRelationInfo = estate->es_result_relation_info;
	resultRelationDesc = resultRelationInfo->ri_RelationDesc;

	/*
	 * have to add code to preform unique checking here. cim -12/1/89
	 */

	/* BEFORE ROW INSERT Triggers */
	if (resultRelationDesc->trigdesc &&
	resultRelationDesc->trigdesc->n_before_row[TRIGGER_EVENT_INSERT] > 0)
	{
		HeapTuple	newtuple;

		newtuple = ExecBRInsertTriggers(resultRelationDesc, tuple);

		if (newtuple == NULL)	/* "do nothing" */
			return 0;

		if (newtuple != tuple)	/* modified by Trigger(s) */
		{
			heap_freetuple(tuple);
			ExecStoreTuple(newtuple,slot,false);
			tuple = slot->val;
		}
	}

	/*
	 * Check the constraints of a tuple
	 */

	if (resultRelationDesc->rd_att->constr)
		ExecConstraints("ExecPut", resultRelationDesc, tuple, estate);

	/*
	 * insert the tuple
	 */
	newId = heap_insert(resultRelationDesc,	tuple); /* heap tuple */
	IncrAppended();

	/*
	 * process indices
	 *
	 * Note: heap_insert adds a new tuple to a relation.  As a side effect,
	 * the tupleid of the new tuple is placed in the new tuple's t_ctid
	 * field.
	 */
	numIndices = resultRelationInfo->ri_NumIndices;
	if (numIndices > 0)
		ExecInsertIndexTuples(slot, tupleid, estate, true);
        
        if ( ItemPointerIsValid(tupleid) ) {
            ctid=tuple->t_self;
     lput:;
            result = heap_delete(resultRelationDesc, tupleid, &ctid, estate->es_snapshot);
            switch (result)
            {
                    case HeapTupleSelfUpdated:
                    case HeapTupleMayBeUpdated:
                            break;

                    case HeapTupleUpdated:
                            if (t_info->XactIsoLevel == XACT_SERIALIZABLE)
                                    elog(ERROR, "Can't serialize access due to concurrent update");
                            else if (!(ItemPointerEquals(tupleid, &ctid)))
                            {
                                    TupleTableSlot *epqslot = EvalPlanQual(estate,
                                                      resultRelationInfo->ri_RangeTableIndex, &ctid);

                                    if (!TupIsNull(epqslot))
                                    {
                                            *tupleid = ctid;
                                            ctid = tuple->t_self;
                                            goto lput;
                                    }
                            }
                    case HeapTupleBeingUpdated:
                        /* heap tuple is being updated and we started after them so just forget about the update */
                    default:
                            elog(ERROR, "Unknown status %u from heap_delete during put operation", result);
                            return 0;
            }      
        } else {
			result = 0;
		}
        
	(estate->es_processed)++;
	estate->es_lastoid = newId;

	/* AFTER ROW INSERT Triggers */
	if (resultRelationDesc->trigdesc) {
		ExecARInsertTriggers(resultRelationDesc, tuple);
	}
        
	return result;
}


/* ----------------------------------------------------------------
 *		ExecAppend
 *
 *		APPENDs are trickier.. we have to insert the tuple into
 *		the base relation and insert appropriate tuples into the
 *		index relations.
 * ----------------------------------------------------------------
 */

void
ExecAppend(TupleTableSlot *slot,
		   ItemPointer tupleid,
		   EState *estate)
{
	HeapTuple	tuple;
	RelationInfo *resultRelationInfo;
	Relation	resultRelationDesc;
	int			numIndices;
	Oid			newId;

	/*
	 * get the heap tuple out of the tuple table slot
	 */
	tuple = slot->val;

	/*
	 * get information on the result relation
	 */
	resultRelationInfo = estate->es_result_relation_info;
	resultRelationDesc = resultRelationInfo->ri_RelationDesc;

	/*
	 * have to add code to preform unique checking here. cim -12/1/89
	 */

	/* BEFORE ROW INSERT Triggers */
	if (resultRelationDesc->trigdesc &&
	resultRelationDesc->trigdesc->n_before_row[TRIGGER_EVENT_INSERT] > 0)
	{
		HeapTuple	newtuple;

		newtuple = ExecBRInsertTriggers(resultRelationDesc, tuple);

		if (newtuple == NULL)	/* "do nothing" */
			return;

		if (newtuple != tuple)	/* modified by Trigger(s) */
		{
			heap_freetuple(tuple);
			ExecStoreTuple(newtuple,slot,false);
			tuple = slot->val;
		}
	}

	/*
	 * Check the constraints of a tuple
	 */

	if (resultRelationDesc->rd_att->constr)
		ExecConstraints("ExecAppend", resultRelationDesc, tuple, estate);

	/*
	 * insert the tuple
	 */
	newId = heap_insert(resultRelationDesc,		/* relation desc */
						tuple); /* heap tuple */
	IncrAppended();

	/*
	 * process indices
	 *
	 * Note: heap_insert adds a new tuple to a relation.  As a side effect,
	 * the tupleid of the new tuple is placed in the new tuple's t_ctid
	 * field.
	 */
	numIndices = resultRelationInfo->ri_NumIndices;
	if (numIndices > 0)
		ExecInsertIndexTuples(slot, NULL, estate, false);
	(estate->es_processed)++;
	estate->es_lastoid = newId;

	/* AFTER ROW INSERT Triggers */
	if (resultRelationDesc->trigdesc)
		ExecARInsertTriggers(resultRelationDesc, tuple);
}

/* ----------------------------------------------------------------
 *		ExecDelete
 *
 *		DELETE is like append, we delete the tuple and its
 *		index tuples.
 * ----------------------------------------------------------------
 */
void
ExecDelete(TupleTableSlot *slot,
		   ItemPointer tupleid,
		   EState *estate)
{
	RelationInfo *resultRelationInfo;
	Relation	resultRelationDesc;
	ItemPointerData ctid = *tupleid;
	int			result;
        TransactionInfo* t_info = GetTransactionInfo();
	/*
	 * get the result relation information
	 */
	resultRelationInfo = estate->es_result_relation_info;
	resultRelationDesc = resultRelationInfo->ri_RelationDesc;

	/* BEFORE ROW DELETE Triggers */
	if (resultRelationDesc->trigdesc &&
	resultRelationDesc->trigdesc->n_before_row[TRIGGER_EVENT_DELETE] > 0)
	{
		bool		dodelete;

		dodelete = ExecBRDeleteTriggers(estate, tupleid);

		if (!dodelete)			/* "do nothing" */
			return;
	}

	/*
	 * delete the tuple
	 */
ldelete:;
	result = heap_delete(resultRelationDesc, tupleid, &ctid,estate->es_snapshot);
	switch (result)
	{
                case HeapTupleBeingUpdated:
                    /* heap tuple is being updated and we started after them so just forget about the update */
                    return;
                case HeapTupleSelfUpdated:
			return;

		case HeapTupleMayBeUpdated:
			break;
                
		case HeapTupleUpdated:
			if (t_info->XactIsoLevel == XACT_SERIALIZABLE)
				elog(ERROR, "Can't serialize access due to concurrent update");
			else if (!(ItemPointerEquals(tupleid, &ctid)))
			{
				TupleTableSlot *epqslot = EvalPlanQual(estate,
						  resultRelationInfo->ri_RangeTableIndex, &ctid);

				if (!TupIsNull(epqslot))
				{
					*tupleid = ctid;
					goto ldelete;
				}
			}
			return;

		default:
			elog(ERROR, "Unknown status %u from heap_delete", result);
			return;
	}

	IncrDeleted();
	(estate->es_processed)++;

	/*
	 * Note: Normally one would think that we have to delete index tuples
	 * associated with the heap tuple now..
	 *
	 * ... but in POSTGRES, we have no need to do this because the vacuum
	 * daemon automatically opens an index scan and deletes index tuples
	 * when it finds deleted heap tuples. -cim 9/27/89
	 */

	/* AFTER ROW DELETE Triggers */
	if (resultRelationDesc->trigdesc)
		ExecARDeleteTriggers(estate, tupleid);

}

/* ----------------------------------------------------------------
 *		ExecReplace
 *
 *		note: we can't run replace queries with transactions
 *		off because replaces are actually appends and our
 *		scan will mistakenly loop forever, replacing the tuple
 *		it just appended..	This should be fixed but until it
 *		is, we don't want to get stuck in an infinite loop
 *		which corrupts your database..
 * ----------------------------------------------------------------
 */
void
ExecReplace(TupleTableSlot *slot,
			ItemPointer tupleid,
			EState *estate)
{
	HeapTuple	tuple;
	RelationInfo *resultRelationInfo;
	Relation	resultRelationDesc;
	ItemPointerData ctid;
	int			result;
	int			numIndices;
        TransactionInfo* t_info = GetTransactionInfo();
  //      int loops = 0;
	/*
	 * abort the operation if not running transactions
	 */
	if (IsBootstrapProcessingMode())
	{
		elog(DEBUG, "ExecReplace: replace can't run without transactions");
		return;
	}

	/*
	 * get the heap tuple out of the tuple table slot
	 */
	tuple = slot->val;

	/*
	 * get the result relation information
	 */
	resultRelationInfo = estate->es_result_relation_info;
	resultRelationDesc = resultRelationInfo->ri_RelationDesc;

	/*
	 * have to add code to preform unique checking here. in the event of
	 * unique tuples, this becomes a deletion of the original tuple
	 * affected by the replace. cim -12/1/89
	 */

	/* BEFORE ROW UPDATE Triggers */
	if (resultRelationDesc->trigdesc &&
	resultRelationDesc->trigdesc->n_before_row[TRIGGER_EVENT_UPDATE] > 0)
	{
		HeapTuple	newtuple;

		newtuple = ExecBRUpdateTriggers(estate, tupleid, tuple);

		if (newtuple == NULL)	/* "do nothing" */
			return;

		if (newtuple != tuple)	/* modified by Trigger(s) */
		{
			heap_freetuple(tuple);
			ExecStoreTuple(newtuple,slot,false);
			tuple = slot->val;
		}
	}

	/*
	 * Check the constraints of a tuple
	 */

	if (resultRelationDesc->rd_att->constr)
		ExecConstraints("ExecReplace", resultRelationDesc, tuple, estate);

	/*
	 * replace the heap tuple
	 */
lreplace:;
	result = heap_update(resultRelationDesc, tupleid, tuple, &ctid, estate->es_snapshot);
	switch (result)
	{
		case HeapTupleSelfUpdated:
			return;

		case HeapTupleMayBeUpdated:
			break;

		case HeapTupleUpdated:
			if (t_info->XactIsoLevel == XACT_SERIALIZABLE)
				elog(ERROR, "Can't serialize access due to concurrent update");
			else if (!(ItemPointerEquals(tupleid, &ctid)))
			{
				TupleTableSlot *epqslot = EvalPlanQual(estate,
						  resultRelationInfo->ri_RangeTableIndex, &ctid);

				if (!TupIsNull(epqslot))
				{
					*tupleid = ctid;
					tuple = ExecRemoveJunk(estate->es_junkFilter, epqslot);
					slot = ExecStoreTuple(tuple, slot,false);
//                                        loops++;
					goto lreplace;
				}
			}
			return;
                case HeapTupleBeingUpdated:
                    /* heap tuple is being updated and we started after them so just forget about the update */
                    return;
                default:
			elog(ERROR, "Unknown status %u from heap_update", result);
			return;
	}

	IncrReplaced();
	(estate->es_processed)++;

	/*
	 * Note: instead of having to update the old index tuples associated
	 * with the heap tuple, all we do is form and insert new index
	 * tuples..  This is because replaces are actually deletes and inserts
	 * and index tuple deletion is done automagically by the vaccuum
	 * deamon.. All we do is insert new index tuples.  -cim 9/27/89
	 */

	/*
	 * process indices
	 *
	 * heap_update updates a tuple in the base relation by invalidating it
	 * and then appending a new tuple to the relation.	As a side effect,
	 * the tupleid of the new tuple is placed in the new tuple's t_ctid
	 * field.  So we now insert index tuples using the new tupleid stored
	 * there.
	 */

	numIndices = resultRelationInfo->ri_NumIndices;
	if (numIndices > 0)
		ExecInsertIndexTuples(slot, NULL, estate, false);

	/* AFTER ROW UPDATE Triggers */
	if (resultRelationDesc->trigdesc)
		ExecARUpdateTriggers(estate, tupleid, tuple);
}

static char *
ExecRelCheck(Relation rel, HeapTuple tuple, EState *estate)
{
	int			ncheck = rel->rd_att->constr->num_check;
	ConstrCheck *check = rel->rd_att->constr->check;
	ExprContext *econtext = makeNode(ExprContext);
	TupleTableSlot *slot = ExecCreateTableSlot();
	RangeTblEntry *rte = makeNode(RangeTblEntry);
	List	   *rtlist;
	List	   *qual;
	int			i;


	ExecStoreTuple(tuple,slot,false);
	ExecSetSlotDescriptor(slot,rel->rd_att);

	rte->relname = RelationGetRelationName(rel);
	rte->ref = makeNode(Attr);
	rte->ref->relname = rte->relname;
	rte->relid = RelationGetRelid(rel);
	/* inh, inFromCl, inJoinSet, skipAcl won't be used, leave them zero */
	rtlist = lcons(rte, NIL);
	econtext->ecxt_scantuple = slot;	/* scan tuple slot */
	econtext->ecxt_innertuple = NULL;	/* inner tuple slot */
	econtext->ecxt_outertuple = NULL;	/* outer tuple slot */
	econtext->ecxt_relation = rel;		/* relation */
	econtext->ecxt_relid = 0;	/* relid */
	econtext->ecxt_param_list_info = NULL;		/* param list info */
	econtext->ecxt_param_exec_vals = NULL;		/* exec param values */
	econtext->ecxt_range_table = rtlist;		/* range table */

	if (estate->es_result_relation_constraints == NULL)
	{
		estate->es_result_relation_constraints =
			(List **) palloc(ncheck * sizeof(List *));

		for (i = 0; i < ncheck; i++)
		{
			qual = (List *) stringToNode(check[i].ccbin);
			estate->es_result_relation_constraints[i] = qual;
		}
	}

	for (i = 0; i < ncheck; i++)
	{
		qual = estate->es_result_relation_constraints[i];

		/*
		 * NOTE: SQL92 specifies that a NULL result from a constraint
		 * expression is not to be treated as a failure.  Therefore, tell
		 * ExecQual to return TRUE for NULL.
		 */
		if (!ExecQual(qual, econtext, true))
			return check[i].ccname;
	}
	
	ExecClearTuple(slot);
	pfree(slot);
	pfree(rte);
	pfree(rtlist);
	pfree(econtext);

	return (char *) NULL;

}

void
ExecConstraints(char *caller, Relation rel, HeapTuple tuple, EState *estate)
{

	Assert(rel->rd_att->constr);

	if (rel->rd_att->constr->has_not_null)
	{
		int			attrChk;

		for (attrChk = 1; attrChk <= rel->rd_att->natts; attrChk++)
		{
			if (rel->rd_att->attrs[attrChk - 1]->attnotnull && heap_attisnull(tuple, attrChk))
				elog(ERROR, "%s: Fail to add null value in not null attribute %s",
					 caller, NameStr(rel->rd_att->attrs[attrChk - 1]->attname));
		}
	}

	if (rel->rd_att->constr->num_check > 0)
	{
		char	   *failed;

		if ((failed = ExecRelCheck(rel, tuple, estate)) != NULL)
			elog(ERROR, "%s: rejected due to CHECK constraint %s", caller, failed);
	}

	return;
}

TupleTableSlot *
EvalPlanQual(EState *estate, Index rti, ItemPointer tid)
{
	evalPlanQual *epq = (evalPlanQual *) estate->es_evalPlanQual;
	evalPlanQual *oldepq;
	EState	   *epqstate = NULL;
	Relation	relation;
	Buffer		buffer;
	HeapTupleData tuple;
	bool		endNode = true;
	Env*	env = GetEnv();
        SnapshotHolder*  holder = GetSnapshotHolder();

	Assert(rti != 0);

	if (epq != NULL && epq->rti == 0)
	{
		Assert(!(estate->es_useEvalPlan) &&
			   epq->estate.es_evalPlanQual == NULL);
		epq->rti = rti;
		endNode = false;
	}

	/*
	 * If this is request for another RTE - Ra, - then we have to check
	 * wasn't PlanQual requested for Ra already and if so then Ra' row was
	 * updated again and we have to re-start old execution for Ra and
	 * forget all what we done after Ra was suspended. Cool? -:))
	 */
	if (epq != NULL && epq->rti != rti &&
		epq->estate.es_evTuple[rti - 1] != NULL)
	{
		do
		{
			/* pop previous PlanQual from the stack */
			epqstate = &(epq->estate);
			oldepq = (evalPlanQual *) epqstate->es_evalPlanQual;
			Assert(oldepq->rti != 0);
			/* stop execution */
			ExecEndNode(epq->plan);
			epqstate->es_tupleTable->next = 0;
			heap_freetuple(epqstate->es_evTuple[epq->rti - 1]);
			epqstate->es_evTuple[epq->rti - 1] = NULL;
			/* push current PQ to freePQ stack */
			oldepq->free = epq;
			epq = oldepq;
		} while (epq->rti != rti);
		estate->es_evalPlanQual = (Pointer) epq;
	}

	/*
	 * If we are requested for another RTE then we have to suspend
	 * execution of current PlanQual and start execution for new one.
	 */
	if (epq == NULL || epq->rti != rti)
	{
		/* try to reuse plan used previously */
		evalPlanQual *newepq = (epq != NULL) ? epq->free : NULL;

		if (newepq == NULL)		/* first call or freePQ stack is empty */
		{
			newepq = (evalPlanQual *) palloc(sizeof(evalPlanQual));
			/* Init EState */
			epqstate = &(newepq->estate);
			memset(epqstate, 0, sizeof(EState));
			epqstate->type = T_EState;
			epqstate->es_direction = ForwardScanDirection;
			epqstate->es_snapshot = estate->es_snapshot;
			epqstate->es_range_table = estate->es_range_table;
			epqstate->es_param_list_info = estate->es_param_list_info;
			if (estate->es_origPlan->nParamExec > 0)
				epqstate->es_param_exec_vals = (ParamExecData *)
					palloc(estate->es_origPlan->nParamExec *
						   sizeof(ParamExecData));
			epqstate->es_tupleTable =
				ExecCreateTupleTable(estate->es_tupleTable->size);
			/* ... rest */
			newepq->plan = copyObject(estate->es_origPlan);
			newepq->free = NULL;
			epqstate->es_evTupleNull = (bool *)
				palloc(length(estate->es_range_table) * sizeof(bool));
			if (epq == NULL)	/* first call */
			{
				epqstate->es_evTuple = (HeapTuple *)
					palloc(length(estate->es_range_table) * sizeof(HeapTuple));
				memset(epqstate->es_evTuple, 0,
					 length(estate->es_range_table) * sizeof(HeapTuple));
			}
			else
				epqstate->es_evTuple = epq->estate.es_evTuple;
		}
		else
			epqstate = &(newepq->estate);
		/* push current PQ to the stack */
		epqstate->es_evalPlanQual = (Pointer) epq;
		epq = newepq;
		estate->es_evalPlanQual = (Pointer) epq;
		epq->rti = rti;
		endNode = false;
	}

	epqstate = &(epq->estate);

	/*
	 * Ok - we're requested for the same RTE (-:)). I'm not sure about
	 * ability to use ExecReScan instead of ExecInitNode, so...
	 */
	if (endNode)
	{
		ExecEndNode(epq->plan);
		epqstate->es_tupleTable->next = 0;
	}

	/* free old RTE' tuple */
	if (epqstate->es_evTuple[epq->rti - 1] != NULL)
	{
		heap_freetuple(epqstate->es_evTuple[epq->rti - 1]);
		epqstate->es_evTuple[epq->rti - 1] = NULL;
	}

	/* ** fetch tid tuple ** */
	if (estate->es_result_relation_info != NULL &&
		estate->es_result_relation_info->ri_RangeTableIndex == rti)
		relation = estate->es_result_relation_info->ri_RelationDesc;
	else
	{
		List	   *l;

		foreach(l, estate->es_rowMark)
		{
			if (((execRowMark *) lfirst(l))->rti == rti)
				break;
		}
		relation = ((execRowMark *) lfirst(l))->relation;
	}
	tuple.t_self = *tid;
	for (;;)
	{
		heap_fetch(relation, holder->SnapshotDirty, &tuple, &buffer);
		if (tuple.t_data != NULL)
		{
			TransactionId xwait = holder->SnapshotDirty->xmax;
			if (TransactionIdIsValid(holder->SnapshotDirty->xmin))
			{
				elog(NOTICE, "EvalPlanQual: t_xmin:%lld is uncommitted ?!",(uint64)holder->SnapshotDirty->xmin);
				ReleaseBuffer(relation, buffer);
				TransactionId xwait = GetCheckpointId();
				XactLockTableWait(xwait);
				continue;
			}
			/*
			 * If tuple is being updated by other transaction then we have
			 * to wait for its commit/abort.
			 */
			if (TransactionIdIsValid(xwait))
			{
				ReleaseBuffer(relation, buffer);
				XactLockTableWait(xwait);
				continue;
			}

			/*
			 * Nice! We got tuple - now copy it.
			 */
			if (epqstate->es_evTuple[epq->rti - 1] != NULL)
				heap_freetuple(epqstate->es_evTuple[epq->rti - 1]);
			epqstate->es_evTuple[epq->rti - 1] = heap_copytuple(&tuple);
			ReleaseBuffer(relation, buffer);
			break;
		}

		/*
		 * Ops! Invalid tuple. Have to check is it updated or deleted.
		 * Note that it's possible to get invalid SnapshotDirty->tid if
		 * tuple updated by this transaction. Have we to check this ?
		 */
		if (ItemPointerIsValid(&(holder->SnapshotDirty->tid)) &&
			!(ItemPointerEquals(&(tuple.t_self), &(holder->SnapshotDirty->tid))))
		{
			tuple.t_self = holder->SnapshotDirty->tid;	/* updated ... */
			continue;
		}

		/*
		 * Deleted or updated by this transaction. Do not (re-)start
		 * execution of this PQ. Continue previous PQ.
		 */
		oldepq = (evalPlanQual *) epqstate->es_evalPlanQual;
		if (oldepq != NULL)
		{
			Assert(oldepq->rti != 0);
			/* push current PQ to freePQ stack */
			oldepq->free = epq;
			epq = oldepq;
			epqstate = &(epq->estate);
			estate->es_evalPlanQual = (Pointer) epq;
		}
		else
		{
			epq->rti = 0;		/* this is the first (oldest) */
			estate->es_useEvalPlan = false;		/* PQ - mark as free and	  */
			return (NULL);		/* continue Query execution   */
		}
	}

	if (estate->es_origPlan->nParamExec > 0)
		memset(epqstate->es_param_exec_vals, 0,
			   estate->es_origPlan->nParamExec * sizeof(ParamExecData));
	memset(epqstate->es_evTupleNull, false,
		   length(estate->es_range_table) * sizeof(bool));
	Assert(epqstate->es_tupleTable->next == 0);
	ExecInitNode(epq->plan, epqstate);

	/*
	 * For UPDATE/DELETE we have to return tid of actual row we're
	 * executing PQ for.
	 */
	*tid = tuple.t_self;

	return (EvalPlanQualNext(estate));
}

static TupleTableSlot *
EvalPlanQualNext(EState *estate)
{
	evalPlanQual *epq = (evalPlanQual *) estate->es_evalPlanQual;
	EState	   *epqstate = &(epq->estate);
	evalPlanQual *oldepq;
	TupleTableSlot *slot;

	Assert(epq->rti != 0);

lpqnext:;
	slot = ExecProcNode(epq->plan);

	/*
	 * No more tuples for this PQ. Continue previous one.
	 */
	if (TupIsNull(slot))
	{
		ExecEndNode(epq->plan);
		epqstate->es_tupleTable->next = 0;
		heap_freetuple(epqstate->es_evTuple[epq->rti - 1]);
		epqstate->es_evTuple[epq->rti - 1] = NULL;
		/* pop old PQ from the stack */
		oldepq = (evalPlanQual *) epqstate->es_evalPlanQual;
		if (oldepq == (evalPlanQual *) NULL)
		{
			epq->rti = 0;		/* this is the first (oldest) */
			estate->es_useEvalPlan = false;		/* PQ - mark as free and	  */
			return (NULL);		/* continue Query execution   */
		}
		Assert(oldepq->rti != 0);
		/* push current PQ to freePQ stack */
		oldepq->free = epq;
		epq = oldepq;
		epqstate = &(epq->estate);
		estate->es_evalPlanQual = (Pointer) epq;
		goto lpqnext;
	}

	return (slot);
}

static void
EndEvalPlanQual(EState *estate)
{
	evalPlanQual *epq = (evalPlanQual *) estate->es_evalPlanQual;
	EState	   *epqstate = &(epq->estate);
	evalPlanQual *oldepq;

	if (epq->rti == 0)			/* plans already shutdowned */
	{
		Assert(epq->estate.es_evalPlanQual == NULL);
		return;
	}

	for (;;)
	{
		ExecEndNode(epq->plan);
		epqstate->es_tupleTable->next = 0;
		if (epqstate->es_evTuple[epq->rti - 1] != NULL)
		{
			heap_freetuple(epqstate->es_evTuple[epq->rti - 1]);
			epqstate->es_evTuple[epq->rti - 1] = NULL;
		}
		/* pop old PQ from the stack */
		oldepq = (evalPlanQual *) epqstate->es_evalPlanQual;
		if (oldepq == (evalPlanQual *) NULL)
		{
			epq->rti = 0;		/* this is the first (oldest) */
			estate->es_useEvalPlan = false;		/* PQ - mark as free */
			break;
		}
		Assert(oldepq->rti != 0);
		/* push current PQ to freePQ stack */
		oldepq->free = epq;
		epq = oldepq;
		epqstate = &(epq->estate);
		estate->es_evalPlanQual = (Pointer) epq;
	}
}
