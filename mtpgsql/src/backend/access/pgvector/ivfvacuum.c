#include "postgres.h"

#include "access/genam.h"
#include "access/itup.h"
#include "catalog/pg_index.h"
#include "commands/vacuum.h"
#include "env/env.h"
#include "ivfflat.h"
#include "env/freespace.h"
#include "storage/bufmgr.h"
#include "storage/itemid.h"
#include "utils/rel.h"
#include "utils/relcache.h"
#include "utils/syscache.h"


/*
 * Bulk delete tuples from the index
 */
IndexBulkDeleteResult *
ivfflat_bulkdeleteindex(IndexVacuumInfo *info, IndexBulkDeleteResult *stats,
				  IndexBulkDeleteCallback callback, void *callback_state)
{
	Relation	index = info->index;
	BlockNumber blkno = IVFFLAT_HEAD_BLKNO;
	BufferAccessStrategy bas = GetAccessStrategy(BAS_BULKREAD);

	if (stats == NULL)
		stats = (IndexBulkDeleteResult *) palloc0(sizeof(IndexBulkDeleteResult));

	/* Iterate over list pages */
	while (BlockNumberIsValid(blkno))
	{
		Buffer		cbuf;
		Page		cpage;
		OffsetNumber coffno;
		OffsetNumber cmaxoffno;
		BlockNumber listPages[MaxOffsetNumber];
		ListInfo	listInfo;

		cbuf = ReadBuffer(index, blkno);
		LockBuffer(index, cbuf, BUFFER_LOCK_SHARE);
		cpage = BufferGetPage(cbuf);

		cmaxoffno = PageGetMaxOffsetNumber(cpage);

		/* Iterate over lists */
		for (coffno = FirstOffsetNumber; coffno <= cmaxoffno; coffno = OffsetNumberNext(coffno))
		{
			IvfflatList list = (IvfflatList) PageGetItem(cpage, PageGetItemId(cpage, coffno));

			listPages[coffno - FirstOffsetNumber] = list->startPage;
		}

		listInfo.blkno = blkno;
		blkno = IvfflatPageGetOpaque(cpage)->nextblkno;

		UnlockReleaseBuffer(cbuf);

		for (coffno = FirstOffsetNumber; coffno <= cmaxoffno; coffno = OffsetNumberNext(coffno))
		{
			BlockNumber searchPage = listPages[coffno - FirstOffsetNumber];
			BlockNumber insertPage = InvalidBlockNumber;

			/* Iterate over entry pages */
			while (BlockNumberIsValid(searchPage))
			{
				Buffer		buf;
				Page		page;
				OffsetNumber offno;
				OffsetNumber maxoffno;
				OffsetNumber deletable[MaxOffsetNumber];
				int			ndeletable;

				vacuum_delay_point();

				buf = ReadBufferExtended(index, MAIN_FORKNUM, searchPage, RBM_NORMAL, bas);

				/*
				 * ambulkdelete cannot delete entries from pages that are
				 * pinned by other backends
				 *
				 * https://www.postgresql.org/docs/current/index-locking.html
				 */
				LockBufferForCleanup(buf);

				page = BufferGetPage(buf);

				maxoffno = PageGetMaxOffsetNumber(page);
				ndeletable = 0;

				/* Find deleted tuples */
				for (offno = FirstOffsetNumber; offno <= maxoffno; offno = OffsetNumberNext(offno))
				{
					IndexTuple	itup = (IndexTuple) PageGetItem(page, PageGetItemId(page, offno));
					ItemPointer htup = &(itup->t_tid);

					if (callback(htup, callback_state))
					{
						deletable[ndeletable++] = offno;
						stats->tuples_removed++;
					}
					else
						stats->num_index_tuples++;
				}

				/* Set to first free page */
				/* Must be set before searchPage is updated */
				if (!BlockNumberIsValid(insertPage) && ndeletable > 0)
					insertPage = searchPage;

				searchPage = IvfflatPageGetOpaque(page)->nextblkno;

				if (ndeletable > 0)
				{
					/*
					 * Delete tuples and commit the buffer. Weaver persists
					 * page mutations via WriteBuffer (IvfflatCommitBuffer);
					 * UnlockReleaseBuffer alone would drop in-memory deletes
					 * on eviction.
					 */
					PageIndexMultiDelete(page, deletable, ndeletable);
					IvfflatCommitBuffer(index, buf);
				}
				else
					UnlockReleaseBuffer(buf);
			}

			/*
			 * Update after all tuples deleted.
			 *
			 * We don't add or delete items from lists pages, so offset won't
			 * change.
			 */
			if (BlockNumberIsValid(insertPage))
			{
				listInfo.offno = coffno;
				IvfflatUpdateList(index, listInfo, insertPage, InvalidBlockNumber, InvalidBlockNumber, MAIN_FORKNUM);
			}
		}
	}

	FreeAccessStrategy(bas);

	return stats;
}

/*
 * Clean up after a VACUUM operation
 */
IndexBulkDeleteResult *
ivfflat_vacuumcleanupindex(IndexVacuumInfo *info, IndexBulkDeleteResult *stats)
{
	Relation	rel = info->index;

	if (info->analyze_only)
		return stats;

	/* stats is NULL if ambulkdelete not called */
	/* OK to return NULL if index not changed */
	if (stats == NULL)
		return NULL;

	stats->num_pages = RelationGetNumberOfBlocks(rel);

	return stats;
}

/*
 * True if blkno is on the IVFFlat list-page chain (centers), not an entry page.
 */
static bool
IvfflatIsListPage(Relation index, BlockNumber blkno)
{
	BlockNumber cur = IVFFLAT_HEAD_BLKNO;

	while (BlockNumberIsValid(cur))
	{
		Buffer		buf;
		Page		page;

		if (cur == blkno)
			return true;

		buf = ReadBuffer(index, cur);
		LockBuffer(index, buf, BUFFER_LOCK_SHARE);
		page = BufferGetPage(buf);
		cur = IvfflatPageGetOpaque(page)->nextblkno;
		UnlockReleaseBuffer(buf);
	}
	return false;
}

/*
 * Heap TID is stale if the heap block is missing, the offset is past
 * maxoff, or the line pointer is unused — same rule as btree recover.
 */
static bool
IvfflatHeapTidIsStale(Relation heaprel, ItemPointer tid)
{
	Buffer		heapbuf;
	Page		heappage;
	OffsetNumber off;
	bool		stale = false;

	if (!ItemPointerIsValid(tid))
		return true;

	heapbuf = ReadBuffer(heaprel, ItemPointerGetBlockNumber(tid));
	if (!BufferIsValid(heapbuf))
		return true;

	LockBuffer(heaprel, heapbuf, BUFFER_LOCK_SHARE);
	heappage = BufferGetPage(heapbuf);
	off = ItemPointerGetOffsetNumber(tid);
	if (off > PageGetMaxOffsetNumber(heappage))
		stale = true;
	else if (!ItemIdIsUsed(PageGetItemId(heappage, off)))
		stale = true;
	LockBuffer(heaprel, heapbuf, BUFFER_LOCK_UNLOCK);
	ReleaseBuffer(heaprel, heapbuf);
	return stale;
}

/*
 * Recover one IVFFlat index page after crash: drop entry IndexTuples whose
 * heap TIDs point at unused/missing line pointers. List and meta pages are
 * left alone. Returns blkno if the page is empty (freespace candidate), else
 * InvalidBlockNumber — same contract as btrecoverpage.
 */
BlockNumber
ivfflat_recoverpage(Relation index, BlockNumber blkno)
{
	Buffer		buf;
	Page		page;
	Relation	heaprel;
	HeapTuple	indextup;
	Oid			heapid;
	OffsetNumber offno;
	OffsetNumber maxoff;
	OffsetNumber deletable[MaxOffsetNumber];
	int			ndeletable = 0;
	bool		dryrun = IsReadOnlyProcessingMode();
	bool		empty;

	if (blkno == IVFFLAT_METAPAGE_BLKNO)
		return InvalidBlockNumber;

	if (IvfflatIsListPage(index, blkno))
		return InvalidBlockNumber;

	buf = ReadBuffer(index, blkno);
	LockBuffer(index, buf, BUFFER_LOCK_SHARE);
	page = BufferGetPage(buf);

	if (PageIsNew(page) || PageIsEmpty(page))
	{
		UnlockReleaseBuffer(buf);
		return blkno;
	}

	if (IvfflatPageGetOpaque(page)->page_id != IVFFLAT_PAGE_ID)
	{
		UnlockReleaseBuffer(buf);
		return InvalidBlockNumber;
	}

	UnlockReleaseBuffer(buf);

	if (dryrun)
		return InvalidBlockNumber;

	indextup = SearchSysCacheTuple(INDEXRELID,
								   ObjectIdGetDatum(RelationGetRelid(index)),
								   PointerGetDatum(NULL),
								   PointerGetDatum(NULL),
								   PointerGetDatum(NULL));
	if (!HeapTupleIsValid(indextup))
		return InvalidBlockNumber;

	heapid = SysCacheGetAttr(INDEXRELID, indextup,
							 Anum_pg_index_indrelid, NULL);
	heaprel = RelationIdGetRelation(heapid, DEFAULTDBOID);
	if (!RelationIsValid(heaprel))
		return InvalidBlockNumber;

	buf = ReadBuffer(index, blkno);
	LockBufferForCleanup(buf);
	page = BufferGetPage(buf);
	maxoff = PageGetMaxOffsetNumber(page);

	for (offno = FirstOffsetNumber; offno <= maxoff; offno = OffsetNumberNext(offno))
	{
		ItemId		itemid = PageGetItemId(page, offno);
		IndexTuple	itup;
		ItemPointer htup;

		if (!ItemIdIsUsed(itemid))
			continue;

		itup = (IndexTuple) PageGetItem(page, itemid);
		htup = &(itup->t_tid);
		if (IvfflatHeapTidIsStale(heaprel, htup))
		{
			deletable[ndeletable++] = offno;
			elog(NOTICE,
				 "ivfflat: Removing orphan index tuple block: %lu offset: %u",
				 (unsigned long) blkno, offno);
		}
	}

	if (ndeletable > 0)
	{
		PageIndexMultiDelete(page, deletable, ndeletable);
		IvfflatCommitBuffer(index, buf);
	}
	else
		UnlockReleaseBuffer(buf);

	RelationClose(heaprel);

	buf = ReadBuffer(index, blkno);
	LockBuffer(index, buf, BUFFER_LOCK_SHARE);
	page = BufferGetPage(buf);
	empty = PageIsNew(page) || PageIsEmpty(page);
	UnlockReleaseBuffer(buf);

	return empty ? blkno : InvalidBlockNumber;
}
