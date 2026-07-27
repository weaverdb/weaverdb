/*-------------------------------------------------------------------------
 *
 * Count ivfflat/hnsw index entries for lazy VACUUM (no ORDER BY scan).
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "access/genam.h"
#include "catalog/pg_am.h"
#include "hnsw.h"
#include "ivfflat.h"
#include "storage/bufmgr.h"
#include "utils/rel.h"

static TupleCount
hnsw_count_index_tuples(Relation index)
{
	TupleCount	count = 0;
	BlockNumber blkno = HNSW_HEAD_BLKNO;

	while (BlockNumberIsValid(blkno))
	{
		Buffer		buf;
		Page		page;
		OffsetNumber offno;
		OffsetNumber maxoffno;

		buf = ReadBuffer(index, blkno);
		LockBuffer(index, buf, BUFFER_LOCK_SHARE);
		page = BufferGetPage(buf);
		maxoffno = PageGetMaxOffsetNumber(page);

		for (offno = FirstOffsetNumber; offno <= maxoffno; offno = OffsetNumberNext(offno))
		{
			HnswElementTuple etup;
			int			i;

			etup = (HnswElementTuple) PageGetItem(page, PageGetItemId(page, offno));

			if (!HnswIsElementTuple(etup))
				continue;

			for (i = 0; i < HNSW_HEAPTIDS; i++)
			{
				if (!ItemPointerIsValid(&etup->heaptids[i]))
					break;
				count++;
			}
		}

		blkno = HnswPageGetOpaque(page)->nextblkno;
		UnlockReleaseBuffer(buf);
	}

	return count;
}

static TupleCount
ivfflat_count_index_tuples(Relation index)
{
	TupleCount	count = 0;
	BlockNumber listBlkno = IVFFLAT_HEAD_BLKNO;

	while (BlockNumberIsValid(listBlkno))
	{
		Buffer		cbuf;
		Page		cpage;
		OffsetNumber coffno;
		OffsetNumber cmaxoffno;

		cbuf = ReadBuffer(index, listBlkno);
		LockBuffer(index, cbuf, BUFFER_LOCK_SHARE);
		cpage = BufferGetPage(cbuf);
		cmaxoffno = PageGetMaxOffsetNumber(cpage);

		for (coffno = FirstOffsetNumber; coffno <= cmaxoffno; coffno = OffsetNumberNext(coffno))
		{
			IvfflatList list;
			BlockNumber entryBlkno;

			list = (IvfflatList) PageGetItem(cpage, PageGetItemId(cpage, coffno));
			entryBlkno = list->startPage;

			while (BlockNumberIsValid(entryBlkno))
			{
				Buffer		buf;
				Page		page;
				OffsetNumber offno;
				OffsetNumber maxoffno;

				buf = ReadBuffer(index, entryBlkno);
				LockBuffer(index, buf, BUFFER_LOCK_SHARE);
				page = BufferGetPage(buf);
				maxoffno = PageGetMaxOffsetNumber(page);

				for (offno = FirstOffsetNumber; offno <= maxoffno; offno = OffsetNumberNext(offno))
					count++;

				entryBlkno = IvfflatPageGetOpaque(page)->nextblkno;
				UnlockReleaseBuffer(buf);
			}
		}

		listBlkno = IvfflatPageGetOpaque(cpage)->nextblkno;
		UnlockReleaseBuffer(cbuf);
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
