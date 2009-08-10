/*-------------------------------------------------------------------------
 *
 * cost.h
 *	  prototypes for costsize.c and clausesel.c.
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * $Id: cost.h,v 1.1.1.1 2006/08/12 00:22:21 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */
#ifndef COST_H
#define COST_H

#include "nodes/relation.h"

/* defaults for costsize.c's Cost parameters */
/* NB: cost-estimation code should use the variables, not these constants! */
#define DEFAULT_EFFECTIVE_CACHE_SIZE  200.0	/* measured in pages */
#define DEFAULT_RANDOM_PAGE_COST  1.2
#define DEFAULT_DELEGATED_RANDOM_PAGE_COST  0.9
#define DEFAULT_CPU_TUPLE_COST	0.005
#define DEFAULT_CPU_DELEGATED_TUPLE_COST	0.004
#define DEFAULT_THREAD_STARTUP_COST	900.0
#define DEFAULT_DELEGATION_STARTUP_COST	100.0
#define DEFAULT_CPU_INDEX_TUPLE_COST 0.0002
#define DEFAULT_CPU_DELEGATED_INDEX_TUPLE_COST 0.00019
#define DEFAULT_CPU_OPERATOR_COST  0.00001

/* defaults for function attributes used for expensive function calculations */
#define BYTE_PCT 100
#define PERBYTE_CPU 0
#define PERCALL_CPU 0
#define OUTIN_RATIO 100


/*
 * prototypes for costsize.c
 *	  routines to compute costs and sizes
 */

/* parameter variables and flags */


PG_EXTERN void cost_seqscan(Path *path, RelOptInfo *baserel);
PG_EXTERN void cost_delegatedseqscan(Path *path, RelOptInfo *baserel);
PG_EXTERN void cost_index(Path *path, Query *root,
		   RelOptInfo *baserel, IndexOptInfo *index,
		   List *indexQuals, bool is_injoin);
PG_EXTERN void cost_delegated_index(Path *path, Query *root,
		   RelOptInfo *baserel, IndexOptInfo *index,
		   List *indexQuals, bool is_injoin);
PG_EXTERN void cost_tidscan(Path *path, RelOptInfo *baserel, List *tideval);
PG_EXTERN void cost_sort(Path *path, List *pathkeys, double tuples, int width);
PG_EXTERN void cost_nestloop(Path *path, Path *outer_path, Path *inner_path,
			  List *restrictlist);
PG_EXTERN void cost_mergejoin(Path *path, Path *outer_path, Path *inner_path,
			   List *restrictlist,
			   List *outersortkeys, List *innersortkeys);
PG_EXTERN void cost_hashjoin(Path *path, Path *outer_path, Path *inner_path,
			  List *restrictlist, Selectivity innerdisbursion);
PG_EXTERN Cost cost_qual_eval(List *quals);
PG_EXTERN void set_baserel_size_estimates(Query *root, RelOptInfo *rel);
PG_EXTERN void set_joinrel_size_estimates(Query *root, RelOptInfo *rel,
						   RelOptInfo *outer_rel,
						   RelOptInfo *inner_rel,
						   List *restrictlist);

/*
 * prototypes for clausesel.c
 *	  routines to compute clause selectivities
 */
PG_EXTERN Selectivity restrictlist_selectivity(Query *root,
						 List *restrictinfo_list,
						 int varRelid);
PG_EXTERN Selectivity clauselist_selectivity(Query *root,
					   List *clauses,
					   int varRelid);
PG_EXTERN Selectivity clause_selectivity(Query *root,
				   Node *clause,
				   int varRelid);

#endif	 /* COST_H */
