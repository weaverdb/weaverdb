/*-------------------------------------------------------------------------
 *
 * prs2lock.h
 *	  data structures for POSTGRES Rule System II (rewrite rules only)
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 *
 *-------------------------------------------------------------------------
 */
#ifndef PRS2LOCK_H
#define PRS2LOCK_H


#include "access/attnum.h"
#include "nodes/pg_list.h"
#include "nodes/nodes.h"

/*
 * RewriteRule -
 *	  holds a info for a rewrite rule
 *
 */
typedef struct RewriteRule
{
	Oid			ruleId;
	CmdType		event;
	AttrNumber	attrno;
	Node	   *qual;
	List	   *actions;
	bool		isInstead;
} RewriteRule;

/*
 * RuleLock -
 *	  all rules that apply to a particular relation. Even though we only
 *	  have the rewrite rule system left and these are not really "locks",
 *	  the name is kept for historical reasons.
 */
typedef struct RuleLock
{
	int			numLocks;
	RewriteRule **rules;
} RuleLock;

#endif	 /* REWRITE_H */
