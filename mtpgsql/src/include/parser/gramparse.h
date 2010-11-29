/*-------------------------------------------------------------------------
 *
 * gramparse.h
 *	  scanner support routines.  used by both the bootstrap lexer
 * as well as the normal lexer
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * $Id: gramparse.h,v 1.1.1.1 2006/08/12 00:22:22 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */

#ifndef GRAMPARSE_H
#define GRAMPARSE_H				/* include once only */


#ifdef __cplusplus
extern "C" {
#endif 
/* from scan.l */
PG_EXTERN void init_io(void);
PG_EXTERN void yyerror(const char *message);

/* from gram.y */
PG_EXTERN Oid	param_type(int t);
PG_EXTERN Oid	param_type_name(char* t);
PG_EXTERN void parser_init(char* stmt, Oid *typev, char** names, int nargs);
PG_EXTERN int	parser_parse(List** parsetree);
PG_EXTERN void parser_destroy();

#ifdef __cplusplus
}
#endif 

#endif	 /* GRAMPARSE_H */
