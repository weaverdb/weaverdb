/*-------------------------------------------------------------------------
 *
 * superuser.c
 *
 *	  The superuser() function.  Determines if user has superuser privilege.
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  $Header: /cvs/weaver/mtpgsql/src/backend/utils/misc/superuser.c,v 1.1.1.1 2006/08/12 00:22:02 synmscott Exp $
 *
 * DESCRIPTION
 *	  See superuser().
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include "catalog/pg_shadow.h"
#include "utils/syscache.h"
#include "miscadmin.h"

bool
superuser(void)
{
/*--------------------------------------------------------------------------
	The Postgres user running this command has Postgres superuser
	privileges.
--------------------------------------------------------------------------*/
	HeapTuple	utup;

	utup = SearchSysCacheTuple(SHADOWNAME,
							   PointerGetDatum(GetPgUserName()),
							   0, 0, 0);
	Assert(utup != NULL);
	return ((Form_pg_shadow) GETSTRUCT(utup))->usesuper;
}
