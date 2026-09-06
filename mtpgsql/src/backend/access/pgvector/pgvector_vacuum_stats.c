/*-------------------------------------------------------------------------
 *
 * Count ivfflat/hnsw index entries for lazy VACUUM (no ORDER BY scan).
 *
 * Mid-write crashes can leave garbage nextblkno. Bound every chain walk
 * to RelationGetNumberOfBlocks so we never ReadBuffer out of range.
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "access/genam.h"
#include "catalog/pg_am.h"
#include "hnsw.h"
#include "ivfflat.h"
#include "pgvector_pagewalk.h"
#include "storage/bufmgr.h"
#include "storage/itemid.h"
#include "utils/rel.h"

static TupleCount
hnsw_count_index_tuples(Relation index)
{
	TupleCount	count = 0;
	BlockNumber blkno = HNSW_HEAD_BLKNO;
	BlockNumber nblocks = RelationGetNumberOfBlocks(index);
	int			steps = 0;

	while (PgvectorBlockInRange(index, blkno) && steps++ < nblocks)
	{
		Buffer		buf;
		Page		page;
		OffsetNumber offno;
		OffsetNumber maxoffno;

		buf = ReadBuffer(index, blkno);
		LockBuffer(index, buf, BUFFER_LOCK_SHARE);
		page = BufferGetPage(buf);
		if (PageIsNew(page) || PageIsEmpty(page) ||
			HnswPageGetOpaque(page)->page_id != HNSW_PAGE_ID)
		{
			UnlockReleaseBuffer(buf);
			break;
		}
		maxoffno = PgvectorClampMaxOff(PageGetMaxOffsetNumber(page));

		for (offno = FirstOffsetNumber; offno <= maxoffno; offno = OffsetNumberNext(offno))
		{
			ItemId		itemid = PageGetItemId(page, offno);
			HnswElementTuple etup;
			int			i;

			if (!ItemIdIsUsed(itemid))
				continue;
			etup = (HnswElementTuple) PageGetItem(page, itemid);

			if (!HnswIsElementTuple(etup) || etup->deleted)
				continue;

			for (i = 0; i < HNSW_HEAPTIDS; i++)
			{
				if (!ItemPointerIsValid(&etup->heaptids[i]))
					break;
				count++;
			}
		}

		blkno = PgvectorSafeNextBlkno(index, blkno, HnswPageGetOpaque(page)->nextblkno);
		UnlockReleaseBuffer(buf);
	}

	return count;
}

static TupleCount
ivfflat_count_index_tuples(Relation index)
{
	TupleCount	count = 0;
	BlockNumber listBlkno = IVFFLAT_HEAD_BLKNO;
	BlockNumber nblocks = RelationGetNumberOfBlocks(index);
	int			list_steps = 0;

	while (PgvectorBlockInRange(index, listBlkno) && list_steps++ < nblocks)
	{
		Buffer		cbuf;
		Page		cpage;
		OffsetNumber coffno;
		OffsetNumber cmaxoffno;
		BlockNumber listPages[MaxOffsetNumber];

		cbuf = ReadBuffer(index, listBlkno);
		LockBuffer(index, cbuf, BUFFER_LOCK_SHARE);
		cpage = BufferGetPage(cbuf);
		if (PageIsNew(cpage) || PageIsEmpty(cpage) ||
			IvfflatPageGetOpaque(cpage)->page_id != IVFFLAT_PAGE_ID)
		{
			UnlockReleaseBuffer(cbuf);
			break;
		}
		cmaxoffno = PgvectorClampMaxOff(PageGetMaxOffsetNumber(cpage));

		for (coffno = FirstOffsetNumber; coffno <= cmaxoffno; coffno = OffsetNumberNext(coffno))
		{
			ItemId		itemid = PageGetItemId(cpage, coffno);
			IvfflatList list;

			if (!ItemIdIsUsed(itemid))
			{
				listPages[coffno - FirstOffsetNumber] = InvalidBlockNumber;
				continue;
			}
			list = (IvfflatList) PageGetItem(cpage, itemid);
			listPages[coffno - FirstOffsetNumber] = list->startPage;
		}

		listBlkno = PgvectorSafeNextBlkno(index, listBlkno,
										 IvfflatPageGetOpaque(cpage)->nextblkno);
		UnlockReleaseBuffer(cbuf);

		for (coffno = FirstOffsetNumber; coffno <= cmaxoffno; coffno = OffsetNumberNext(coffno))
		{
			BlockNumber entryBlkno = listPages[coffno - FirstOffsetNumber];
			int			entry_steps = 0;

			while (PgvectorBlockInRange(index, entryBlkno) && entry_steps++ < nblocks)
			{
				Buffer		buf;
				Page		page;
				OffsetNumber offno;
				OffsetNumber maxoffno;

				buf = ReadBuffer(index, entryBlkno);
				LockBuffer(index, buf, BUFFER_LOCK_SHARE);
				page = BufferGetPage(buf);
				if (PageIsNew(page) || PageIsEmpty(page) ||
					IvfflatPageGetOpaque(page)->page_id != IVFFLAT_PAGE_ID)
				{
					UnlockReleaseBuffer(buf);
					break;
				}
				maxoffno = PgvectorClampMaxOff(PageGetMaxOffsetNumber(page));

				for (offno = FirstOffsetNumber; offno <= maxoffno; offno = OffsetNumberNext(offno))
				{
					if (ItemIdIsUsed(PageGetItemId(page, offno)))
						count++;
				}

				entryBlkno = PgvectorSafeNextBlkno(index, BufferGetBlockNumber(buf),
												   IvfflatPageGetOpaque(page)->nextblkno);
				UnlockReleaseBuffer(buf);
			}
		}
	}

	return count;
}

TupleCount
pgvector_lazy_index_tuple_count(Relation index)
{
	if (index->rd_rel->relam == HNSW_AM_OID)
		return hnsw_count_index_tuples(index);
	if (index->rd_rel->relam == IVFFLAT_AM_OID)
		return ivfflat_count_index_tuples(index);

	return 0;
}
