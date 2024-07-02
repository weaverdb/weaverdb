/*-------------------------------------------------------------------------
 *
 * clauses.h
 *	  prototypes for clauses.c.
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 *
 *-------------------------------------------------------------------------
 */
#ifndef CLAUSES_H
#define CLAUSES_H

#include "nodes/relation.h"

PG_EXTERN Expr *make_clause(int type, Node *oper, List *args);

PG_EXTERN bool is_opclause(Node *clause);
PG_EXTERN Expr *make_opclause(Oper *op, Var *leftop, Var *rightop);
PG_EXTERN Var *get_leftop(Expr *clause);
PG_EXTERN Var *get_rightop(Expr *clause);

PG_EXTERN bool is_funcclause(Node *clause);
PG_EXTERN Expr *make_funcclause(Func *func, List *funcargs);

PG_EXTERN bool or_clause(Node *clause);
PG_EXTERN Expr *make_orclause(List *orclauses);

PG_EXTERN bool not_clause(Node *clause);
PG_EXTERN Expr *make_notclause(Expr *notclause);
PG_EXTERN Expr *get_notclausearg(Expr *notclause);

PG_EXTERN bool and_clause(Node *clause);
PG_EXTERN Expr *make_andclause(List *andclauses);
PG_EXTERN Expr *make_ands_explicit(List *andclauses);
PG_EXTERN List *make_ands_implicit(Expr *clause);

PG_EXTERN bool contain_agg_clause(Node *clause);
PG_EXTERN List *pull_agg_clause(Node *clause);

PG_EXTERN bool contain_subplans(Node *clause);
PG_EXTERN List *pull_subplans(Node *clause);
PG_EXTERN void check_subplans_for_ungrouped_vars(Node *clause, Query *query);

PG_EXTERN List *pull_constant_clauses(List *quals, List **constantQual);

PG_EXTERN void clause_get_relids_vars(Node *clause, Relids *relids, List **vars);
PG_EXTERN int	NumRelids(Node *clause);
PG_EXTERN void get_relattval(Node *clause, int targetrelid,
			  int *relid, AttrNumber *attno,
			  Datum *constval, int *flag);
PG_EXTERN void get_rels_atts(Node *clause, int *relid1,
			  AttrNumber *attno1, int *relid2, AttrNumber *attno2);
PG_EXTERN void CommuteClause(Expr *clause);

PG_EXTERN Node *eval_const_expressions(Node *node);

PG_EXTERN bool expression_tree_walker(Node *node, bool (*walker) (),
											   void *context);
PG_EXTERN Node *expression_tree_mutator(Node *node, Node *(*mutator) (),
												 void *context);

#define is_subplan(clause)	((clause) != NULL && \
							 IsA(clause, Expr) && \
							 ((Expr *) (clause))->opType == SUBPLAN_EXPR)

#endif	 /* CLAUSES_H */
