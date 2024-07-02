/*-------------------------------------------------------------------------
 *
 * defrem.h
 *	  POSTGRES define and remove utility definitions.
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 *
 *-------------------------------------------------------------------------
 */
#ifndef DEFREM_H
#define DEFREM_H

#include "nodes/parsenodes.h"
#include "tcop/dest.h"

/*
 * prototypes in defind.c
 */
PG_EXTERN void DefineIndex(char *heapRelationName,
			char *indexRelationName,
			char *accessMethodName,
			List *attributeList,
			List *parameterList,
			bool unique,
			bool primary,
			Expr *predicate,
			List *rangetable);
PG_EXTERN void ExtendIndex(char *indexRelationName,
			Expr *predicate,
			List *rangetable);
PG_EXTERN void RemoveIndex(char *name);
PG_EXTERN void ReindexIndex(const char *indexRelationName, bool force,bool exclusive);
PG_EXTERN void ReindexTable(const char *relationName, bool force,bool exclusive);
PG_EXTERN void ReindexDatabase(const char *databaseName, bool force, bool all,bool exclusive);

/*
 * prototypes in define.c
 */
PG_EXTERN void CreateFunction(ProcedureStmt *stmt, CommandDest dest);
PG_EXTERN void DefineOperator(char *name, List *parameters);
PG_EXTERN void DefineAggregate(char *name, List *parameters);
PG_EXTERN void DefineType(char *name, List *parameters);
PG_EXTERN void CreateFunction(ProcedureStmt *stmt, CommandDest dest);

/*
 * prototypes in remove.c
 */
PG_EXTERN void RemoveFunction(char *functionName, int nargs, List *argNameList, Node* retNode);
PG_EXTERN void RemoveOperator(char *operatorName,
			   char *typeName1, char *typeName2);
PG_EXTERN void RemoveType(char *typeName);
PG_EXTERN void RemoveAggregate(char *aggName, char *aggType);

#endif	 /* DEFREM_H */
