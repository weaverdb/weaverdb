/*-------------------------------------------------------------------------
 *
 * pg_version.c
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  $Header: /cvs/weaver/mtpgsql/src/bin/pg_version/pg_version.c,v 1.1.1.1 2006/08/12 00:11:29 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */
#include <stdlib.h>
#include <stdio.h>

#include "version.h"			/* interface to SetPgVersion */



int
main(int argc, char **argv)
{
	int			retcode;		/* our eventual return code */
	char	   *reason;			/* Reason that SetPgVersion failed, NULL
								 * if it didn't. */

	if (argc < 2)
	{
		fprintf(stderr, "pg_version: missing argument\n");
		exit(1);
	}
	SetPgVersion(argv[1], &reason);
	if (reason)
	{
		fprintf(stderr,
				"pg_version is unable to create the PG_VERSION file. "
				"SetPgVersion gave this reason: %s\n",
				reason);
		retcode = 10;
	}
	else
		retcode = 0;
	return retcode;
}
