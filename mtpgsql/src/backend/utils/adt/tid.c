/*-------------------------------------------------------------------------
 *
 * tid.c
 *	  Functions for the built-in type tuple id
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  $Header: /cvs/weaver/mtpgsql/src/backend/utils/adt/tid.c,v 1.1.1.1 2006/08/12 00:21:53 synmscott Exp $
 *
 * NOTES
 *	  input routine largely stolen from boxin().
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include "utils/builtins.h"
#define LDELIM			'('
#define RDELIM			')'
#define DELIM			','
#define NTIDARGS		2

/* ----------------------------------------------------------------
 *		tidin
 * ----------------------------------------------------------------
 */
ItemPointer
tidin(const char *str)
{
	const char *p,
			   *coord[NTIDARGS];
	int			i;
	ItemPointer result;

	BlockNumber blockNumber;
	OffsetNumber offsetNumber;

	if (str == NULL)
		return NULL;

	for (i = 0, p = str; *p && i < NTIDARGS && *p != RDELIM; p++)
		if (*p == DELIM || (*p == LDELIM && !i))
			coord[i++] = p + 1;

	/* if (i < NTIDARGS - 1) */
	if (i < NTIDARGS)
	{
		elog(ERROR, "%s invalid tid format", str);
		return NULL;
	}

	blockNumber = (BlockNumber) atoi(coord[0]);
	offsetNumber = (OffsetNumber) atoi(coord[1]);

	result = (ItemPointer) palloc(sizeof(ItemPointerData));
	ItemPointerSet(result, blockNumber, offsetNumber);

	return result;
}

/* ----------------------------------------------------------------
 *		tidout
 * ----------------------------------------------------------------
 */
char *
tidout(ItemPointer itemPtr)
{
	BlockNumber blockNumber;
	OffsetNumber offsetNumber;
	char		buf[32];
	char	   *str;
	static char *invalidTid = "()";

	if (!itemPtr || !ItemPointerIsValid(itemPtr))
	{
		str = palloc(strlen(invalidTid));
		strcpy(str, invalidTid);
		return str;
	}

	blockNumber = ItemPointerGetBlockNumber(itemPtr);
	offsetNumber = ItemPointerGetOffsetNumber(itemPtr);

	sprintf(buf, "(%d,%d)", blockNumber, offsetNumber);

	str = (char *) palloc(strlen(buf) + 1);
	strcpy(str, buf);

	return str;
}

/*****************************************************************************
 *	 PUBLIC ROUTINES														 *
 *****************************************************************************/

bool
tideq(ItemPointer arg1, ItemPointer arg2)
{
	if ((!arg1) || (!arg2))
		return false;

	return ( (ItemPointerGetBlockNumber(arg1) ==
			  ItemPointerGetBlockNumber(arg2)) &&
			 (ItemPointerGetOffsetNumber(arg1) == 
              ItemPointerGetOffsetNumber(arg2)) );
}

bool
tidne(ItemPointer arg1, ItemPointer arg2)
{
	if ((!arg1) || (!arg2))
		return false;
	return (ItemPointerGetBlockNumber(arg1) !=
			ItemPointerGetBlockNumber(arg2) ||
			ItemPointerGetOffsetNumber(arg1) != 
                        ItemPointerGetOffsetNumber(arg2));
}

text *
tid_text(ItemPointer tid)
{
	char	   *str;

	if (!tid)
		return (text *) NULL;
	str = tidout(tid);

	return textin(str);
}	/* tid_text() */

ItemPointer
text_tid(const text *string)
{
	ItemPointer result;
	char	   *str;

	if (!string)
		return (ItemPointer) 0;

	str = textout((text *) string);
	result = tidin(str);
	pfree(str);

	return result;
}	/* text_tid() */


/*
 *	Functions to get latest tid of a specified tuple.
 *	Maybe these implementations is moved
 *	to another place
*/
#include <utils/relcache.h>
ItemPointer
currtid_byreloid(Oid reloid, ItemPointer tid)
{
	ItemPointer result = NULL;
	ItemPointerData      ret;
	Relation             rel;

	result = (ItemPointer) palloc(sizeof(ItemPointerData));
	ItemPointerSetInvalid(result);
	if (rel = heap_open(reloid, AccessShareLock), rel)
	{
		ret = heap_get_latest_tid(rel, SnapshotNow, tid);
		if ( ItemPointerIsValid(&ret) )
			ItemPointerCopy(&ret, result);
		heap_close(rel, AccessShareLock);
	}
	else
		elog(ERROR, "Relation %u not found", reloid);

	return result;
}	/* currtid_byreloid() */

ItemPointer
currtid_byrelname(const text *relname, ItemPointer tid)
{
	ItemPointer result = NULL;
	ItemPointerData      ret;
	char	   *str;
	Relation	rel;

	if (!relname)
		return result;

	str = textout((text *) relname);

	result = (ItemPointer) palloc(sizeof(ItemPointerData));
	ItemPointerSetInvalid(result);
	if (rel = heap_openr(str, AccessShareLock), rel)
	{
		ret = heap_get_latest_tid(rel, SnapshotNow, tid);
		if ( ItemPointerIsValid(&ret) )
			ItemPointerCopy(&ret, result);
		heap_close(rel, AccessShareLock);
	}
	else
		elog(ERROR, "Relation %s not found", textout((text *) relname));
	pfree(str);

	return result;
}	/* currtid_byrelname() */
