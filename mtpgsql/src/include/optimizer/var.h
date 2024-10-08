/*-------------------------------------------------------------------------
 *
 * var.h
 *	  prototypes for var.c.
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 *
 *-------------------------------------------------------------------------
 */
#ifndef VAR_H
#define VAR_H

#include "nodes/primnodes.h"

PG_EXTERN List *pull_varnos(Node *me);
PG_EXTERN bool contain_var_clause(Node *clause);
PG_EXTERN List *pull_var_clause(Node *clause, bool includeUpperVars);

#endif	 /* VAR_H */
