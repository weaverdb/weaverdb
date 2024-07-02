/*-------------------------------------------------------------------------
 *
 * rewriteSupport.h
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
#ifndef REWRITESUPPORT_H
#define REWRITESUPPORT_H

#include "access/attnum.h"
#include "nodes/pg_list.h"

PG_EXTERN int	IsDefinedRewriteRule(char *ruleName);

PG_EXTERN void prs2_addToRelation(Oid relid, Oid ruleId, CmdType event_type,
				   AttrNumber attno, bool isInstead, Node *qual,
				   List *actions);
PG_EXTERN void prs2_deleteFromRelation(Oid relid, Oid ruleId);


#endif	 /* REWRITESUPPORT_H */
