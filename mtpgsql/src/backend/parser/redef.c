/*-------------------------------------------------------------------------
 *
 * redef.c
 *	  
 * Copyright (c) 2000-2024, Myron Scott  <myron@weaverdb.org>
 *
 *
 *
 *
 *-------------------------------------------------------------------------
 */


#include "env/env.h"

#include "redef.h"

#define value GetEnv()->buffer


char* yytext(void)
{
	return value;
}
