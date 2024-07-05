/*-------------------------------------------------------------------------
 *
 * parser.c
 *
 * Portions Copyright (c) 2000-2024, Myron Scott  <myron@weaverdb.org>
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include "env/env.h"

#include "parser/analyze.h"
#include "parser/gramparse.h"
#include "parser/parser.h"
#include "parser/parse_expr.h"
#include "parser/parserinfo.h"

/*
 * parser-- returns a list of parse trees
 */
List *
parser(char *str, Oid *typev,char** argnames, int nargs)
{
	List	   *queryList;
        List       *parsetree;
	int			yyresult;

/* this makes sure that the parser info is grabbed from the 
    global env and not the pointer cache
*/
	init_io();		

	parse_expr_init();

	parser_init(str, typev, argnames, nargs);
	yyresult = parser_parse(&parsetree);
        parser_destroy();
        
	if (yyresult)				/* error */
		return (List *) NULL;

	queryList = parse_analyze(parsetree, NULL);

	return queryList;
}

