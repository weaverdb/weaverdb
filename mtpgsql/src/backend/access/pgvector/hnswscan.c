#include "postgres.h"

#include <limits.h>

#include "access/genam.h"
#include "access/relscan.h"
#include "hnsw.h"
#include "lib/pairingheap.h"
#include "miscadmin.h"
#include "nodes/pg_list.h"
#include "pgvector_scan.h"
#include "storage/lmgr.h"
#include "utils/float.h"
#include "utils/memutils.h"
#include "utils/relcache.h"
#include "utils/snapmgr.h"
#include "access/blobstorage.h"

#include "varatt.h"

/*
 * Algorithm 5 from paper
 */
static List *
GetScanItems(IndexScanDesc scan, Datum value)
{
	HnswScanOpaque so = (HnswScanOpaque) scan->opaque;
	Relation	index = pgvector_hnsw_index_rel(scan);
	HnswSupport *support = &so->support;
	List	   *ep;
	List	   *w;
	int			m;
	HnswElement entryPoint;
	char	   *base = NULL;
	HnswQuery  *q = &so->q;

	/* Get m and entry point */
	HnswGetMetaPageInfo(index, &m, &entryPoint);

	q->value = value;
	so->m = m;

	if (entryPoint == NULL)
		return NIL;

	ep = list_make1(HnswEntryCandidate(base, entryPoint, q, index, support, false));

	for (int lc = entryPoint->level; lc >= 1; lc--)
	{
		w = HnswSearchLayer(base, q, ep, 1, lc, index, support, m, false, NULL, NULL, NULL, true, NULL);
		ep = w;
	}

	return HnswSearchLayer(base, q, ep, hnsw_ef_search, 0, index, support, m, false, NULL, &so->v, hnsw_iterative_scan != HNSW_ITERATIVE_SCAN_OFF ? &so->discarded : NULL, true, &so->tuples);
}

/*
 * Resume scan at ground level with discarded candidates
 */
static List *
ResumeScanItems(IndexScanDesc scan)
{
	HnswScanOpaque so = (HnswScanOpaque) scan->opaque;
	Relation	index = pgvector_hnsw_index_rel(scan);
	List	   *ep = NIL;
	char	   *base = NULL;
	int			batch_size = hnsw_ef_search;

	if (pairingheap_is_empty(so->discarded))
		return NIL;

	/* Get next batch of candidates */
	for (int i = 0; i < batch_size; i++)
	{
		HnswSearchCandidate *sc;

		if (pairingheap_is_empty(so->discarded))
			break;

		sc = HnswGetSearchCandidate(w_node, pairingheap_remove_first(so->discarded));

		ep = lappend(ep, sc);
	}

	return HnswSearchLayer(base, &so->q, ep, batch_size, 0, index, &so->support, so->m, false, NULL, &so->v, &so->discarded, false, &so->tuples);
}

/*
 * Get scan value
 */
static Datum
GetScanValue(IndexScanDesc scan)
{
	HnswScanOpaque so = (HnswScanOpaque) scan->opaque;
	Datum		value;
	ScanKey		orderby = pgvector_hnsw_orderby(scan);

	if (orderby->sk_flags & SK_ISNULL)
		value = PointerGetDatum(NULL);
	else
	{
		value = orderby->sk_argument;

		if (DatumGetPointer(value) != NULL && ISINDIRECT(value))
			value = materialize_blob_datum(value);
		Assert(DatumGetPointer(value) == NULL || !ISINDIRECT(value));

		/* Normalize if needed */
		if (so->support.normprocinfo != NULL)
			value = HnswNormValue(so->typeInfo, so->support.collation, value);
	}

	return value;
}

#if defined(HNSW_MEMORY)
/*
 * Show memory usage
 */
static void
ShowMemoryUsage(HnswScanOpaque so)
{
	elog(INFO, "memory: %zu KB, tuples: " INT64_FORMAT, MemoryContextMemAllocated(so->tmpCtx, false) / 1024, so->tuples);
}
#endif

/*
 * Prepare for an index scan
 */
IndexScanDesc
hnsw_beginscanindex(Relation index, int nkeys, int norderbys)
{
	IndexScanDesc scan;
	HnswScanOpaque so;
	double		maxMemory;

	scan = RelationGetIndexScan(index, false, (uint16) nkeys, NULL);

	so = (HnswScanOpaque) palloc(sizeof(HnswScanOpaqueData));
	memset(so, 0, sizeof(HnswScanOpaqueData));
	so->numberOfOrderBys = norderbys;
	so->xs_snapshot = SnapshotNow;
	scan->opaque = so;

	so->typeInfo = HnswGetTypeInfo(index);

	/* Set support functions */
	HnswInitSupport(&so->support, index);

	/*
	 * Use a lower max allocation size than default to allow scanning more
	 * tuples for iterative search before exceeding work_mem
	 */
	so->tmpCtx = AllocSetContextCreate(CurrentMemoryContext,
									   "Hnsw scan temporary context",
									   0, 8 * 1024, 256 * 1024);

	/* Calculate max memory */
	/* Add 256 extra bytes to fill last block when close */
	maxMemory = (double) work_mem * hnsw_scan_mem_multiplier * 1024.0 + 256;
	so->maxMemory = Min(maxMemory, (double) (SIZE_MAX / 2));

	return scan;
}

/*
 * Start or restart an index scan
 */
void
hnsw_rescanindex(IndexScanDesc scan, ScanKey keys, int nkeys, ScanKey orderbys, int norderbys)
{
	HnswScanOpaque so = (HnswScanOpaque) scan->opaque;

	/*
	 * RelationGetIndexScan() calls amrescan before the AM has allocated
	 * opaque (same pattern as btrescan). Skip until beginscan finishes.
	 */
	if (so == NULL)
		return;

	so->first = true;
	so->plainScan = false;
	so->plainBlkno = InvalidBlockNumber;
	/* v and discarded are allocated in tmpCtx */
	so->v.tids = NULL;
	so->discarded = NULL;
	so->tuples = 0;
	so->previousDistance = -get_float8_infinity();
	MemoryContextReset(so->tmpCtx);

	if (keys && scan->numberOfKeys > 0)
		memmove(scan->keyData, keys, scan->numberOfKeys * sizeof(ScanKeyData));

	if (orderbys && norderbys > 0)
		pgvector_hnsw_set_orderbys(scan, orderbys, norderbys);
}

/*
 * Sequential index scan without ORDER BY (lazy VACUUM index stats).
 */
static bool
hnsw_plain_gettuple(IndexScanDesc scan)
{
	HnswScanOpaque so = (HnswScanOpaque) scan->opaque;
	Relation	index = pgvector_hnsw_index_rel(scan);

	if (!so->plainScan)
	{
		so->plainScan = true;
		so->plainBlkno = HNSW_HEAD_BLKNO;
		so->plainOffno = FirstOffsetNumber;
		so->plainTidIdx = 0;
		pgstat_count_index_scan(index);
	}

	while (BlockNumberIsValid(so->plainBlkno))
	{
		Buffer		buf;
		Page		page;
		OffsetNumber maxoffno;
		HnswElementTuple etup;

		buf = ReadBuffer(index, so->plainBlkno);
		LockBuffer(index, buf, BUFFER_LOCK_SHARE);
		page = BufferGetPage(buf);
		maxoffno = PageGetMaxOffsetNumber(page);

		for (; so->plainOffno <= maxoffno; so->plainOffno = OffsetNumberNext(so->plainOffno))
		{
			etup = (HnswElementTuple) PageGetItem(page, PageGetItemId(page, so->plainOffno));

			if (!HnswIsElementTuple(etup))
				continue;

			for (; so->plainTidIdx < HNSW_HEAPTIDS; so->plainTidIdx++)
			{
				if (!ItemPointerIsValid(&etup->heaptids[so->plainTidIdx]))
					break;

				scan->currentItemData = etup->heaptids[so->plainTidIdx];
				scan->xs_ctup.t_self = etup->heaptids[so->plainTidIdx];
				so->plainTidIdx++;
				UnlockReleaseBuffer(buf);
				return true;
			}
			so->plainTidIdx = 0;
		}

		so->plainBlkno = HnswPageGetOpaque(page)->nextblkno;
		so->plainOffno = FirstOffsetNumber;
		UnlockReleaseBuffer(buf);
	}

	return false;
}

/*
 * Fetch the next tuple in the given scan
 */
bool
hnsw_gettupleindex(IndexScanDesc scan, ScanDirection dir)
{
	HnswScanOpaque so = (HnswScanOpaque) scan->opaque;
	MemoryContext oldCtx;

	/*
	 * Index can be used to scan backward, but Postgres doesn't support
	 * backward scan on operators
	 */
	Assert(ScanDirectionIsForward(dir));

	if (pgvector_hnsw_norderbys(scan) <= 0)
		return hnsw_plain_gettuple(scan);

	oldCtx = MemoryContextSwitchTo(so->tmpCtx);

	if (so->first)
	{
		Datum		value;

		/* Count index scan for stats */
		pgstat_count_index_scan(pgvector_hnsw_index_rel(scan));

		/* Get scan value */
		value = GetScanValue(scan);

		/*
		 * Get a shared lock. This allows vacuum to ensure no in-flight scans
		 * before marking tuples as deleted.
		 */
		LockPage(pgvector_hnsw_index_rel(scan), HNSW_SCAN_LOCK, ShareLock);

		so->w = GetScanItems(scan, value);

		/* Release shared lock */
		UnlockPage(pgvector_hnsw_index_rel(scan), HNSW_SCAN_LOCK, ShareLock);

		so->first = false;

#if defined(HNSW_MEMORY)
		ShowMemoryUsage(so);
#endif
	}

	for (;;)
	{
		char	   *base = NULL;
		HnswSearchCandidate *sc;
		HnswElement element;
		ItemPointer heaptid;

		if (list_length(so->w) == 0)
		{
			if (hnsw_iterative_scan == HNSW_ITERATIVE_SCAN_OFF)
				break;

			/* Empty index */
			if (so->discarded == NULL)
				break;

			/* Reached max number of tuples or memory limit */
			if (so->tuples >= hnsw_max_scan_tuples || MemoryContextMemAllocated(so->tmpCtx, false) > so->maxMemory)
			{
				if (pairingheap_is_empty(so->discarded))
					break;

				/* Return remaining tuples */
				so->w = lappend(so->w, HnswGetSearchCandidate(w_node, pairingheap_remove_first(so->discarded)));
			}
			else
			{
				LockPage(pgvector_hnsw_index_rel(scan), HNSW_SCAN_LOCK, ShareLock);

				so->w = ResumeScanItems(scan);

				UnlockPage(pgvector_hnsw_index_rel(scan), HNSW_SCAN_LOCK, ShareLock);

#if defined(HNSW_MEMORY)
				ShowMemoryUsage(so);
#endif
			}

			if (list_length(so->w) == 0)
				break;
		}

		sc = llast(so->w);
		element = HnswPtrAccess(base, sc->element);

		/* Move to next element if no valid heap TIDs */
		if (element->heaptidsLength == 0)
		{
			so->w = list_delete_last(so->w);

			/* Mark memory as free for next iteration */
			if (hnsw_iterative_scan != HNSW_ITERATIVE_SCAN_OFF)
			{
				pfree(element);
				pfree(sc);
			}

			continue;
		}

		heaptid = &element->heaptids[--element->heaptidsLength];

		if (hnsw_iterative_scan == HNSW_ITERATIVE_SCAN_STRICT)
		{
			if (sc->distance < so->previousDistance)
				continue;

			so->previousDistance = sc->distance;
		}

		MemoryContextSwitchTo(oldCtx);

		scan->currentItemData = *heaptid;
		scan->xs_ctup.t_self = *heaptid;
		return true;
	}

	MemoryContextSwitchTo(oldCtx);
	return false;
}

/*
 * End a scan and release resources
 */
void
hnsw_endscanindex(IndexScanDesc scan)
{
	HnswScanOpaque so = (HnswScanOpaque) scan->opaque;

	MemoryContextDelete(so->tmpCtx);

	pfree(so);
	scan->opaque = NULL;
}
