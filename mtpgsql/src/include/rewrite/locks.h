/*-------------------------------------------------------------------------
 *
 * locks.h
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
#ifndef LOCKS_H
#define LOCKS_H

#include "nodes/parsenodes.h"
#include "rewrite/prs2lock.h"

PG_EXTERN List *matchLocks(CmdType event, RuleLock *rulelocks, int varno,
		   Query *parsetree);
PG_EXTERN void checkLockPerms(List *locks, Query *parsetree, int rt_index);

#endif	 /* LOCKS_H */
