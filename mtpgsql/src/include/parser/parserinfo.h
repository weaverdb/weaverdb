/*-------------------------------------------------------------------------
 *
 * parserinfo.h
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
#ifndef PARSERINFO_H
#define PARSERINFO_H

#include "nodes/pg_list.h"

typedef struct ParserInfo {
	List* 			parsetree;
	char* 			parseString;
	char* 			parseCh;

	char*			literalbuf;		
	int			literallen;		
	int			literalalloc;	

	int			xcdepth;
/*  stuff for flex  */
        void*                   yyscanner;
        int                     yyline;

	bool 			QueryIsRule;
    /*   from gram.y   */
	char 			saved_relname[NAMEDATALEN];  /* need this for complex attributes */   
	
        Oid*			param_type_info;
	int			pfunc_num_args;
	char**			pfunc_names;

} ParserInfo;

PG_EXTERN ParserInfo* GetParserInfo(void);
PG_EXTERN ParserInfo* CaptureParserInfo(void);


#endif	 /* PARSERINFO_H */
