/*-------------------------------------------------------------------------
 *
 * makefuncs.h
 *	  prototypes for the creator functions (for primitive nodes)
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * $Id: makefuncs.h,v 1.1.1.1 2006/08/12 00:22:19 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */
#ifndef MAKEFUNC_H
#define MAKEFUNC_H

#include "nodes/parsenodes.h"

PG_EXTERN Oper *makeOper(Oid opno,
		 Oid opid,
		 Oid opresulttype,
		 int opsize,
		 FunctionCachePtr op_fcache);

PG_EXTERN Var *makeVar(Index varno,
		AttrNumber varattno,
		Oid vartype,
		int32 vartypmod,
		Index varlevelsup);

PG_EXTERN TargetEntry *makeTargetEntry(Resdom *resdom, Node *expr);

PG_EXTERN Resdom *makeResdom(AttrNumber resno,
		   Oid restype,
		   int32 restypmod,
		   char *resname,
		   Index reskey,
		   Oid reskeyop,
		   bool resjunk);

PG_EXTERN Const *makeConst(Oid consttype,
		  int constlen,
		  Datum constvalue,
		  bool constisnull,
		  bool constbyval,
		  bool constisset,
		  bool constiscast);

PG_EXTERN Attr *
			makeAttr(char *relname, char *attname);

#endif	 /* MAKEFUNC_H */
