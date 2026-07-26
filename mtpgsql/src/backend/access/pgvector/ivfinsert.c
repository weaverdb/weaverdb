#include "postgres.h"

#include <float.h>

#include "access/genam.h"
#include "access/itup.h"
#include "fmgr.h"
#include "ivfflat.h"
#include "nodes/execnodes.h"
#include "storage/bufmgr.h"
#include "storage/lmgr.h"
#include "utils/memutils.h"
#include "utils/rel.h"

/*
 * Find the list that minimizes the distance function
 */
static void
FindInsertPage(Relation index, Datum *values, BlockNumber *insertPage, ListInfo * listInfo)
{
	double		minDistance = DBL_MAX;
	BlockNumber nextblkno = IVFFLAT_HEAD_BLKNO;
	FmgrInfo   *procinfo;
	Oid			collation;

	/* Avoid compiler warning */
	listInfo->blkno = nextblkno;
	listInfo->offno = FirstOffsetNumber;

	procinfo = index_getprocinfo(index, 1, IVFFLAT_DISTANCE_PROC);
	collation = InvalidOid;

	/* Search all list pages */
	while (BlockNumberIsValid(nextblkno))
	{
		Buffer		cbuf;
		Page		cpage;
		OffsetNumber maxoffno;

		cbuf = ReadBuffer(index, nextblkno);
		LockBuffer(index, cbuf, BUFFER_LOCK_SHARE);
		cpage = BufferGetPage(cbuf);
		maxoffno = PageGetMaxOffsetNumber(cpage);

		for (OffsetNumber offno = FirstOffsetNumber; offno <= maxoffno; offno = OffsetNumberNext(offno))
		{
			IvfflatList list;
			double		distance;

			list = (IvfflatList) PageGetItem(cpage, PageGetItemId(cpage, offno));
			distance = DatumGetFloat8(FunctionCall2Coll(procinfo, collation, values[0], PointerGetDatum(&list->center)));

			if (distance < minDistance || !BlockNumberIsValid(*insertPage))
			{
				*insertPage = list->insertPage;
				listInfo->blkno = nextblkno;
				listInfo->offno = offno;
				minDistance = distance;
			}
		}

		nextblkno = IvfflatPageGetOpaque(cpage)->nextblkno;

		UnlockReleaseBuffer(cbuf);
	}
}

/*
 * Insert a tuple into the index
 */
static void
InsertTuple(Relation index, Datum *values, bool *isnull, ItemPointer heap_tid)
{
	const		IvfflatTypeInfo *typeInfo = IvfflatGetTypeInfo(index);
	IndexTuple	itup;
	Datum		value;
	FmgrInfo   *normprocinfo;
	Buffer		buf;
	Page		page;
	Size		itemsz;
	BlockNumber insertPage = InvalidBlockNumber;
	ListInfo	listInfo;
	BlockNumber originalInsertPage;

	/* Detoast once for all calls */
	value = PointerGetDatum(PG_DETOAST_DATUM(values[0]));

	/* Normalize if needed */
	normprocinfo = IvfflatOptionalProcInfo(index, IVFFLAT_NORM_PROC);
	if (normprocinfo != NULL)
	{
		Oid			collation = InvalidOid;

		if (!IvfflatCheckNorm(normprocinfo, collation, value))
			return;

		value = IvfflatNormValue(typeInfo, collation, value);
	}

	/* Ensure index is valid */
	IvfflatGetMetaPageInfo(index, NULL, NULL);

	/* Find the insert page - sets the page and list info */
	FindInsertPage(index, &value, &insertPage, &listInfo);
	Assert(BlockNumberIsValid(insertPage));
	originalInsertPage = insertPage;

	/* Form tuple */
	itup = index_form_tuple(RelationGetDescr(index), &value, isnull);
	itup->t_tid = *heap_tid;

	/* Get tuple size */
	itemsz = MAXALIGN(IndexTupleSize(itup));
	Assert(itemsz <= BLCKSZ - MAXALIGN(SizeOfPageHeaderData) - MAXALIGN(sizeof(IvfflatPageOpaqueData)) - sizeof(ItemIdData));

	/* Find a page to insert the item */
	for (;;)
	{
		buf = ReadBuffer(index, insertPage);
		LockBuffer(index, buf, BUFFER_LOCK_EXCLUSIVE);

		page = BufferGetPage(buf);

		if (PageGetFreeSpace(page) >= itemsz)
			break;

		insertPage = IvfflatPageGetOpaque(page)->nextblkno;

		if (BlockNumberIsValid(insertPage))
		{
			/* Move to next page */
                        LockBuffer(index, buf, BUFFER_NOLOCK);
			ReleaseBuffer(index, buf);
		}
		else
		{
			Buffer		newbuf;
			Page		newpage;

			/* Add a new page */
			LockRelationForExtension(index, ExclusiveLock);
			newbuf = IvfflatNewBuffer(index, MAIN_FORKNUM);
			UnlockRelationForExtension(index, ExclusiveLock);

			/* Init new page */
			newpage = BufferGetPage(newbuf);
			IvfflatInitPage(newbuf, newpage);

			/* Update insert page */
			insertPage = BufferGetBlockNumber(newbuf);

			/* Update previous buffer */
			IvfflatPageGetOpaque(page)->nextblkno = insertPage;

			/* Commit */

			/* Unlock previous buffer */
                        LockBuffer(index, buf, BUFFER_NOLOCK);
			WriteBuffer(index, buf);

			/* Prepare new buffer */
			buf = newbuf;
			page = BufferGetPage(buf);
			break;
		}
	}

	/* Add to next offset */
	if (PageAddItem(page, (Item) itup, itemsz, InvalidOffsetNumber, false) == InvalidOffsetNumber)
		elog(ERROR, "failed to add index item to \"%s\"", RelationGetRelationName(index));

	IvfflatCommitBuffer(index, buf);

	/* Update the insert page */
	if (insertPage != originalInsertPage)
		IvfflatUpdateList(index, listInfo, insertPage, originalInsertPage, InvalidBlockNumber, MAIN_FORKNUM);
}

/*
 * Insert a tuple into the index
 */
bool
ivfflat_insertindex(Relation index, Datum *values, bool *isnull, ItemPointer heap_tid,
			  Relation heap, IndexUniqueCheck checkUnique
			  ,PgvectorIndexInfo *indexInfo
)
{
	MemoryContext oldCtx;
	MemoryContext insertCtx;

	/* Skip nulls */
	if (isnull[0])
		return false;

	/*
	 * Use memory context since detoast, IvfflatNormValue, and
	 * index_form_tuple can allocate
	 */
	insertCtx = AllocSetContextCreate(CurrentMemoryContext,
									  "Ivfflat insert temporary context",
									  ALLOCSET_DEFAULT_SIZES);
	oldCtx = MemoryContextSwitchTo(insertCtx);

	/* Insert tuple */
	InsertTuple(index, values, isnull, heap_tid);

	/* Delete memory context */
	MemoryContextSwitchTo(oldCtx);
	MemoryContextDelete(insertCtx);

	return false;
}
