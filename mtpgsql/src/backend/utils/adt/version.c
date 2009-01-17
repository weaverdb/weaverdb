/*-------------------------------------------------------------------------
 *
 * version.c
 *	 Returns the version string
 *
 * IDENTIFICATION
 *
 * $Header: /cvs/weaver/mtpgsql/src/backend/utils/adt/version.c,v 1.1.1.1 2006/08/12 00:21:54 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */


#include "postgres.h"
#include "version.h"

text	   *version(void);

text *
version(void)
{
	int			n = strlen(PG_VERSION_STR) + VARHDRSZ;
	text	   *ret = (text *) palloc(n);

	SETVARSIZE(ret,n);
	memcpy(VARDATA(ret), PG_VERSION_STR, strlen(PG_VERSION_STR));

	return ret;
}
