/*-------------------------------------------------------------------------
 *
 * parse_clause.h
 *
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * $Id: parse_clause.h,v 1.1.1.1 2006/08/12 00:22:22 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */
#ifndef PARSE_CLAUSE_H
#define PARSE_CLAUSE_H

#include "parser/parse_node.h"

PG_EXTERN void makeRangeTable(ParseState *pstate, List *frmList);
PG_EXTERN void setTargetTable(ParseState *pstate, char *relname);
PG_EXTERN Node *transformWhereClause(ParseState *pstate, Node *where);
PG_EXTERN List *transformGroupClause(ParseState *pstate, List *grouplist,
					 List *targetlist);
PG_EXTERN List *transformSortClause(ParseState *pstate, List *orderlist,
					List *targetlist);
PG_EXTERN List *transformDistinctClause(ParseState *pstate, List *distinctlist,
						List *targetlist, List **sortClause);

PG_EXTERN List *addAllTargetsToSortList(List *sortlist, List *targetlist);
PG_EXTERN Index assignSortGroupRef(TargetEntry *tle, List *tlist);

#endif	 /* PARSE_CLAUSE_H */
