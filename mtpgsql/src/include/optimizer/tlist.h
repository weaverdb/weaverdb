/*-------------------------------------------------------------------------
 *
 * tlist.h
 *	  prototypes for tlist.c.
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 *
 *-------------------------------------------------------------------------
 */
#ifndef TLIST_H
#define TLIST_H

#include "nodes/relation.h"

PG_EXTERN TargetEntry *tlistentry_member(Node *node, List *targetlist);
PG_EXTERN Node *matching_tlist_expr(Node *node, List *targetlist);
PG_EXTERN Resdom *tlist_member(Node *node, List *targetlist);

PG_EXTERN void add_var_to_tlist(RelOptInfo *rel, Var *var);
PG_EXTERN TargetEntry *create_tl_element(Var *var, int resdomno);

PG_EXTERN List *new_unsorted_tlist(List *targetlist);
PG_EXTERN List *flatten_tlist(List *tlist);
PG_EXTERN List *add_to_flat_tlist(List *tlist, List *vars);

PG_EXTERN Var *get_expr(TargetEntry *tle);

PG_EXTERN TargetEntry *get_sortgroupclause_tle(SortClause *sortClause,
						List *targetList);
PG_EXTERN Node *get_sortgroupclause_expr(SortClause *sortClause,
						 List *targetList);

#endif	 /* TLIST_H */
