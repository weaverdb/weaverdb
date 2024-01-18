%option prefix="Int_yy"
%option bison-bridge
%{
/*-------------------------------------------------------------------------
 *
 * bootscanner.lex
 *	  a lexical scanner for the bootstrap parser
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  $Header: /cvs/weaver/mtpgsql/src/backend/bootstrap/bootscanner.l,v 1.1.1.1 2006/08/12 00:20:07 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */
#include <time.h>

#include "postgres.h"
#include "env/env.h"

#include "access/attnum.h"
#include "access/funcindex.h"
#include "access/htup.h"
#include "access/itup.h"
#include "access/skey.h"
#include "access/strat.h"
#include "access/tupdesc.h"
#include "bootstrap/bootstrap.h"
#include "catalog/pg_am.h"
#include "catalog/pg_attribute.h"
#include "catalog/pg_class.h"
#include "nodes/nodes.h"
#include "nodes/parsenodes.h"
#include "nodes/pg_list.h"
#include "nodes/primnodes.h"
#include "parser/parserinfo.h"
#include "parser/scansup.h"
#include "rewrite/prs2lock.h"
#include "storage/block.h"
#include "storage/fd.h"
#include "storage/itemptr.h"
#include "storage/off.h"
#include "utils/nabstime.h"
#include "utils/rel.h"

#include "bootstrap_tokens.h"

#define			YY_NO_UNPUT
#define YY_NO_INPUT
#define YY_DECL int yylex \
               (YYSTYPE * yylval_param , void* scanner)
/* some versions of lex define this as a macro */
#if defined(yywrap)
#undef yywrap
#endif /* yywrap */
static int Int_yywrap(void);

%}

D		[0-9A-F]
hex		\\x{D}{D}
Exp		[Ee][-+]?{D}+
id		([A-Za-z0-9_]|{hex}|\-)+
sid		\"([^\"])*\"
arrayid [A-Za-z0-9_]+\[{D}*\]

%%

open			{ return(OPEN); }

close			{ return(XCLOSE); }

create			{ return(XCREATE); }

OID				{ return(OBJ_ID); }
bootstrap		{ return(XBOOTSTRAP); }
_null_			{ return(NULLVAL); }

insert			{ return(INSERT_TUPLE); }

","				{ return(COMMA); }
"="				{ return(EQUALS); }
"("				{ return(LPAREN); }
")"				{ return(RPAREN); }

[\n]			{ GetParserInfo()->yyline++; }
[\t]			;
" "				;

^\#[^\n]* ; /* drop everything after "#" for comments */


"declare"		{ return(XDECLARE); }
"build"			{ return(XBUILD); }
"indices"		{ return(INDICES); }
"unique"		{ return(UNIQUE); }
"index"			{ return(INDEX); }
"on"			{ return(ON); }
"using"			{ return(USING); }
{arrayid}		{
					yylval_param->ival = EnterString(MapArrayTypeName((char*)Int_yytext));
					return(ID);
				}
{id}			{
					char   *newid = scanstr((char*)Int_yytext);
					yylval_param->ival = EnterString(newid);
					pfree(newid);
					return(ID);
				}
{sid}			{
					char   *newid;
					Int_yytext[strlen(Int_yytext)-1] = '\0'; /* strip off quotes */
					newid = scanstr((char*)Int_yytext+1);
					yylval_param->ival = EnterString(newid);
					pfree(newid);
					Int_yytext[strlen(Int_yytext)] = '"'; /* restore quotes */
					return(ID);
				}

(-)?{D}+"."{D}*({Exp})? |
(-)?{D}*"."{D}+({Exp})? |
(-)?{D}+{Exp}			{
							yylval_param->ival = EnterString((char*)Int_yytext);
							return(CONST);
						}

.				{
					printf("syntax error %d : -> %s\n", GetParserInfo()->yyline, Int_yytext);
				}



%%

int
Int_yywrap(void)
{
	return 1;
}

void
Int_yyerror(void* info, const char *str)
{
	fprintf(stderr,"\tsyntax error %d : %s",GetParserInfo()->yyline, str);
}
