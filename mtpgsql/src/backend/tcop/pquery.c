/*-------------------------------------------------------------------------
 *
 * pquery.c
 *	  POSTGRES process query command code
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

#include "executor/execdefs.h"
#include "executor/executor.h"
#include "tcop/pquery.h"
#include "utils/ps_status.h"

#include "commands/command.h"

static char *CreateOperationTag(int operationType);
static void ProcessQueryDesc(QueryDesc *queryDesc, Node *limoffset,
				 Node *limcount);


/* ----------------------------------------------------------------
 *		CreateQueryDesc
 * ----------------------------------------------------------------
 */
QueryDesc  *
CreateQueryDesc(Query *parsetree,
				Plan *plantree,
				CommandDest dest)
{
	QueryDesc  *qd = (QueryDesc *) palloc(sizeof(QueryDesc));

	qd->operation = parsetree->commandType;		/* operation */
	qd->parsetree = parsetree;	/* parse tree */
	qd->plantree = plantree;	/* plan */
	qd->dest = dest;			/* output dest */
	return qd;
}

/* ----------------------------------------------------------------
 *		CreateExecutorState
 *
 *		Note: this may someday take parameters -cim 9/18/89
 * ----------------------------------------------------------------
 */
EState *
CreateExecutorState(void)
{
	EState	   *state;

	/* ----------------
	 *	create a new executor state
	 * ----------------
	 */
	state = makeNode(EState);

	/* ----------------
	 *	initialize the Executor State structure
	 * ----------------
	 */
	state->es_direction = ForwardScanDirection;
	state->es_range_table = NIL;

	state->es_into_relation_descriptor = NULL;
	state->es_result_relation_info = NULL;

	state->es_param_list_info = NULL;
	state->es_param_exec_vals = NULL;

	state->es_BaseId = 0;
	state->es_tupleTable = NULL;

	state->es_junkFilter = NULL;

	/* ----------------
	 *	return the executor state structure
	 * ----------------
	 */
	return state;
}

/* ----------------------------------------------------------------
 *		CreateOperationTag
 *
 *		utility to get a string representation of the
 *		query operation.
 * ----------------------------------------------------------------
 */
static char *
CreateOperationTag(int operationType)
{
	char	   *tag;

	switch (operationType)
	{
		case CMD_SELECT:
			tag = "SELECT";
			break;
                case CMD_PUT:
			tag = "PUT";
			break;
            case CMD_INSERT:
			tag = "INSERT";
			break;
		case CMD_DELETE:
			tag = "DELETE";
			break;
		case CMD_UPDATE:
			tag = "UPDATE";
			break;
		default:
			elog(DEBUG, "CreateOperationTag: unknown operation type %d",
				 operationType);
			tag = NULL;
			break;
	}

	return tag;
}

/* ----------------
 *		ProcessPortal
 * ----------------
 */

void
ProcessPortal(char *portalName,
			  Query *parseTree,
			  Plan *plan,
			  EState *state,
			  TupleDesc attinfo,
			  CommandDest dest)
{
	Portal		portal;
	MemoryContext portalContext;

	/* ----------------
	 *	 convert the current blank portal into the user-specified
	 *	 portal and initialize the state and query descriptor.
	 * ----------------
	 */

	portal = CreatePortal(portalName);

	PortalSetQuery(portal,
				   CreateQueryDesc(parseTree, plan, dest),
				   attinfo,
				   state,
				   PortalCleanup);

	/* ----------------
	 *	now create a new blank portal and switch to it.
	 *	Otherwise, the new named portal will be cleaned.
	 *
	 *	Note: portals will only be supported within a BEGIN...END
	 *	block in the near future.  Later, someone will fix it to
	 *	do what is possible across transaction boundries. -hirohama
	 * ----------------
	 */
	portalContext = (MemoryContext) PortalGetHeapMemory(portal);

	MemoryContextSwitchTo(portalContext);
}


/* ----------------------------------------------------------------
 *		ProcessQueryDesc
 *
 *		Read the comments for ProcessQuery() below...
 * ----------------------------------------------------------------
 */
static void
ProcessQueryDesc(QueryDesc *queryDesc, Node *limoffset, Node *limcount)
{
	Query	   *parseTree;
	Plan	   *plan;
	int			operation;
	char	   *tag = NULL;
	EState	   *state;
	TupleDesc	attinfo;

	bool		isRetrieveIntoPortal;
	bool		isRetrieveIntoRelation;
	char	   *intoName = NULL;
	CommandDest dest;

	/* ----------------
	 *	get info from the query desc
	 * ----------------
	 */
	parseTree = queryDesc->parsetree;
	plan = queryDesc->plantree;

	operation = queryDesc->operation;
	PS_SET_STATUS(tag = CreateOperationTag(operation));
	dest = queryDesc->dest;

	/* ----------------
	 *	initialize portal/into relation status
	 * ----------------
	 */
	isRetrieveIntoPortal = false;
	isRetrieveIntoRelation = false;

	if (operation == CMD_SELECT)
	{
		if (parseTree->isPortal)
		{
			isRetrieveIntoPortal = true;
			intoName = parseTree->into;
			if (parseTree->isBinary)
			{

				/*
				 * For internal format portals, we change Remote
				 * (externalized form) to RemoteInternal (internalized
				 * form)
				 */
				dest = queryDesc->dest = RemoteInternal;
			}
		}
		else if (parseTree->into != NULL)
		{
			/* select into table */
			isRetrieveIntoRelation = true;
		}

	}

	/* ----------------
	 *	when performing a retrieve into, we override the normal
	 *	communication destination during the processing of the
	 *	the query.	This only affects the tuple-output function
	 *	- the correct destination will still see BeginCommand()
	 *	and EndCommand() messages.
	 * ----------------
	 */
	if (isRetrieveIntoRelation)
		queryDesc->dest = (int) None;

	/* ----------------
	 *	create a default executor state..
	 * ----------------
	 */
	state = CreateExecutorState();

	/* ----------------
	 *	call ExecStart to prepare the plan for execution
	 * ----------------
	 */
	attinfo = ExecutorStart(queryDesc, state);

	/* ----------------
	 *	 report the query's result type information
	 *	 back to the front end or to whatever destination
	 *	 we're dealing with.
	 * ----------------
	 */
	BeginCommand(NULL,
				 operation,
				 attinfo,
				 isRetrieveIntoRelation,
				 isRetrieveIntoPortal,
				 tag,
				 dest);

	/* ----------------
	 *	Named portals do not do a "fetch all" initially, so now
	 *	we return since ExecMain has been called with EXEC_START
	 *	to initialize the query plan.
	 *
	 *	Note: ProcessPortal transforms the current "blank" portal
	 *		  into a named portal and creates a new blank portal so
	 *		  everything we allocated in the current "blank" memory
	 *		  context will be preserved across queries.  -cim 2/22/91
	 * ----------------
	 */
	if (isRetrieveIntoPortal)
	{
		ProcessPortal(intoName,
					  parseTree,
					  plan,
					  state,
					  attinfo,
					  dest);

		EndCommand(tag, dest);
		return;
	}

	/* ----------------
	 *	 Now we get to the important call to ExecutorRun() where we
	 *	 actually run the plan..
	 * ----------------
	 */
	ExecutorRun(queryDesc, state, EXEC_RUN, limoffset, limcount);

	/* save infos for EndCommand */
	UpdateCommandInfo(operation, state->es_lastoid, state->es_processed);

	/* ----------------
	 *	 now, we close down all the scans and free allocated resources...
	 * with ExecutorEnd()
	 * ----------------
	 */
	ExecutorEnd(queryDesc, state);

	/* ----------------
	 *	Notify the destination of end of processing.
	 * ----------------
	 */
	EndCommand(tag, dest);
}

/* ----------------------------------------------------------------
 *		ProcessQuery
 *
 *		Execute a plan, the non-parallel version
 * ----------------------------------------------------------------
 */

void
ProcessQuery(Query *parsetree,
			 Plan *plan,
			 CommandDest dest)
{
	QueryDesc  *queryDesc;
	extern int	dontExecute;	/* from postgres.c */
	extern void print_plan(Plan *p, Query *parsetree);	/* from print.c */

	queryDesc = CreateQueryDesc(parsetree, plan, dest);

	if (dontExecute)
	{
		/* don't execute it, just show the query plan */
		print_plan(plan, parsetree);
	}
	else
		ProcessQueryDesc(queryDesc, parsetree->limitOffset,
						 parsetree->limitCount);
}
