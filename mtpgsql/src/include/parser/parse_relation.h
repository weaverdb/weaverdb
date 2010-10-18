/*-------------------------------------------------------------------------
 *
 * parse_relation.h
 *	  prototypes for parse_relation.c.
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * $Id: parse_relation.h,v 1.1.1.1 2006/08/12 00:22:22 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */
#ifndef PARSE_RELATION_H
#define PARSE_RELATION_H

#include "parser/parse_node.h"

PG_EXTERN RangeTblEntry *refnameRangeTableEntry(ParseState *pstate, char *refname);
PG_EXTERN int refnameRangeTablePosn(ParseState *pstate,
					  char *refname,
					  int *sublevels_up);
PG_EXTERN RangeTblEntry *colnameRangeTableEntry(ParseState *pstate, char *colname);
PG_EXTERN RangeTblEntry *addRangeTableEntry(ParseState *pstate,
				   char *relname,
				   Attr *ref,
				   bool inh,
				   bool inFromCl,
				   bool inJoinSet);
PG_EXTERN Attr *expandTable(ParseState *pstate, char *refname, bool getaliases);
PG_EXTERN List *expandAll(ParseState *pstate, char *relname, Attr *ref,
		  int *this_resno);
PG_EXTERN int	attnameAttNum(Relation rd, char *a);
PG_EXTERN int	specialAttNum(char *a);
PG_EXTERN bool attnameIsSet(Relation rd, char *name);
PG_EXTERN int	attnumAttNelems(Relation rd, int attid);
PG_EXTERN Oid	attnumTypeId(Relation rd, int attid);

#endif	 /* PARSE_RELATION_H */
