/*-------------------------------------------------------------------------
 *
 * rint.c
 *	  rint() implementation
 *
 * Copyright (c) 1999, repas AEG Automation GmbH
 *
 *
 * IDENTIFICATION
 *	  $Header: /cvs/weaver/mtpgsql/src/backend/port/qnx4/rint.c,v 1.1.1.1 2006/08/12 00:21:15 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */

#include <math.h>
#include "os.h"

double
rint(double x)
{
	double		f,
				n = 0.;

	f = modf(x, &n);

	if (x > 0.)
	{
		if (f > .5)
			n += 1.;
	}
	else if (x < 0.)
	{
		if (f < -.5)
			n -= 1.;
	}
	return n;
}
