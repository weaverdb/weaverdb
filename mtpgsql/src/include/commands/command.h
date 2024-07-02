/*-------------------------------------------------------------------------
 *
 * command.h
 *	  prototypes for command.c.
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 *
 *-------------------------------------------------------------------------
 */
#ifndef COMMAND_H
#define COMMAND_H

#include "utils/portal.h"
/*
extern MemoryContext PortalExecutorHeapMemory;
*/
/*
 * PerformPortalFetch
 *		Performs the POSTQUEL function FETCH.  Fetches count (or all if 0)
 * tuples in portal with name in the forward direction iff goForward.
 *
 * Exceptions:
 *		BadArg if forward invalid.
 *		"WARN" if portal not found.
 */
PG_EXTERN void PerformPortalFetch(char *name, bool forward, int count,
				   char *tag, CommandDest dest);

/*
 * PerformPortalClose
 *		Performs the POSTQUEL function CLOSE.
 */
PG_EXTERN void PerformPortalClose(char *name, CommandDest dest);

PG_EXTERN void PortalCleanup(Portal portal);

/*
 * ALTER TABLE variants
 */
PG_EXTERN void AlterTableAddColumn(const char *relationName,
					bool inh, ColumnDef *colDef);

PG_EXTERN void AlterTableAlterColumn(const char *relationName,
					  bool inh, const char *colName,
					  Node *newDefault);

PG_EXTERN void AlterTableDropColumn(const char *relationName,
					 bool inh, const char *colName,
					 int behavior);

PG_EXTERN void AlterTableAddConstraint(const char *relationName,
						bool inh, Node *newConstraint);

PG_EXTERN void AlterTableDropConstraint(const char *relationName,
						 bool inh, const char *constrName,
						 int behavior);

/*
 * LOCK
 */
PG_EXTERN void LockTableCommand(LockStmt *lockstmt);

#endif	 /* COMMAND_H */
