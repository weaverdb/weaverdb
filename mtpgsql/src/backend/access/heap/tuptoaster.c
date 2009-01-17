/*-------------------------------------------------------------------------
 *
 * tuptoaster.c
 *	  Support routines for external and compressed storage of
 *	  variable size attributes.
 *
 * Copyright (c) 2000, PostgreSQL Global Development Group
 *
 *
 * IDENTIFICATION
 *	  $Header: /cvs/weaver/mtpgsql/src/backend/access/heap/tuptoaster.c,v 1.1.1.1 2006/08/12 00:20:00 synmscott Exp $
 *
 *
 * INTERFACE ROUTINES
 *		heap_tuple_toast_attrs -
 *			Try to make a given tuple fit into one page by compressing
 *			or moving off attributes
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"
#include "env/env.h"
#include "access/heapam.h"
#include "access/tuptoaster.h"
#include "catalog/catalog.h"
#include "utils/rel.h"


#ifdef TUPLE_TOASTER_ACTIVE

void
heap_tuple_toast_attrs(Relation rel, HeapTuple newtup, HeapTuple oldtup)
{
	return;
}


varattrib  *
heap_tuple_untoast_attr(varattrib * attr)
{
	elog(ERROR, "heap_tuple_untoast_attr() called");
}


#endif	 /* TUPLE_TOASTER_ACTIVE */
