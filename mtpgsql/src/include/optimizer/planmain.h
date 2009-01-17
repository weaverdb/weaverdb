/*-------------------------------------------------------------------------
 *
 * planmain.h
 *	  prototypes for various files in optimizer/plan
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * $Id: planmain.h,v 1.1.1.1 2006/08/12 00:22:22 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */
#ifndef PLANMAIN_H
#define PLANMAIN_H

#include "nodes/plannodes.h"
#include "nodes/relation.h"

/*   from subselect.c   */
typedef struct optimizer_globals {
	Index	   		PlannerQueryLevel;		/* level of current query */
	List*			PlannerInitPlan;		/* init subplans for current query */
	List*			PlannerParamVar;		/* to get Var from Param->paramid */
	int	   			PlannerPlanId;			/* to assign unique ID to subquery plans */
/*   from variable.c     */
	bool 			_use_keyset_query_optimizer;
	int			PoolSize;
	int			Generations;
	long			RandomSeed;
	double			SelectionBias;
} OptimizerGlobals;

PG_EXTERN OptimizerGlobals* GetOptimizerInfo(void);

/*
 * prototypes for plan/planmain.c
 */
PG_EXTERN Plan *query_planner(Query *root, List *tlist, List *qual,
			  double tuple_fraction);

/*
 * prototypes for plan/createplan.c
 */
PG_EXTERN Plan *create_plan(Query *root, Path *best_path);

PG_EXTERN Sort *make_sort(List *tlist, Oid nonameid, Plan *lefttree,
		  int keycount);
PG_EXTERN Agg *make_agg(List *tlist, List *qual, Plan *lefttree);
PG_EXTERN Group *make_group(List *tlist, bool tuplePerGroup, int ngrp,
		   AttrNumber *grpColIdx, Plan *lefttree);
PG_EXTERN Noname *make_noname(List *tlist, List *pathkeys, Plan *subplan);
PG_EXTERN Unique *make_unique(List *tlist, Plan *lefttree, List *distinctList);
PG_EXTERN Result *make_result(List *tlist, Node *resconstantqual, Plan *subplan);

/*
 * prototypes for plan/initsplan.c
 */
PG_EXTERN void make_var_only_tlist(Query *root, List *tlist);
PG_EXTERN void add_restrict_and_join_to_rels(Query *root, List *clauses);
PG_EXTERN void add_missing_rels_to_query(Query *root);

/*
 * prototypes for plan/setrefs.c
 */
PG_EXTERN void set_plan_references(Plan *plan);
PG_EXTERN List *join_references(List *clauses, List *outer_tlist,
				List *inner_tlist, Index acceptable_rel);
PG_EXTERN void fix_opids(Node *node);

/*
 * prep/prepkeyset.c
 */
PG_EXTERN void transformKeySetQuery(Query *origNode);

#endif	 /* PLANMAIN_H */
