/*-------------------------------------------------------------------------
 *
 * excabort.c
 *	  Default exception abort code.
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  $Header: /cvs/weaver/mtpgsql/src/backend/utils/error/excabort.c,v 1.1.1.1 2006/08/12 00:21:58 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */
#include <stdlib.h>
#include <unistd.h>

#include "postgres.h"

#include "utils/exc.h"

void
ExcAbort(const Exception *excP,
		 ExcDetail detail,
		 ExcData data,
		 ExcMessage message)
{
	/* dump core */
	abort();
        while ( TRUE ) sleep(1000);
}
