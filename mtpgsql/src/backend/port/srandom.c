/* $Id: srandom.c,v 1.1.1.1 2006/08/12 00:21:14 synmscott Exp $ */


#include <math.h>
#include <errno.h>

#include "config.h"

void
srandom(unsigned int seed)
{
	srand48((long int) seed);
}
