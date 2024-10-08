/*
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 *
 */

#include <unistd.h>

#include "postgres.h"
#ifdef HAVE_CRYPT_H
#include "crypt.h"
#endif
#include "env/env.h"

#include "libpq/libpq.h"
#include "libpq/password.h"
#include "miscadmin.h"

int
verify_password(char *auth_arg, char *user, char *password)
{
	char	   *pw_file_fullname;
	FILE	   *pw_file;

	pw_file_fullname = (char *) palloc(strlen(DataDir) + strlen(auth_arg) + 2);
	strcpy(pw_file_fullname, DataDir);
	strcat(pw_file_fullname, "/");
	strcat(pw_file_fullname, auth_arg);

#ifndef __CYGWIN32__
	pw_file = AllocateFile(pw_file_fullname, "r");
#else
	pw_file = AllocateFile(pw_file_fullname, "rb");
#endif
	if (!pw_file)
	{
		snprintf(PQerrormsg, PQERRORMSG_LENGTH,
				 "verify_password: couldn't open password file '%s'\n",
				 pw_file_fullname);
		fputs(PQerrormsg, stderr);
		pqdebug("%s", PQerrormsg);

		pfree(pw_file_fullname);

		return STATUS_ERROR;
	}

	pfree(pw_file_fullname);

	while (!feof(pw_file))
	{
		char		pw_file_line[255],
				   *p,
				   *test_user,
				   *test_pw;

		fgets(pw_file_line, sizeof(pw_file_line), pw_file);
		p = pw_file_line;

		test_user = strtok_r(p, ":",&test_pw);
		if (!test_user || !test_pw ||
			test_user[0] == '\0' || test_pw[0] == '\0')
			continue;

		/* kill the newline */
		if (test_pw[strlen(test_pw) - 1] == '\n')
			test_pw[strlen(test_pw) - 1] = '\0';

		if (strcmp(user, test_user) == 0)
		{
			/* we're outta here one way or the other, so close file */
			FreeFile(pw_file);
#ifdef NOCRYPT
                        if (strcmp(password, test_pw) == 0) {
                            return STATUS_OK;
                        }
#else
			if (strcmp(crypt(password, test_pw), test_pw) == 0)
			{
				/* it matched. */
				return STATUS_OK;
			}
#endif
			snprintf(PQerrormsg, PQERRORMSG_LENGTH,
					 "verify_password: password mismatch for '%s'.\n",
					 user);
			fputs(PQerrormsg, stderr);
			pqdebug("%s", PQerrormsg);

			return STATUS_ERROR;
		}
	}

	FreeFile(pw_file);

	snprintf(PQerrormsg, PQERRORMSG_LENGTH,
			 "verify_password: user '%s' not found in password file.\n",
			 user);
	fputs(PQerrormsg, stderr);
	pqdebug("%s", PQerrormsg);

	return STATUS_ERROR;
}
