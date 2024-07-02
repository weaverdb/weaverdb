/*-------------------------------------------------------------------------
 *
 * rewriteRemove.h
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
#ifndef REWRITEREMOVE_H
#define REWRITEREMOVE_H

PG_EXTERN char *RewriteGetRuleEventRel(char *rulename);
PG_EXTERN void RemoveRewriteRule(char *ruleName);
PG_EXTERN void RelationRemoveRules(Oid relid);

#endif	 /* REWRITEREMOVE_H */
