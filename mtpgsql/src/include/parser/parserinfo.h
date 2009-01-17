/*-------------------------------------------------------------------------
 *
 * parserinfo.h
 *
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * $Id: parserinfo.h,v 1.1.1.1 2006/08/12 00:22:22 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */
#ifndef PARSERINFO_H
#define PARSERINFO_H

#include "nodes/pg_list.h"

typedef struct ParserInfo {
/*  for parser   */
	char* 			yytext;

	int 			yyleng;
	int 			yy_start;
	int 			yy_init;
	char 			yy_hold_char;
	int 			yy_n_chars;
	void*			yy_current_buffer;
	char*			yy_c_buf_p;
	int				yyline;
	
	List* 			parsetree;
	char* 			parseString;
	char* 			parseCh;

	char*			literalbuf;		
	int				literallen;		
	int				literalalloc;	

	int				xcdepth;
/*  stuff for flex  */
	int				yy_did_buffer_switch_on_eof;
	char*			yy_last_accepting_cpos;
	int				yy_last_accepting_state;

	bool 			QueryIsRule;
    /*   from gram.y   */
	char 			saved_relname[NAMEDATALEN];  /* need this for complex attributes */   
	
        Oid*			param_type_info;
	int			pfunc_num_args;
	char**			pfunc_names;

/*  bootstrap   */
	char 			Int_yy_hold_char;
        char* 			Int_yytext;
	int 			Int_yyleng;
	int 			Int_yy_start;
	int 			Int_yy_init;
	int 			Int_yy_n_chars;
	void* 			Int_yy_current_buffer;
	char* 			Int_yy_c_buf_p;
	int 			Int_yyline;
/*  stuff for bootstrap flex  */
	int				Int_yy_did_buffer_switch_on_eof;
	char*                           Int_yy_last_accepting_cpos;
	int				Int_yy_last_accepting_state;
} ParserInfo;

PG_EXTERN ParserInfo* GetParserInfo(void);
PG_EXTERN ParserInfo* CaptureParserInfo(void);


#endif	 /* PARSERINFO_H */
