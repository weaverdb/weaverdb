/*-------------------------------------------------------------------------
 *
 * format.c
 *	  a wrapper around code that does what vsprintf does.
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  $Header: /cvs/weaver/mtpgsql/src/backend/utils/error/format.c,v 1.1.1.1 2006/08/12 00:21:58 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#define FormMaxSize		1024
#define FormMinSize		(FormMaxSize / 8)

static char FormBuf[FormMaxSize];


/* ----------------
 *		vararg_format
 * ----------------
 */
char *
vararg_format(const char *fmt,...)
{
	va_list		args;

	va_start(args, fmt);
	vsnprintf(FormBuf, FormMaxSize - 1, fmt, args);
	va_end(args);
	return FormBuf;
}
