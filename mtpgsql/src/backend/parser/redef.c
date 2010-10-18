
#include "env/env.h"

#include "redef.h"

#define value GetEnv()->value	


char* yytext(void)
{
	return value;
}
