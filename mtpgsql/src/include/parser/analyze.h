/*-------------------------------------------------------------------------
 *
 * analyze.h
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * $Id: analyze.h,v 1.1.1.1 2006/08/12 00:22:22 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */
#ifndef ANALYZE_H
#define ANALYZE_H

#include "parser/parse_node.h"

PG_EXTERN List *parse_analyze(List *pl, ParseState *parentParseState);

PG_EXTERN void create_select_list(Node *ptr, List **select_list, bool *unionall_present);
PG_EXTERN Node *A_Expr_to_Expr(Node *ptr, bool *intersect_present);

#endif	 /* ANALYZE_H */
