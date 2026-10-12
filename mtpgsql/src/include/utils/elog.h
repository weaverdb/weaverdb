/*-------------------------------------------------------------------------
 *
 * elog.h
 *	  POSTGRES error logging definitions.
 *
 *
 * Portions Copyright (c) 2000-2024, Myron Scott  <myron@weaverdb.org>
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 *
 *-------------------------------------------------------------------------
 */
#ifndef ELOG_H
#define ELOG_H

/*
 * NOTICE goes to the connected client (or local output). DEBUG and NOIND
 * go only to the server log and are dropped unless debug is on.
 *
 * WARN must stay numerically <= DEBUG. elog() uses `lev > DEBUG` to decide
 * what is sent to the frontend, so WARN then shares DEBUG's destination
 * (the InitializeElog file, or stderr) without being dropped when debug is off.
 */
#define NOTICE	0				/* client info when a frontend is connected */
#define ERROR	(-1)			/* user error - return to known state */
#define FATAL	1				/* fatal error - abort process (embedded: panic-shutdown instance) */
#define REALLYFATAL 2			/* take down the other backends with me */
#define STOP	REALLYFATAL
#define DEBUG	(-2)			/* debug message; server log only, if debug is on */
#define LOG		DEBUG
#define NOIND	(-3)			/* debug message, don't indent as far */
#define WARN	(-4)			/* server-log warning; same place as DEBUG, always emitted */

#ifdef __cplusplus
extern "C" {
#endif
PG_EXTERN void coded_elog(int lev, int code, const char *fmt,...);
PG_EXTERN int my_system(const char* cmd);
#ifndef __GNUC__
PG_EXTERN void elog(int lev, const char *fmt,...);
#else
/* This extension allows gcc to check the format string for consistency with
   the supplied arguments. */
PG_EXTERN void elog(int lev, const char *fmt,...) __attribute__((format(printf, 2, 3)));
#endif

#ifndef PG_STANDALONE

PG_EXTERN int	InitializeElog(const char* logfile, bool debug, bool redirecterr);
#ifdef HAVE_ALLOCINFO
PG_EXTERN void	DebugMemory(const char* type, const char* name, void* _cxt, Size _chunk, const char* file, int line, const char* func);
#endif
#ifdef __cplusplus
}
#endif

#endif

#endif	 /* ELOG_H */
