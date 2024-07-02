/*-------------------------------------------------------------------------
 *
 * rewriteManip.h
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
#ifndef REWRITEMANIP_H
#define REWRITEMANIP_H

#include "rewrite/rewriteHandler.h"

/* RewriteManip.c */
PG_EXTERN void OffsetVarNodes(Node *node, int offset, int sublevels_up);
PG_EXTERN void ChangeVarNodes(Node *node, int old_varno, int new_varno,
			   int sublevels_up);
PG_EXTERN void IncrementVarSublevelsUp(Node *node, int delta_sublevels_up,
						int min_sublevels_up);
PG_EXTERN void AddQual(Query *parsetree, Node *qual);
PG_EXTERN void AddHavingQual(Query *parsetree, Node *havingQual);
PG_EXTERN void AddNotQual(Query *parsetree, Node *qual);
PG_EXTERN void AddGroupClause(Query *parsetree, List *group_by, List *tlist);

PG_EXTERN bool checkExprHasAggs(Node *node);
PG_EXTERN bool checkExprHasSubLink(Node *node);

PG_EXTERN void FixNew(RewriteInfo *info, Query *parsetree);

PG_EXTERN void HandleRIRAttributeRule(Query *parsetree, List *rtable,
					   List *targetlist, int rt_index,
					   int attr_num, int *modified, int *badsql);

#endif	 /* REWRITEMANIP_H */
