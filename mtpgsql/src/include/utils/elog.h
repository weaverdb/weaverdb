/*-------------------------------------------------------------------------
 *
 * elog.h
 *	  POSTGRES error logging definitions.
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * $Id: elog.h,v 1.1.1.1 2006/08/12 00:22:26 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */
#ifndef ELOG_H
#define ELOG_H

#define NOTICE	0				/* random info - no special action */
#define ERROR	(-1)			/* user error - return to known state */
#define FATAL	1				/* fatal error - abort process */
#define REALLYFATAL 2			/* take down the other backends with me */
#define STOP	REALLYFATAL
#define DEBUG	(-2)			/* debug message */
#define LOG		DEBUG
#define NOIND	(-3)			/* debug message, don't indent as far */

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

PG_EXTERN int	DebugFileOpen(bool redirect);
#ifdef HAVE_ALLOCINFO
PG_EXTERN void	DebugMemory(const char* type, const char* name, void* _cxt, Size _chunk, const char* file, int line, const char* func);
#endif
#ifdef __cplusplus
}
#endif

#endif

#endif	 /* ELOG_H */
