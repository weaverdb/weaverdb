/* $Id: random.c,v 1.1.1.1 2006/08/12 00:21:13 synmscott Exp $ */


#include <math.h>
#include <errno.h>

#include "config.h"

long
random()
{
	return lrand48();
}
