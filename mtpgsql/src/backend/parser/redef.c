
#include "env/env.h"

#include "redef.h"

#define value GetEnv()->buffer


char* yytext(void)
{
	return value;
}
