/*-------------------------------------------------------------------------
 *
 * user.h
 *
 *
 *
 *
 *-------------------------------------------------------------------------
 */
#ifndef USER_H
#define USER_H

#include "nodes/parsenodes.h"
#include "access/htup.h"

PG_EXTERN void CreateUser(CreateUserStmt *stmt);
PG_EXTERN void AlterUser(AlterUserStmt *stmt);
PG_EXTERN void DropUser(DropUserStmt *stmt);

PG_EXTERN void CreateGroup(CreateGroupStmt *stmt);
PG_EXTERN void AlterGroup(AlterGroupStmt *stmt, const char *tag);
PG_EXTERN void DropGroup(DropGroupStmt *stmt);

PG_EXTERN HeapTuple update_pg_pwd(void);

#endif	 /* USER_H */
