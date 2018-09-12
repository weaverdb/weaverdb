/*-------------------------------------------------------------------------
 *
 * palloc.c
 *	  POSTGRES memory allocator code.
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  $Header: /cvs/weaver/mtpgsql/src/backend/utils/mmgr/palloc.c,v 1.1.1.1 2006/08/12 00:22:03 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */


#include "postgres.h"

#include "nodes/memnodes.h"




/* ----------------------------------------------------------------
 *		User library functions
 * ----------------------------------------------------------------
 */

/* ----------
 * palloc(), pfree() and repalloc() are now macros in palloc.h
 * ----------
 */

char *
pstrdup(const char *string)
{
	char	   *nstr;
	int			len;

	nstr = palloc(len = strlen(string) + 1);
	memcpy(nstr, string, len);

	return nstr;
}
