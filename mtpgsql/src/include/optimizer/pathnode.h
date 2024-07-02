/*-------------------------------------------------------------------------
 *
 * pathnode.h
 *	  prototypes for pathnode.c, indexnode.c, relnode.c.
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 *
 *-------------------------------------------------------------------------
 */
#ifndef PATHNODE_H
#define PATHNODE_H

#include "nodes/relation.h"

/*
 * prototypes for pathnode.c
 */
PG_EXTERN int compare_path_costs(Path *path1, Path *path2,
				   CostSelector criterion);
PG_EXTERN int compare_fractional_path_costs(Path *path1, Path *path2,
							  double fraction);
PG_EXTERN void set_cheapest(RelOptInfo *parent_rel);
PG_EXTERN void add_path(RelOptInfo *parent_rel, Path *new_path);

PG_EXTERN Path *create_seqscan_path(RelOptInfo *rel);
PG_EXTERN Path *create_delegated_seqscan_path(RelOptInfo *rel);
PG_EXTERN IndexPath *create_index_path(Query *root, RelOptInfo *rel,
				  IndexOptInfo *index,
				  List *restriction_clauses,
				  ScanDirection indexscandir);
PG_EXTERN IndexPath *create_delegated_index_path(Query *root, RelOptInfo *rel,
				  IndexOptInfo *index,
				  List *restriction_clauses,
				  ScanDirection indexscandir);
PG_EXTERN TidPath *create_tidscan_path(RelOptInfo *rel, List *tideval);

PG_EXTERN NestPath *create_nestloop_path(RelOptInfo *joinrel,
					 Path *outer_path,
					 Path *inner_path,
					 List *restrict_clauses,
					 List *pathkeys);

PG_EXTERN MergePath *create_mergejoin_path(RelOptInfo *joinrel,
					  Path *outer_path,
					  Path *inner_path,
					  List *restrict_clauses,
					  List *pathkeys,
					  List *mergeclauses,
					  List *outersortkeys,
					  List *innersortkeys);

PG_EXTERN HashPath *create_hashjoin_path(RelOptInfo *joinrel,
					 Path *outer_path,
					 Path *inner_path,
					 List *restrict_clauses,
					 List *hashclauses,
					 Selectivity innerdisbursion);

/*
 * prototypes for relnode.c
 */
PG_EXTERN RelOptInfo *get_base_rel(Query *root, int relid);
PG_EXTERN RelOptInfo *get_join_rel(Query *root, RelOptInfo *outer_rel,
			 RelOptInfo *inner_rel,
			 List **restrictlist_ptr);

/*
 * prototypes for indexnode.h
 */
PG_EXTERN List *find_relation_indices(Query *root, RelOptInfo *rel);

#endif	 /* PATHNODE_H */
