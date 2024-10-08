/*-------------------------------------------------------------------------
 *
 * explain.c
 *	  Explain the query execution plan
 *
 * Portions Copyright (c) 2000-2024, Myron Scott  <myron@weaverdb.org>
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994-5, Regents of the University of California
 *
 *
 *-------------------------------------------------------------------------
 */


#include "postgres.h"
#include "env/env.h"
#include "commands/explain.h"
#include "lib/stringinfo.h"
#include "nodes/print.h"
#include "optimizer/planner.h"
#include "parser/parsetree.h"
#include "rewrite/rewriteHandler.h"
#include "utils/relcache.h"
#include "libpq/libpq.h"


typedef struct ExplainState
{
	/* options */
	bool		printCost;		/* print cost */
	bool		printNodes;		/* do nodeToString() instead */
	/* other states */
	List	   *rtable;			/* range table */
} ExplainState;

static char *Explain_PlanToString(Plan *plan, ExplainState *es);
static void ExplainOneQuery(Query *query, bool verbose, CommandDest dest);

/* Convert a null string pointer into "<>" */
#define stringStringInfo(s) (((s) == NULL) ? "<>" : (s))


/*
 * ExplainQuery -
 *	  print out the execution plan for a given query
 *
 */
void
ExplainQuery(Query *query, bool verbose, CommandDest dest)
{
	List	   *rewritten;
	List	   *l;

	/* rewriter and planner may not work in aborted state? */
	if (IsAbortedTransactionBlockState())
	{
		elog(NOTICE, "(transaction aborted): %s",
			 "queries ignored until END");
		return;
	}

	/* rewriter and planner will not cope with utility statements */
	if (query->commandType == CMD_UTILITY)
	{
		elog(NOTICE, "Utility statements have no plan structure");
		return;
	}

	/* Rewrite through rule system */
	rewritten = QueryRewrite(query);

	/* In the case of an INSTEAD NOTHING, tell at least that */
	if (rewritten == NIL)
	{
		elog(NOTICE, "Query rewrites to nothing");
		return;
	}

	/* Explain every plan */
	foreach(l, rewritten)
		ExplainOneQuery(lfirst(l), verbose, dest);
}

/*
 * ExplainOneQuery -
 *	  print out the execution plan for one query
 *
 */
static void
ExplainOneQuery(Query *query, bool verbose, CommandDest dest)
{
	char	   *s;
	Plan	   *plan;
	ExplainState *es;

	/* plan the query */
	plan = planner(query);

	/* pg_plan could have failed */
	if (plan == NULL)
		return;

	es = (ExplainState *) palloc(sizeof(ExplainState));
	MemSet(es, 0, sizeof(ExplainState));

	es->printCost = true;		/* default */

	if (verbose)
		es->printNodes = true;

	es->rtable = query->rtable;

	if (es->printNodes)
	{
		s = nodeToString(plan);
		if (s)
		{
                        if (dest == Local) {
                            pq_putbytes(s, strlen(s));
                            pq_putbytes("\n", 1);
                            pq_flush();
                        } else {
                            elog(NOTICE, "QUERY DUMP:\n\n%s", s);
                        }
			pfree(s);
		}
	}

	if (es->printCost)
	{
		s = Explain_PlanToString(plan, es);
		if (s)
		{
                        if (dest == Local) {
                            pq_putbytes(s, strlen(s));
                            pq_flush();
                        } else {
                            elog(NOTICE, "QUERY PLAN:\n\n%s", s);
                        }
			pfree(s);
		}
	}

	if (es->printNodes && dest != Local)
		pprint(plan);			/* display in postmaster log file */

	pfree(es);
}

/*****************************************************************************
 *
 *****************************************************************************/

/*
 * explain_outNode -
 *	  converts a Node into ascii string and append it to 'str'
 */
static void
explain_outNode(StringInfo str, Plan *plan, int indent, ExplainState *es)
{
	List	   *l;
        List        *indexlist;
	Relation	relation;
	char	   *pname;
	int			i;

	if (plan == NULL)
	{
		appendStringInfo(str, "\n");
		return;
	}

	switch (nodeTag(plan))
	{
		case T_Result:
			pname = "Result";
			break;
		case T_Append:
			pname = "Append";
			break;
		case T_NestLoop:
			pname = "Nested Loop";
			break;
		case T_MergeJoin:
			pname = "Merge Join";
			break;
		case T_HashJoin:
			pname = "Hash Join";
			break;
		case T_SeqScan:
			pname = "Seq Scan";
			break;
		case T_DelegatedSeqScan:
			pname = "Delegated Seq Scan";
			break;
		case T_IndexScan:
			pname = "Index Scan";
			break;
		case T_DelegatedIndexScan:
			pname = "Delegated Index Scan";
			break;
		case T_Noname:
			pname = "Noname Scan";
			break;
		case T_Material:
			pname = "Materialize";
			break;
		case T_Sort:
			pname = "Sort";
			break;
		case T_Group:
			pname = "Group";
			break;
		case T_Agg:
			pname = "Aggregate";
			break;
		case T_Unique:
			pname = "Unique";
			break;
		case T_Hash:
			pname = "Hash";
			break;
		case T_TidScan:
			pname = "Tid Scan";
			break;
		default:
			pname = "???";
			break;
	}

	appendStringInfo(str, pname);
	switch (nodeTag(plan))
	{
		case T_IndexScan:
                case T_DelegatedIndexScan:
			if (ScanDirectionIsBackward(((IndexScan *) plan)->indxorderdir))
				appendStringInfo(str, " Backward");
			appendStringInfo(str, " using ");
			i = 0;
/* this is a dirty hack MKS to squeeze in delegated */
                        indexlist = ( nodeTag(plan) == T_DelegatedIndexScan ) ?
                            lconsi(((DelegatedIndexScan *) plan)->indexid, NIL) :
                            ((IndexScan *) plan)->indxid;

                        foreach(l, indexlist)
                        {
                                relation = RelationIdGetRelation(lfirsti(l),DEFAULTDBOID);
                                Assert(relation);
                                appendStringInfo(str, "%s%s",
                                                                 (++i > 1) ? ", " : "",
                                        stringStringInfo(RelationGetRelationName(relation)));
                                /* drop relcache refcount from RelationIdGetRelation */
                                RelationDecrementReferenceCount(relation);
                        }
			/* FALL THRU */
		case T_SeqScan:
		case T_DelegatedSeqScan:
		case T_TidScan:
			if (((Scan *) plan)->scanrelid > 0)
			{
				RangeTblEntry *rte = nth(((Scan *) plan)->scanrelid - 1, es->rtable);

				appendStringInfo(str, " on %s",
								 stringStringInfo(rte->relname));
				if (rte->ref != NULL)
				{
					if ((strcmp(rte->ref->relname, rte->relname) != 0)
						|| (length(rte->ref->attrs) > 0))
					{
						appendStringInfo(str, " %s",
									stringStringInfo(rte->ref->relname));

						if (length(rte->ref->attrs) > 0)
						{
							List	   *c;
							int			firstEntry = true;

							appendStringInfo(str, " (");
							foreach(c, rte->ref->attrs)
							{
								if (!firstEntry)
								{
									appendStringInfo(str, ", ");
									firstEntry = false;
								}
								appendStringInfo(str, "%s", strVal(lfirst(c)));
							}
							appendStringInfo(str, ")");
						}
					}
				}
			}
			break;
		default:
			break;
	}
	if (es->printCost)
	{
		appendStringInfo(str, "  (cost=%.2f..%.2f rows=%.0f width=%d)",
						 plan->startup_cost, plan->total_cost,
						 plan->plan_rows, plan->plan_width);
	}
	appendStringInfo(str, "\n");

	/* initPlan-s */
	if (plan->initPlan)
	{
		List	   *saved_rtable = es->rtable;
		List	   *lst;

		for (i = 0; i < indent; i++)
			appendStringInfo(str, "  ");
		appendStringInfo(str, "  InitPlan\n");
		foreach(lst, plan->initPlan)
		{
			es->rtable = ((SubPlan *) lfirst(lst))->rtable;
			for (i = 0; i < indent; i++)
				appendStringInfo(str, "  ");
			appendStringInfo(str, "    ->  ");
			explain_outNode(str, ((SubPlan *) lfirst(lst))->plan, indent + 2, es);
		}
		es->rtable = saved_rtable;
	}

	/* lefttree */
	if (outerPlan(plan))
	{
		for (i = 0; i < indent; i++)
			appendStringInfo(str, "  ");
		appendStringInfo(str, "  ->  ");
		explain_outNode(str, outerPlan(plan), indent + 3, es);
	}

	/* righttree */
	if (innerPlan(plan))
	{
		for (i = 0; i < indent; i++)
			appendStringInfo(str, "  ");
		appendStringInfo(str, "  ->  ");
		explain_outNode(str, innerPlan(plan), indent + 3, es);
	}

	/* subPlan-s */
	if (plan->subPlan)
	{
		List	   *saved_rtable = es->rtable;
		List	   *lst;

		for (i = 0; i < indent; i++)
			appendStringInfo(str, "  ");
		appendStringInfo(str, "  SubPlan\n");
		foreach(lst, plan->subPlan)
		{
			es->rtable = ((SubPlan *) lfirst(lst))->rtable;
			for (i = 0; i < indent; i++)
				appendStringInfo(str, "  ");
			appendStringInfo(str, "    ->  ");
			explain_outNode(str, ((SubPlan *) lfirst(lst))->plan, indent + 4, es);
		}
		es->rtable = saved_rtable;
	}

	if (nodeTag(plan) == T_Append)
	{
		List	   *saved_rtable = es->rtable;
		List	   *lst;
		int			whichplan = 0;
		Append	   *appendplan = (Append *) plan;

		foreach(lst, appendplan->appendplans)
		{
			Plan	   *subnode = (Plan *) lfirst(lst);

			if (appendplan->inheritrelid > 0)
			{
				RangeTblEntry *rtentry;

				rtentry = nth(whichplan, appendplan->inheritrtable);
				Assert(rtentry != NULL);
				rt_store(appendplan->inheritrelid, es->rtable, rtentry);
			}
			else
				es->rtable = nth(whichplan, appendplan->unionrtables);

			for (i = 0; i < indent; i++)
				appendStringInfo(str, "  ");
			appendStringInfo(str, "    ->  ");

			explain_outNode(str, subnode, indent + 4, es);

			whichplan++;
		}
		es->rtable = saved_rtable;
	}
}

static char *
Explain_PlanToString(Plan *plan, ExplainState *es)
{
	StringInfoData str;

	/* see stringinfo.h for an explanation of this maneuver */
	initStringInfo(&str);
	if (plan != NULL)
		explain_outNode(&str, plan, 0, es);
	return str.data;
}
