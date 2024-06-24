/*-------------------------------------------------------------------------
 *
 * miscinit.c
 *	  miscellanious initialization support stuff
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  $Header: /cvs/weaver/mtpgsql/src/backend/utils/init/miscinit.c,v 1.1.1.1 2006/08/12 00:22:00 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */

#include <sys/param.h>

#include <signal.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
#include <grp.h>
#include <pwd.h>
#include <stdlib.h>

#include "postgres.h"

#include "env/env.h"
#include "catalog/catname.h"
#include "catalog/pg_shadow.h"
#include "miscadmin.h"
#include "utils/syscache.h"


#ifdef CYR_RECODE
unsigned char RecodeForwTable[128];
unsigned char RecodeBackTable[128];

#endif

ProcessingMode Mode = InitProcessing;
 char*  DatabasePath;
/*
 char*  DatabaseName;
*/

/* ----------------------------------------------------------------
 *		ignoring system indexes support stuff
 * ----------------------------------------------------------------
 */

static bool isIgnoringSystemIndexes = false;

/*
 * IsIgnoringSystemIndexes
 *		True if ignoring system indexes.
 */
bool
IsIgnoringSystemIndexes()
{
/*	printf("index ignoring:%d\n",isIgnoringSystemIndexes);  */
	return isIgnoringSystemIndexes;
}

/*
 * IgnoreSystemIndexes
 *	Set true or false whether PostgreSQL ignores system indexes.
 *
 */
void
IgnoreSystemIndexes(bool mode)
{
	isIgnoringSystemIndexes = mode;
}

/* ----------------------------------------------------------------
 *				database path / name support stuff
 * ----------------------------------------------------------------
 */


char*
GetDatabasePath()
{
	return GetEnv()->DatabasePath;
}
char*
GetDatabaseName()
{
	char* name = GetEnv()->DatabaseName;
	if ( name == NULL ) {
		name = "";
	}
	return name;
}

Oid 
GetDatabaseId()
{
    
    Oid id = GetEnv()->DatabaseId;
    	if ( id <= 0 && !IsBootstrapProcessingMode() ) {
		elog(FATAL,"Got Invalid Oid");
	}
	return id;
}

void
SetDatabaseName(const char *name)
{
	if (name)
	{
		GetEnv()->DatabaseName = pstrdup(name);
		GetEnv()->DatabasePath = ExpandDatabasePath(name);
	}
}

#ifndef MULTIBYTE
/* even if MULTIBYTE is not enabled, this function is neccesary
 * since pg_proc.h has entries for them.
 */
const char *
getdatabaseencoding()
{
	return ("SQL_ASCII");
}

const char *
pg_encoding_to_char(int encoding)
{
	return ("SQL_ASCII");
}

int
pg_char_to_encoding(const char *encoding_string)
{
	return (0);
}

#endif

#ifdef CYR_RECODE
#define MAX_TOKEN	80

/* Some standard C libraries, including GNU, have an isblank() function.
   Others, including Solaris, do not.  So we have our own.
*/
static bool
isblank(const char c)
{
	return c == ' ' || c == 9 /* tab */ ;
}

static void
next_token(FILE *fp, char *buf, const int bufsz)
{
/*--------------------------------------------------------------------------
  Grab one token out of fp.  Tokens are strings of non-blank
  characters bounded by blank characters, beginning of line, and end
  of line.	Blank means space or tab.  Return the token as *buf.
  Leave file positioned to character immediately after the token or
  EOF, whichever comes first.  If no more tokens on line, return null
  string as *buf and position file to beginning of next line or EOF,
  whichever comes first.
--------------------------------------------------------------------------*/
	int			c;
	char	   *eb = buf + (bufsz - 1);

	/* Move over inital token-delimiting blanks */
	while (isblank(c = getc(fp)));

	if (c != '\n')
	{

		/*
		 * build a token in buf of next characters up to EOF, eol, or
		 * blank.
		 */
		while (c != EOF && c != '\n' && !isblank(c))
		{
			if (buf < eb)
				*buf++ = c;
			c = getc(fp);

			/*
			 * Put back the char right after the token (putting back EOF
			 * is ok)
			 */
		}
		ungetc(c, fp);
	}
	*buf = '\0';
}

static void
read_through_eol(FILE *file)
{
	int			c;

	do
		c = getc(file);
	while (c != '\n' && c != EOF);
}

void
SetCharSet()
{
	FILE	   *file;
	char	   *p,
				c,
				eof = false;
	char	   *map_file;
	char		buf[MAX_TOKEN];
	int			i;
	unsigned char FromChar,
				ToChar;

	for (i = 0; i < 128; i++)
	{
		RecodeForwTable[i] = i + 128;
		RecodeBackTable[i] = i + 128;
	}

	p = getenv("PG_RECODETABLE");
	if (p && *p != '\0')
	{
		map_file = (char *) os_malloc((strlen(DataDir) +
									strlen(p) + 2) * sizeof(char));
		if ( map_file == NULL ) {
                        GetEnv()->errorcode = 747;
			elog(ERROR, "Memory exhausted");                    
                }
                sprintf(map_file, "%s/%s", DataDir, p);
#ifndef __CYGWIN32__
		file = AllocateFile(map_file, "r");
#else
		file = AllocateFile(map_file, "rb");
#endif
		if (file == NULL)
			return;
		eof = false;
		while (!eof)
		{
			c = getc(file);
			ungetc(c, file);
			if (c == EOF)
				eof = true;
			else
			{
				if (c == '#')
					read_through_eol(file);
				else
				{
					/* Read the FromChar */
					next_token(file, buf, sizeof(buf));
					if (buf[0] != '\0')
					{
						FromChar = strtoul(buf, 0, 0);
						/* Read the ToChar */
						next_token(file, buf, sizeof(buf));
						if (buf[0] != '\0')
						{
							ToChar = strtoul(buf, 0, 0);
							RecodeForwTable[FromChar - 128] = ToChar;
							RecodeBackTable[ToChar - 128] = FromChar;
						}
						read_through_eol(file);
					}
				}
			}
		}
		FreeFile(file);
		free(map_file);
	}
}

char *
convertstr(unsigned char *buff, int len, int dest)
{
	int			i;
	char	   *ch = buff;

	for (i = 0; i < len; i++, buff++)
	{
		if (*buff > 127)
			if (dest)
				*buff = RecodeForwTable[*buff - 128];
			else
				*buff = RecodeBackTable[*buff - 128];
	}
	return ch;
}

#endif

/* ----------------
 *		GetPgUserName and SetPgUserName
 *
 *		SetPgUserName must be called before InitPostgres, since the setuid()
 *		is done there.
 *
 *		Replace GetPgUserName() with a lower-case version
 *		to allow use in new case-insensitive SQL (referenced
 *		in pg_proc.h). Define GetPgUserName() as a macro - tgl 97/04/26
 * ----------------
 */
char *
getpgusername()
{
/*  define sets username in env  */
	return GetEnv()->UserName;
}

void
SetPgUserName(char* name)
{
/*  define sets username in env  */
	GetEnv()->UserName = pstrdup(name);
}

/* ----------------------------------------------------------------
 *		GetUserId and SetUserId
 * ----------------------------------------------------------------
 */
 /*
static Oid	UserId = InvalidOid;
*/
int
GetUserId()
{
	return GetEnv()->UserId;
}

void
SetUserId()
{
	HeapTuple	userTup;
	char	   *userName;

	/*
	 * Don't do scans if we're bootstrapping, none of the system catalogs
	 * exist yet, and they should be owned by postgres anyway.
	 */
	if (IsBootstrapProcessingMode())
	{
		GetEnv()->UserId = geteuid();
		return;
	}

	userName = GetPgUserName();
        if ( strlen(userName) > 0 ) {
            userTup = SearchSysCacheTuple(SHADOWNAME,
                                                                      PointerGetDatum(userName),
                                                                      0, 0, 0);
            if (!HeapTupleIsValid(userTup))
                    elog(FATAL, "SetUserId: user '%s' is not in '%s'",
                             userName,
                             ShadowRelationName);
            GetEnv()->UserId =  ((Form_pg_shadow) GETSTRUCT(userTup))->usesysid;
        } else {
            GetEnv()->UserId = 0;
        }
}

/*-------------------------------------------------------------------------
 *
 * posmaster pid file stuffs. $DATADIR/postmaster.pid is created when:
 *
 *	(1) postmaster starts. In this case pid > 0.
 *	(2) postgres starts in standalone mode. In this case
 *		pid < 0
 *
 * to gain an interlock.
 *
 *	SetPidFname(datadir)
 *		Remember the the pid file name. This is neccesary
 *		UnlinkPidFile() is called from proc_exit().
 *
 *	GetPidFname(datadir)
 *		Get the pid file name. SetPidFname() should be called
 *		before GetPidFname() gets called.
 *
 *	UnlinkPidFile()
 *		This is called from proc_exit() and unlink the pid file.
 *
 *	SetPidFile(pid_t pid)
 *		Create the pid file. On failure, it checks if the process
 *		actually exists or not. SetPidFname() should be called
 *		in prior to calling SetPidFile().
 *
 *-------------------------------------------------------------------------
 */

/*
 * Path to pid file. proc_exit() remember it to unlink the file.
 */
static char PidFile[MAXPGPATH];

/*
 * Remove the pid file. This function is called from proc_exit.
 */
void
UnlinkPidFile(void)
{
	unlink(PidFile);
}

/*
 * Set path to the pid file
 */
void
SetPidFname(char *datadir)
{
	snprintf(PidFile, sizeof(PidFile), "%s/%s", datadir, PIDFNAME);
}

/*
 * Get path to the pid file
 */
char *
GetPidFname(void)
{
	return (PidFile);
}

/*
 * Create the pid file
 */
int
SetPidFile(pid_t pid)
{
	int			fd;
	char	   *pidfile;
	char		pidstr[32];
	int			len;
	pid_t		post_pid;
	int			is_postgres = 0;

	/*
	 * Creating pid file
	 */
	pidfile = GetPidFname();
	fd = open(pidfile, O_RDWR | O_CREAT | O_EXCL, 0600);
	if (fd < 0)
	{

		/*
		 * Couldn't create the pid file. Probably it already exists. Read
		 * the file to see if the process actually exists
		 */
		fd = open(pidfile, O_RDONLY, 0600);
		if (fd < 0)
		{
			fprintf(stderr, "Can't open pid file: %s\n", pidfile);
			fprintf(stderr, "Please check the permission and try again.\n");
			return (-1);
		}
		if ((len = read(fd, pidstr, sizeof(pidstr) - 1)) < 0)
		{
			fprintf(stderr, "Can't read pid file: %s\n", pidfile);
			fprintf(stderr, "Please check the permission and try again.\n");
			close(fd);
			return (-1);
		}
		close(fd);

		/*
		 * Check to see if the process actually exists
		 */
		pidstr[len] = '\0';
		post_pid = (pid_t) atoi(pidstr);

		/* if pid < 0, the pid is for postgres, not postmatser */
		if (post_pid < 0)
		{
			is_postgres++;
			post_pid = -post_pid;
		}

		if (post_pid == 0 || (post_pid > 0 && kill(post_pid, 0) < 0))
		{

			/*
			 * No, the process did not exist. Unlink the file and try to
			 * create it
			 */
			if (unlink(pidfile) < 0)
			{
				fprintf(stderr, "Can't remove pid file: %s\n", pidfile);
				fprintf(stderr, "The file seems accidently left, but I couldn't remove it.\n");
				fprintf(stderr, "Please remove the file by hand and try again.\n");
				return (-1);
			}
			fd = open(pidfile, O_RDWR | O_CREAT | O_EXCL, 0600);
			if (fd < 0)
			{
				fprintf(stderr, "Can't create pid file: %s\n", pidfile);
				fprintf(stderr, "Please check the permission and try again.\n");
				return (-1);
			}
		}
		else
		{

			/*
			 * Another postmaster is running
			 */
			fprintf(stderr, "Can't create pid file: %s\n", pidfile);
			if (is_postgres)
				fprintf(stderr, "Is another postgres (pid: %d) running?\n", post_pid);
			else
				fprintf(stderr, "Is another postmaster (pid: %s) running?\n", pidstr);
			return (-1);
		}
	}

	sprintf(pidstr, "%d", pid);
	if (write(fd, pidstr, strlen(pidstr)) != strlen(pidstr))
	{
		fprintf(stderr, "Write to pid file failed\n");
		fprintf(stderr, "Please check the permission and try again.\n");
		close(fd);
		unlink(pidfile);
		return (-1);
	}
	close(fd);

	return (0);
}
