/*-------------------------------------------------------------------------
 *
 * parse_expr.h
 *
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * $Id: parse_expr.h,v 1.1.1.1 2006/08/12 00:22:22 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */
#ifndef PARSE_EXPR_H
#define PARSE_EXPR_H

#include "parser/parse_node.h"
#include "parser/parse_type.h"

#define EXPR_COLUMN_FIRST	1
#define EXPR_RELATION_FIRST 2

extern int	max_expr_depth;

PG_EXTERN Node *transformExpr(ParseState *pstate, Node *expr, int precedence);
PG_EXTERN Oid	exprType(Node *expr);
PG_EXTERN int32 exprTypmod(Node *expr);
PG_EXTERN bool exprIsLengthCoercion(Node *expr, int32 *coercedTypmod);
PG_EXTERN void parse_expr_init(void);

#endif	 /* PARSE_EXPR_H */
