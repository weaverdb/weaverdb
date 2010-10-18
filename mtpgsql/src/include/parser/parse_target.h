/*-------------------------------------------------------------------------
 *
 * parse_target.h
 *
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * $Id: parse_target.h,v 1.1.1.1 2006/08/12 00:22:22 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */
#ifndef PARSE_TARGET_H
#define PARSE_TARGET_H

#include "parser/parse_node.h"

PG_EXTERN List *transformTargetList(ParseState *pstate, List *targetlist);
PG_EXTERN TargetEntry *transformTargetEntry(ParseState *pstate,
					 Node *node, Node *expr,
					 char *colname, bool resjunk);
PG_EXTERN void updateTargetListEntry(ParseState *pstate, TargetEntry *tle,
					  char *colname, int attrno,
					  List *indirection);
PG_EXTERN Node *CoerceTargetExpr(ParseState *pstate, Node *expr,
				 Oid type_id, Oid attrtype, int32 attrtypmod);
PG_EXTERN List *checkInsertTargets(ParseState *pstate, List *cols,
				   List **attrnos);

#endif	 /* PARSE_TARGET_H */
