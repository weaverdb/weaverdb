/*-------------------------------------------------------------------------
 *
 * planner.h
 *	  prototypes for planner.c.
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * $Id: planner.h,v 1.1.1.1 2006/08/12 00:22:22 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */
#ifndef PLANNER_H
#define PLANNER_H

/*
*/

#include "nodes/parsenodes.h"
#include "nodes/plannodes.h"

PG_EXTERN Plan *planner(Query *parse);
PG_EXTERN Plan *subquery_planner(Query *parse, double tuple_fraction);
PG_EXTERN Plan *union_planner(Query *parse, double tuple_fraction);
PG_EXTERN void pg_checkretval(Oid rettype, List *querytree_list);

#endif	 /* PLANNER_H */
