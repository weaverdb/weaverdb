/*-------------------------------------------------------------------------
 *
 * execnodes.h
 *	  definitions for executor state nodes
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 *
 *-------------------------------------------------------------------------
 */
#ifndef PRINT_H
#define PRINT_H

#include "nodes/parsenodes.h"
#include "nodes/plannodes.h"

/*
 * nodes/{outfuncs.c,print.c}
 */
#define nodeDisplay		pprint

PG_EXTERN char *plannode_type(Plan *p);
PG_EXTERN void print(void *obj);
PG_EXTERN void pprint(void *obj);
PG_EXTERN void print_rt(List *rtable);
PG_EXTERN void print_expr(Node *expr, List *rtable);
PG_EXTERN void print_pathkeys(List *pathkeys, List *rtable);
PG_EXTERN void print_tl(List *tlist, List *rtable);
PG_EXTERN void print_slot(TupleTableSlot *slot);
PG_EXTERN void print_plan_recursive(Plan *p, Query *parsetree,
					 int indentLevel, char *label);
PG_EXTERN void print_plan(Plan *p, Query *parsetree);

#endif	 /* PRINT_H */
