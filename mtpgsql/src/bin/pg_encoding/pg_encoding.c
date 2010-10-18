/*-------------------------------------------------------------------------
 *
 * pg_encoding.c
 *
 *
 * Copyright (c) 1998, PostgreSQL development group
 *
 *
 * IDENTIFICATION
 *	  $Header: /cvs/weaver/mtpgsql/src/bin/pg_encoding/pg_encoding.c,v 1.1.1.1 2006/08/12 00:11:28 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"
#include "miscadmin.h"
#include "mb/pg_wchar.h"

static void usage(void);

int
main(int argc, char **argv)
{
	char		c;
	char	   *p;
	int			rtn;

	if (argc < 2)
	{
		usage();
		exit(1);
	}
	p = argv[1];
	while ((c = *p++))
	{
		if (c < '0' || c > '9')
		{
			rtn = pg_char_to_encoding(argv[1]);
			if (rtn >= 0)
				printf("%d\n", rtn);
			exit(0);
		}
	}
	printf("%s\n", pg_encoding_to_char(atoi(argv[1])));
	exit(0);
}

static void
usage()
{
	fprintf(stderr, "\nUsage: pg_encoding encoding_name | encoding_number\n\n");
}
