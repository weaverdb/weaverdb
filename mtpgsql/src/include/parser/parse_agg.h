/*-------------------------------------------------------------------------
 *
 * parse_agg.h
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
#ifndef PARSE_AGG_H
#define PARSE_AGG_H

#include "parser/parse_node.h"

PG_EXTERN void AddAggToParseState(ParseState *pstate, Aggref *aggref);
PG_EXTERN void parseCheckAggregates(ParseState *pstate, Query *qry);
PG_EXTERN Aggref *ParseAgg(ParseState *pstate, char *aggname, Oid basetype,
		 List *args, bool agg_star, bool agg_distinct,
		 int precedence);
PG_EXTERN void agg_error(char *caller, char *aggname, Oid basetypeID);

#endif	 /* PARSE_AGG_H */
