/*-------------------------------------------------------------------------
 *
 * tstrint.c
 *	  rint() test
 *
 * Copyright (c) 1999, repas AEG Automation GmbH
 *
 *
 * IDENTIFICATION
 *	  $Header: /cvs/weaver/mtpgsql/src/backend/port/qnx4/tstrint.c,v 1.1.1.1 2006/08/12 00:21:15 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include "os.h"


int
main(int argc, char **argv)
{
	double		x;

	if (argc != 2)
		exit(1);

	x = strtod(argv[1], NULL);
	printf("rint( %f ) = %f\n", x, rint(x));

	return 0;
}
