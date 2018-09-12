/*-------------------------------------------------------------------------
 *
 * paths.h
 *	  prototypes for various files in optimizer/path (were separate
 *	  header files)
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * $Id: paths.h,v 1.1.1.1 2006/08/12 00:22:22 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */
#ifndef PATHS_H
#define PATHS_H

#include "nodes/relation.h"

/* default GEQO threshold (default value for geqo_rels) */
#define GEQO_RELS 11


/*
 * allpaths.c
 */
PG_EXTERN const bool enable_geqo;
PG_EXTERN const int	geqo_rels;

PG_EXTERN RelOptInfo *make_one_rel(Query *root);

/*
 * indxpath.c
 *	  routines to generate index paths
 */
PG_EXTERN void create_index_paths(Query *root, RelOptInfo *rel, List *indices,
				   List *restrictinfo_list,
				   List *joininfo_list);
PG_EXTERN Oid indexable_operator(Expr *clause, Oid opclass, Oid relam,
				   bool indexkey_on_left);
PG_EXTERN List *extract_or_indexqual_conditions(RelOptInfo *rel,
								IndexOptInfo *index,
								Expr *orsubclause);
PG_EXTERN List *expand_indexqual_conditions(List *indexquals);

/*
 * orindxpath.c
 *	  additional routines for indexable OR clauses
 */
PG_EXTERN void create_or_index_paths(Query *root, RelOptInfo *rel,
					  List *clauses);

/*
 * tidpath.h
 *	  routines to generate tid paths
 */
PG_EXTERN void create_tidscan_paths(Query *root, RelOptInfo *rel);

/*
 * joinpath.c
 *	   routines to create join paths
 */
PG_EXTERN void add_paths_to_joinrel(Query *root, RelOptInfo *joinrel,
					 RelOptInfo *outerrel,
					 RelOptInfo *innerrel,
					 List *restrictlist);

/*
 * joinrels.c
 *	  routines to determine which relations to join
 */
PG_EXTERN void make_rels_by_joins(Query *root, int level);
PG_EXTERN RelOptInfo *make_rels_by_clause_joins(Query *root,
						  RelOptInfo *old_rel,
						  List *other_rels);
PG_EXTERN RelOptInfo *make_rels_by_clauseless_joins(Query *root,
							  RelOptInfo *old_rel,
							  List *other_rels);

/*
 * pathkeys.c
 *	  utilities for matching and building path keys
 */
typedef enum
{
	PATHKEYS_EQUAL,				/* pathkeys are identical */
	PATHKEYS_BETTER1,			/* pathkey 1 is a superset of pathkey 2 */
	PATHKEYS_BETTER2,			/* vice versa */
	PATHKEYS_DIFFERENT			/* neither pathkey includes the other */
} PathKeysComparison;

PG_EXTERN void add_equijoined_keys(Query *root, RestrictInfo *restrictinfo);
PG_EXTERN List *canonicalize_pathkeys(Query *root, List *pathkeys);
PG_EXTERN PathKeysComparison compare_pathkeys(List *keys1, List *keys2);
PG_EXTERN bool pathkeys_contained_in(List *keys1, List *keys2);
PG_EXTERN Path *get_cheapest_path_for_pathkeys(List *paths, List *pathkeys,
							   CostSelector cost_criterion);
PG_EXTERN Path *get_cheapest_fractional_path_for_pathkeys(List *paths,
										  List *pathkeys,
										  double fraction);
PG_EXTERN List *build_index_pathkeys(Query *root, RelOptInfo *rel,
					 IndexOptInfo *index,
					 ScanDirection scandir);
PG_EXTERN List *build_join_pathkeys(List *outer_pathkeys,
					List *join_rel_tlist,
					List *equi_key_list);
PG_EXTERN List *make_pathkeys_for_sortclauses(List *sortclauses,
							  List *tlist);
PG_EXTERN List *find_mergeclauses_for_pathkeys(List *pathkeys,
							   List *restrictinfos);
PG_EXTERN List *make_pathkeys_for_mergeclauses(Query *root,
							   List *mergeclauses,
							   List *tlist);

#endif	 /* PATHS_H */
