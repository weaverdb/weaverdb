/*
 * pg_id.c
 *
 * A crippled id utility for use in various shell scripts in use by PostgreSQL
 * (in particular initdb)
 *
 * Portions Copyright (c) 2000-2024, Myron Scott  <myron@weaverdb.org>
 * Copyright (C) 2000 by PostgreSQL Global Development Group
 *
 *
 */
#include <postgres.h>

#ifdef HAVE_GETOPT_H
#include <getopt.h>
#endif
#include <pwd.h>


#include <unistd.h>


int
main(int argc, char *argv[])
{
	int			c;
	int			nameflag = 0,
				realflag = 0,
				userflag = 0;
	const char *username = NULL;

	struct passwd *pw;

	while ((c = getopt(argc, argv, "nru")) != -1)
	{
		switch (c)
		{
			case 'n':
				nameflag = 1;
				break;
			case 'r':
				realflag = 1;
				break;
			case 'u':
				userflag = 1;
				break;
			default:
				fprintf(stderr, "Usage: %s [-n] [-r] [-u] [username]\n", argv[0]);
				exit(1);
		}
	}

	if (argc - optind >= 1)
		username = argv[optind];

	if (nameflag && !userflag)
	{
		fprintf(stderr, "%s: -n must be used together with -u\n", argv[0]);
		exit(1);
	}
	if (username && realflag)
	{
		fprintf(stderr, "%s: -r cannot be used when a user name is given\n", argv[0]);
		exit(1);
	}


	if (username)
	{
		pw = getpwnam(username);
		if (!pw)
		{
			fprintf(stderr, "%s: %s: no such user\n", argv[0], username);
			exit(1);
		}
	}
	else if (realflag)
		pw = getpwuid(getuid());
	else
		pw = getpwuid(geteuid());

	if (!pw)
	{
		perror(argv[0]);
		exit(1);
	}

	if (!userflag)
		printf("uid=%d(%s)\n", (int) pw->pw_uid, pw->pw_name);
	else if (nameflag)
		puts(pw->pw_name);
	else
		printf("%d\n", (int) pw->pw_uid);

	return 0;
}
