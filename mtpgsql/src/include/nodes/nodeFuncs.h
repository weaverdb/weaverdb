/*-------------------------------------------------------------------------
 *
 * nodeFuncs.h
 *
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 *
 *-------------------------------------------------------------------------
 */
#ifndef NODEFUNCS_H
#define NODEFUNCS_H

#include "nodes/primnodes.h"

PG_EXTERN bool single_node(Node *node);
PG_EXTERN bool var_is_outer(Var *var);
PG_EXTERN bool var_is_rel(Var *var);
PG_EXTERN Oper *replace_opid(Oper *oper);
PG_EXTERN bool non_null(Expr *c);

#endif	 /* NODEFUNCS_H */
