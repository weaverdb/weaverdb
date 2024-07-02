/*-------------------------------------------------------------------------
 *
 * parserinfo.c
 *	  
 *
 *
 *
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include "env/env.h"
#include "parser/parserinfo.h"

static SectionId parser_id = SECTIONID("PARS");

static ParserInfo* InitializeParser(void);

#ifdef TLS
TLS  ParserInfo* parser_info = NULL;
#else
#define parser_info GetEnv()->parser_info
#endif

ParserInfo*
GetParserInfo(void)
{
    ParserInfo*  info = parser_info;

    if ( info == NULL ) {
        info = InitializeParser();
    }
    
    return info;
}

ParserInfo*
InitializeParser(void) {
        ParserInfo* info = AllocateEnvSpace(parser_id,sizeof(ParserInfo));

        parser_info = info;

        return info;
}

ParserInfo* CaptureParserInfo(void) {
/*  clear the cache pointer  */
    return GetParserInfo();
}
