/*-------------------------------------------------------------------------
 *
 * geqo_random.h
 *	  random number generator
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 *
 *-------------------------------------------------------------------------
 */

/* contributed by:
   =*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=
   *  Martin Utesch				 * Institute of Automatic Control	   *
   =							 = University of Mining and Technology =
   *  utesch@aut.tu-freiberg.de  * Freiberg, Germany				   *
   =*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=
 */

/* -- parts of this are adapted from D. Whitley's Genitor algorithm -- */

#ifndef GEQO_RANDOM_H
#define GEQO_RANDOM_H

#include <math.h>

#include "env/env.h"

#define GEQOMASK 2147483647

#define geqo_rand() ((double)prandom()/GEQOMASK)

/* geqo_randint returns integer value
   between lower and upper inclusive */

#define geqo_randint(upper,lower) ( (int) floor( geqo_rand()*((upper-lower)+0.999999) )  +	lower )

#endif	 /* GEQO_RANDOM_H */
