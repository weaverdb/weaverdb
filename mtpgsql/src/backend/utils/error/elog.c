/*-------------------------------------------------------------------------
 *
 * elog.c
 *	  error logger
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  $Header: /cvs/weaver/mtpgsql/src/backend/utils/error/elog.c,v 1.1.1.1 2006/08/12 00:21:58 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */


#include <time.h>
#include <fcntl.h>
#ifndef O_RDONLY
#include <sys/file.h>
#endif	 /* O_RDONLY */

#include <errno.h>
#include <unistd.h>
#include <signal.h>
#ifdef USE_SYSLOG
#include <syslog.h>
#endif
#include <stdarg.h>

#include "postgres.h"
#include "env/env.h"

#include "libpq/libpq.h"
#include "libpq/pqformat.h"
#include "miscadmin.h"
#include "storage/ipc.h"
#include "storage/multithread.h"
#include "tcop/tcopprot.h"
#include "utils/trace.h"
#include "commands/copy.h"
#include "env/dbwriter.h"
#include "env/dolhelper.h"


#ifdef USE_SYSLOG
/*
 * Global option to control the use of syslog(3) for logging:
 *
 *		0	stdout/stderr only
 *		1	stdout/stderr + syslog
 *		2	syslog only
 */
#define UseSyslog pg_options[OPT_SYSLOG]
#define PG_LOG_FACILITY LOG_LOCAL0
#else
#define UseSyslog 0
#endif

static int	Debugfile = -1;

static int	ElogDebugIndentLevel = 0;
static bool     IgnoreDebugLevel = false;
#ifdef HAVE_ALLOCINFO
static bool     DebugMemoryFlag = false;
#endif
/*--------------------
 * elog
 *		Primary error logging function.
 *
 * 'lev': error level; indicates recovery action to take, if any.
 * 'fmt': a printf-style string.
 * Additional arguments, if any, are formatted per %-escapes in 'fmt'.
 *
 * In addition to the usual %-escapes recognized by printf, "%m" in
 * fmt is replaced by the error message for the current value of errno.
 *
 * Note: no newline is needed at the end of the fmt string, since
 * elog will provide one for the output methods that need it.
 *
 * If 'lev' is ERROR or worse, control does not return to the caller.
 * See elog.h for the error level definitions.
 *--------------------
 */
int my_system(const char* cmd) {
	FILE *p;
        char msg[512];
	
	if ((p = popen(cmd, "r")) == NULL ) {
            perror("SYSTEM FAILURE");
		return (-1);
        }
        
        while (fgets(msg, 512, p) != NULL)
             elog(DEBUG,"%s", msg);
                
        return (pclose(p));
}

void
coded_elog(int lev, int code, const char *fmt,...)
{
	va_list		ap;
	char          msg_buf[256];

	GetEnv()->errorcode = code;
        va_start(ap, fmt);
        vsnprintf(msg_buf, 255, fmt, ap);
        va_end(ap);
	elog(lev, "%s", msg_buf);
}

void
elog(int lev, const char *fmt,...)
{
	va_list		ap;

	/*
	 * The expanded format and final output message are dynamically
	 * allocated if necessary, but not if they fit in the "reasonable
	 * size" buffers shown here.  In extremis, we'd rather depend on
	 * having a few hundred bytes of stack space than on malloc() still
	 * working (since memory-clobber errors often take out malloc first).
	 * Don't make these buffers unreasonably large though, on pain of
	 * having to chase a bug with no error message.
	 */
	char		fmt_fixedbuf[128];
	char		msg_fixedbuf[256];
	char	   *fmt_buf = fmt_fixedbuf;
	char	   *msg_buf = msg_fixedbuf;
        char	   *notimestamp, *noprefix;

	/* this buffer is only used if errno has a bogus value: */
	char		errorstr_buf[32];
	const char *errorstr;
	const char *prefix;
	const char *cp;
	char	   *bp;
	int			indent = 0;
	int			space_needed;
        bool                    expand = true;

#ifdef USE_SYSLOG
	int			log_level;
#endif

	if (lev <= DEBUG && Debugfile < 0)
		return;					/* ignore debug msgs if noplace to send */

        if (lev <= DEBUG && IgnoreDebugLevel) 
                return;

	if (lev == ERROR || lev == FATAL)
	{
		GetEnv()->InError = true;
		/* this is probably redundant... */
		if (IsInitProcessingMode())
			lev = FATAL;
	}

	/* choose message prefix and indent level */
	switch (lev)
	{
		case NOIND:
			indent = ElogDebugIndentLevel - 1;
			if (indent < 0)
				indent = 0;
			if (indent > 30)
				indent = indent % 30;
			prefix = "DEBUG:  ";
			break;
		case DEBUG:
                        if (Debugfile != fileno(stderr)) {
                            prefix = "";
                        } else {
                            indent = ElogDebugIndentLevel;
                            if (indent < 0)
                                    indent = 0;
                            if (indent > 30)
                                    indent = indent % 30;
                            prefix = "DEBUG:  ";
                        }
			break;
		case NOTICE:
			prefix = "NOTICE:  ";
			break;
		case ERROR:
			prefix = "ERROR:  ";
			break;
		default:
			/* temporarily use msg buf for prefix */
                        expand = false;
			sprintf(msg_fixedbuf, "FATAL %d:  ", lev);
			prefix = msg_fixedbuf;
			break;
	}

	/* get errno string for %m */
	sprintf(errorstr_buf,"error %d",100);
	errorstr = errorstr_buf;
	/*
	 * Set up the expanded format, consisting of the prefix string plus
	 * input format, with any %m replaced by strerror() string (since
	 * vsnprintf won't know what to do with %m).  To keep space
	 * calculation simple, we only allow one %m.
	 */
	space_needed = indent + (GetEnv()->lineno ? 24 : 0)
		+ strlen(fmt) + strlen(errorstr) + 1;
	if (expand && space_needed > (int) sizeof(fmt_fixedbuf))
	{
		fmt_buf = (char *) palloc(space_needed);
		if (fmt_buf == NULL)
		{
			/* We're up against it, convert to fatal out-of-memory error */
			fmt_buf = fmt_fixedbuf;
			lev = REALLYFATAL;
			fmt = "elog: out of memory";		/* this must fit in
												 * fmt_fixedbuf! */
		}
	}
#ifdef ELOG_TIMESTAMPS
	notimestamp = stpcpy(msg_buf, tprintf_timestamp());
        notimestamp = stpcpy(notimestamp, "  ");
	noprefix = stpcpy(notimestamp, prefix);
#else
        notimestamp = msg_buf;
	noprefix = strpcpy(msg_buf, prefix);
#endif
	bp = fmt_buf;
	while (indent-- > 0)
		*bp++ = ' ';

	/* If error was in CopyFrom() print the offending line number -- dz */
	if (GetEnv()->lineno)
	{
		sprintf(bp, "copy: line %d, ", GetEnv()->lineno);
		bp += strlen(bp);
		if (lev == ERROR || lev >= FATAL)
			GetEnv()->lineno = 0;
	}

	for (cp = fmt; *cp; cp++)
	{
		if (cp[0] == '%' && cp[1] != '\0')
		{
			if (cp[1] == 'm')
			{

				/*
				 * XXX If there are any %'s in errorstr then vsnprintf
				 * will do the Wrong Thing; do we need to cope? Seems
				 * unlikely that % would appear in system errors.
				 */
				strcpy(bp, errorstr);

				/*
				 * copy the rest of fmt literally, since we can't afford
				 * to insert another %m.
				 */
				strcat(bp, cp + 2);
				bp += strlen(bp);
				break;
			}
			else
			{
				/* copy % and next char --- this avoids trouble with %%m */
				*bp++ = *cp++;
				*bp++ = *cp;
			}
		}
		else
			*bp++ = *cp;
	}
	*bp = '\0';

	/*
	 * Now generate the actual output text using vsnprintf(). Be sure to
	 * leave space for \n added later as well as trailing null.
	 */
        int preamble = (noprefix - msg_buf);
	space_needed = sizeof(msg_fixedbuf);
	for (;;)
	{
		int			nprinted;

		va_start(ap, fmt);
		nprinted = vsnprintf(noprefix, space_needed - preamble - 1, fmt_buf, ap);
		va_end(ap);

		/*
		 * Note: some versions of vsnprintf return the number of chars
		 * actually stored, but at least one returns -1 on failure. Be
		 * conservative about believing whether the print worked.
		 */
		if (!expand || (nprinted >= 0 && nprinted < space_needed - 1) )
			break;

		space_needed *= 2;
		msg_buf = (char *) repalloc(msg_buf, space_needed);
		if (msg_buf == NULL)
		{
			/* We're up against it, convert to fatal out-of-memory error */
			msg_buf = msg_fixedbuf;
			lev = REALLYFATAL;
#ifdef ELOG_TIMESTAMPS
			strcpy(msg_buf, tprintf_timestamp());
			strcat(msg_buf, "FATAL:  elog: out of memory");
#else
			strcpy(msg_buf, "FATAL:  elog: out of memory");
#endif
			break;
		}
                notimestamp = msg_buf + preamble;
	}

	/*
	 * Message prepared; send it where it should go
	 */

#ifdef USE_SYSLOG
	switch (lev)
	{
		case NOIND:
			log_level = LOG_DEBUG;
			break;
		case DEBUG:
			log_level = LOG_DEBUG;
			break;
		case NOTICE:
			log_level = LOG_NOTICE;
			break;
		case ERROR:
			log_level = LOG_WARNING;
			break;
		case FATAL:
		default:
			log_level = LOG_ERR;
			break;
	}
	write_syslog(log_level, notimestamp);
#endif

	if (lev > DEBUG && WhereToSendOutput() == Remote)
	{
		/* Send IPC message to the front-end program */
		char		msgtype;
                

                strcat(msg_buf, "\n");

		if (lev == NOTICE)
			msgtype = 'N';
		else
		{

			/*
			 * Abort any COPY OUT in progress when an error is detected.
			 * This hack is necessary because of poor design of copy
			 * protocol.
			 */
			pq_endcopyout(true);
			msgtype = 'E';
		}
		/* exclude the timestamp from msg sent to frontend */
/*  Let the upper layers do this if the type is remote  */
                pq_puttextmessage(msgtype, notimestamp);  
                pq_putbytes("\n", 1);  

		/*
		 * This flush is normally not necessary, since postgres.c will
		 * flush out waiting data when control returns to the main loop.
		 * But it seems best to leave it here, so that the client has some
		 * clue what happened if the backend dies before getting back to
		 * the main loop ... error/notice messages should not be a
		 * performance-critical path anyway, so an extra flush won't hurt
		 * much ...
		 */
		pq_flush();
	} else if (lev > DEBUG && WhereToSendOutput() == Local) {
                pq_putbytes(notimestamp, strlen(notimestamp));
                pq_putbytes("\n", 1);
                pq_flush();
        } else  if (Debugfile >= 0 && UseSyslog <= 1) {
		write(Debugfile, msg_buf, strlen(msg_buf));
		write(Debugfile, "\n", 1);
        }

	/*
	 * Perform error recovery action as specified by lev.
	 */
	if (lev == ERROR)
	{
		/*
		 * If we have not yet entered the main backend loop (ie, we are in
		 * the postmaster or in backend startup), then go directly to
		 * proc_exit.  The same is true if anyone tries to report an error
		 * after proc_exit has begun to run.  (It's proc_exit's
		 * responsibility to see that this doesn't turn into infinite
		 * recursion!)	But in the latter case, we exit with nonzero exit
		 * code to indicate that something's pretty wrong.
		 */
		if (IsMultiuser() && GetEnv() == NULL)
		{
			fflush(stdout);
			fflush(stderr);
			/* XXX shouldn't proc_exit be doing the above?? */
			proc_exit(lev);
		}

		if ( IsMultiuser() && !IsDBWriter() ) {
                    CancelDolHelpers();
                    Env* env = (Env*)GetEnv();
                    strncpy(env->errortext,noprefix,255);
                    strncpy(env->state,prefix,39);
                    if ( GetEnv()->errorcode != 0 ) {
                            longjmp(GetEnv()->errorContext, GetEnv()->errorcode);   
                    } else {
                            longjmp(GetEnv()->errorContext, 100);
                    }
		} else {
			siglongjmp(Warn_restart, 1);
		}
		/*  if arrived here, jump failed */
		fflush(stdout);
		fflush(stderr);
		proc_exit(lev);
	}

	if (lev >= FATAL)
	{
            if ( IsMultiuser() ) {
                printf("SYSTEM HALT: from thread %ld\n",(long)pthread_self());
                printf("%s", msg_buf);
                fprintf(stderr, "%s\n", msg_buf);
                #ifdef MACOSX
                kill(getpid(),SIGABRT);
                #else
                        /*
                sigsend(P_PID,P_MYID,SIGABRT);
                         */
                abort();
                #endif
            } else {
                /*
                 * Serious crash time. Postmaster will observe nonzero process
                 * exit status and kill the other backends too.
                 *
                 * XXX: what if we are *in* the postmaster?  proc_exit() won't kill
                 * our children...
                 */
                fflush(stdout);
                fflush(stderr);
                proc_exit(lev);
            }
	}

	/* We reach here if lev <= NOTICE.	OK to return to caller. */
}

#ifndef PG_STANDALONE

int
InitializeElog(const char* logfile, bool debug, bool redirecterr) {
	int			fd;

	Debugfile = -1;
	ElogDebugIndentLevel = 0;
        IgnoreDebugLevel = !debug;

	if (logfile != NULL)
	{
		if ((fd = open(logfile, O_CREAT | O_APPEND | O_WRONLY,
					   0666)) < 0) {
                    elog(FATAL, "InitializeElog: open of %s: %m",
				 logfile);
                } else {
                    if (debug) {
                        fprintf(stderr, "logging output to %s\n", logfile);
                    }
                    if (redirecterr) {
        		close(fd);
                        if (!freopen(logfile, "a", stderr)) {
                            elog(FATAL, "InitializeElog: %s reopen as stderr: %m",
				 logfile);
                        } else {
                            fd = fileno(stderr);
                        }
                    }
                    Debugfile = fd;
                    return Debugfile;
                }
	}

	/*
	 * If no filename was specified, send debugging output to stderr. If
	 * stderr has been hosed, try to open a file.
	 */
	fd = fileno(stderr);
	if (fcntl(fd, F_GETFD, 0) < 0)
	{
                char    lastditchlogger[MAXPGPATH];
		snprintf(lastditchlogger, MAXPGPATH, "%s%cpg.errors.%d",
				 DataDir, SEP_CHAR, (int) MyProcPid);
		fd = open(lastditchlogger, O_CREAT | O_APPEND | O_WRONLY, 0666);
	}
	if (fd < 0)
		elog(FATAL, "InitializeElog: could not open debugging file");

	Debugfile = fd;
	return Debugfile;
}
#ifdef HAVE_ALLOCINFO
void
DebugMemory(const char* type, const char* name, void* _cxt, Size _chunk, const char* file, int line, const char* func) {
    Env* env = GetEnv();
    if ((DebugMemoryFlag || (env != NULL && env->print_memory)) && Debugfile >= 0) {
        dprintf(Debugfile, "%s: %s: %p, %ld in %s at %s:%d\n", type, name, _cxt, _chunk, func, file, line);
    }
}
#endif /* HAVE_ALLOCINFO */

#endif /* PG_STANDALONE */
