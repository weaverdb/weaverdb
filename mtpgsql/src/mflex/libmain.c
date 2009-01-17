/* libmain - flex run-time support library "main" function */

/* $Header: /cvs/weaver/mtpgsql/src/mflex/libmain.c,v 1.1.1.1 2006/08/12 00:24:59 synmscott Exp $ */

extern int yylex();

int main( argc, argv )
int argc;
char *argv[];
	{
	while ( yylex() != 0 )
		;

	return 0;
	}
