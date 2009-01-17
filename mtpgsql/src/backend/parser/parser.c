/*-------------------------------------------------------------------------
 *
 * parser.c
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  $Header: /cvs/weaver/mtpgsql/src/backend/parser/parser.c,v 1.1.1.1 2006/08/12 00:21:12 synmscott Exp $
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


static void DeleteBuffer(void);

/*
char*	   parseString;		
List	   *parsetree;
*/

#ifdef SETS_FIXED
static void fixupsets();
static void define_sets();

#endif

/*
 * parser-- returns a list of parse trees
 */
List *
parser(char *str, Oid *typev,char** argnames, int nargs)
{
	List	   *queryList;
	int			yyresult;

/* this makes sure that the parser info is grabbed from the 
    global env and not the pointer cache
*/
	ParserInfo* parseinfo = CaptureParserInfo();

	init_io();

	parseinfo->parseString = pstrdup(str);
	parseinfo->parsetree = NIL;			

	parser_init(typev, argnames, nargs);
	parse_expr_init();
	DeleteBuffer();

	yyresult = yyparse();


/*
	clearerr(stdin);
*/

	if (yyresult)				/* error */
		return (List *) NULL;

	queryList = parse_analyze(parseinfo->parsetree, NULL);

#ifdef SETS_FIXED

	/*
	 * Fixing up sets calls the parser, so it reassigns the global
	 * variable parsetree. So save the real parsetree.
	 */
	savetree = parseinfo->parsetree;
	foreach(parse, savetree)
	{							/* savetree is really a list of parses */

		/* find set definitions embedded in query */
		fixupsets((Query *) lfirst(parse));

	}
	return savetree;
#endif

	return queryList;
}

static void 
DeleteBuffer()
{
	GetParserInfo()->yy_init = 1;
	GetParserInfo()->yy_current_buffer = NULL;
}

#ifdef SETS_FIXED
static void
fixupsets(Query *parse)
{
	if (parse == NULL)
		return;
	if (parse->commandType == CMD_UTILITY)		/* utility */
		return;
	if (parse->commandType != CMD_INSERT)
		return;
	define_sets(parse);
}

/* Recursively find all of the Consts in the parsetree.  Some of
 * these may represent a set.  The value of the Const will be the
 * query (a string) which defines the set.	Call SetDefine to define
 * the set, and store the OID of the new set in the Const instead.
 */
static void
define_sets(Node *clause)
{
	Oid			setoid;
	Type		t = typeidType(OIDOID);
	Oid			typeoid = typeTypeId(t);
	Size		oidsize = typeLen(t);
	bool		oidbyval = typeByVal(t);

	if (clause == NULL)
		return;
	else if (IsA(clause, LispList))
	{
		define_sets(lfirst(clause));
		define_sets(lnext(clause));
	}
	else if (IsA(clause, Const))
	{
		if (get_constisnull((Const) clause) ||
			!get_constisset((Const) clause))
			return;
		setoid = SetDefine(((Const *) clause)->constvalue,
						   typeidTypeName(((Const *) clause)->consttype));
		set_constvalue((Const) clause, setoid);
		set_consttype((Const) clause, typeoid);
		set_constlen((Const) clause, oidsize);
		set_constypeByVal((Const) clause, oidbyval);
	}
	else if (IsA(clause, Iter))
		define_sets(((Iter *) clause)->iterexpr);
	else if (single_node(clause))
		return;
	else if (or_clause(clause) || and_clause(clause))
	{
		List	   *temp;

		/* mapcan */
		foreach(temp, ((Expr *) clause)->args)
			define_sets(lfirst(temp));
	}
	else if (is_funcclause(clause))
	{
		List	   *temp;

		/* mapcan */
		foreach(temp, ((Expr *) clause)->args)
			define_sets(lfirst(temp));
	}
	else if (IsA(clause, ArrayRef))
		define_sets(((ArrayRef *) clause)->refassgnexpr);
	else if (not_clause(clause))
		define_sets(get_notclausearg(clause));
	else if (is_opclause(clause))
	{
		define_sets(get_leftop(clause));
		define_sets(get_rightop(clause));
	}
}

#endif
