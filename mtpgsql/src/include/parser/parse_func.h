/*-------------------------------------------------------------------------
 *
 * parse_func.h
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
#ifndef PARSER_FUNC_H
#define PARSER_FUNC_H

#include "parser/parse_node.h"

/*
 *	This structure is used to explore the inheritance hierarchy above
 *	nodes in the type tree in order to disambiguate among polymorphic
 *	functions.
 */
typedef struct _InhPaths
{
	int			nsupers;		/* number of superclasses */
	Oid			self;			/* this class */
	Oid		   *supervec;		/* vector of superclasses */
} InhPaths;

/*
 *	This structure holds a list of possible functions or operators that
 *	agree with the known name and argument types of the function/operator.
 */
typedef struct _CandidateList
{
	Oid		   *args;
	struct _CandidateList *next;
}		   *CandidateList;

PG_EXTERN Node *ParseNestedFuncOrColumn(ParseState *pstate, Attr *attr,
						int *curr_resno, int precedence);
PG_EXTERN Node *ParseFuncOrColumn(ParseState *pstate,
				  char *funcname, List *fargs,
				  bool agg_star, bool agg_distinct,
				  int *curr_resno, int precedence);


PG_EXTERN List *setup_base_tlist(Oid typeid);

PG_EXTERN bool typeInheritsFrom(Oid subclassTypeId, Oid superclassTypeId);

PG_EXTERN void func_error(char *caller, char *funcname,
		   int nargs, Oid *argtypes, char *msg);

#endif	 /* PARSE_FUNC_H */
