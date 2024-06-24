/*-------------------------------------------------------------------------
 *
 * dynloader.c
 *	  dynamic loader for QNX4 using the shared library mechanism
 *
 * Copyright (c) 1999, repas AEG Automation GmbH
 *
 *
 * IDENTIFICATION
 *	  $Header: /cvs/weaver/mtpgsql/src/backend/port/dynloader/qnx4.c,v 1.1.1.1 2006/08/12 00:21:15 synmscott Exp $
 *
 *	NOTES
 *
 *-------------------------------------------------------------------------
 */
/* System includes */
/*

#include <a.out.h>
#include <dl.h>
*/
#include "postgres.h"
#include "fmgr.h"
#include "utils/dynamic_loader.h"
#include "dynloader.h"

void *
pg_dlopen(char *filename)
{
	return (void *) NULL;
}

func_ptr
pg_dlsym(void *handle, char *funcname)
{
	return NULL;
}

void
pg_dlclose(void *handle)
{
}

char *
pg_dlerror()
{
	static char errmsg[] = "Failed to load shared library due to lack of shared library support.";

	return errmsg;
}
