#include "postgres.h"

#include <float.h>

#include "access/genam.h"
#include "access/itup.h"
#include "access/relscan.h"
#include "access/tupdesc.h"
#include "catalog/pg_type.h"
#include "fmgr.h"
#include "lib/pairingheap.h"
#include "access/heapam.h"
#include "pgvector_scan.h"
#include "miscadmin.h"
#include "pgstat.h"
#include "storage/bufmgr.h"
#include "utils/memutils.h"
#include "utils/rel.h"
#include "utils/snapmgr.h"
#include "pgvector_tuplesort.h"
#include "access/blobstorage.h"

#include "varatt.h"

#define GetScanList(ptr) pairingheap_container(IvfflatScanList, ph_node, ptr)
#define GetScanListConst(ptr) pairingheap_const_container(IvfflatScanList, ph_node, ptr)

/*
 * Compare list distances
 */
static int
CompareLists(const pairingheap_node *a, const pairingheap_node *b, void *arg)
{
	if (GetScanListConst(a)->distance > GetScanListConst(b)->distance)
		return 1;

	if (GetScanListConst(a)->distance < GetScanListConst(b)->distance)
		return -1;

	return 0;
}

/*
 * Get lists and sort by distance
 */
static void
GetScanLists(IndexScanDesc scan, Datum value)
{
	IvfflatScanOpaque so = (IvfflatScanOpaque) scan->opaque;
	BlockNumber nextblkno = IVFFLAT_HEAD_BLKNO;
	int			listCount = 0;
	double		maxDistance = DBL_MAX;

	/* Search all list pages */
	while (BlockNumberIsValid(nextblkno))
	{
		Buffer		cbuf;
		Page		cpage;
		OffsetNumber maxoffno;

		cbuf = ReadBuffer(pgvector_scan_index_rel(scan), nextblkno);
		LockBuffer(pgvector_scan_index_rel(scan), cbuf, BUFFER_LOCK_SHARE);
		cpage = BufferGetPage(cbuf);

		maxoffno = PageGetMaxOffsetNumber(cpage);

		for (OffsetNumber offno = FirstOffsetNumber; offno <= maxoffno; offno = OffsetNumberNext(offno))
		{
			IvfflatList list = (IvfflatList) PageGetItem(cpage, PageGetItemId(cpage, offno));
			double		distance;

			/* Use procinfo from the index instead of scan key for performance */
			distance = DatumGetFloat8(so->distfunc(so->procinfo, so->collation, PointerGetDatum(&list->center), value));

			if (listCount < so->maxProbes)
			{
				IvfflatScanList *scanlist;

				scanlist = &so->lists[listCount];
				scanlist->startPage = list->startPage;
				scanlist->distance = distance;
				listCount++;

				/* Add to heap */
				pairingheap_add(so->listQueue, &scanlist->ph_node);

				/* Calculate max distance */
				if (listCount == so->maxProbes)
					maxDistance = GetScanList(pairingheap_first(so->listQueue))->distance;
			}
			else if (distance < maxDistance)
			{
				IvfflatScanList *scanlist;

				/* Remove */
				scanlist = GetScanList(pairingheap_remove_first(so->listQueue));

				/* Reuse */
				scanlist->startPage = list->startPage;
				scanlist->distance = distance;
				pairingheap_add(so->listQueue, &scanlist->ph_node);

				/* Update max distance */
				maxDistance = GetScanList(pairingheap_first(so->listQueue))->distance;
			}
		}

		nextblkno = IvfflatPageGetOpaque(cpage)->nextblkno;

		do {
			LockBuffer(pgvector_scan_index_rel(scan), cbuf, BUFFER_LOCK_UNLOCK);
			ReleaseBuffer(pgvector_scan_index_rel(scan), cbuf);
		} while (0);
	}

	for (int i = listCount - 1; i >= 0; i--)
		so->listPages[i] = GetScanList(pairingheap_remove_first(so->listQueue))->startPage;

	Assert(pairingheap_is_empty(so->listQueue));
}

/*
 * Get items
 */
static void
GetScanItems(IndexScanDesc scan, Datum value)
{
	IvfflatScanOpaque so = (IvfflatScanOpaque) scan->opaque;
	TupleDesc	tupdesc = RelationGetDescr(pgvector_scan_index_rel(scan));
	TupleTableSlot *slot = so->vslot;
	int			batchProbes = 0;

	tuplesort_reset(so->sortstate);

	/* Search closest probes lists */
	while (so->listIndex < so->maxProbes && (++batchProbes) <= so->probes)
	{
		BlockNumber searchPage = so->listPages[so->listIndex++];

		/* Search all entry pages for list */
		while (BlockNumberIsValid(searchPage))
		{
			Buffer		buf;
			Page		page;
			OffsetNumber maxoffno;

			buf = ReadBufferExtended(pgvector_scan_index_rel(scan), MAIN_FORKNUM, searchPage, RBM_NORMAL, so->bas);
			LockBuffer(pgvector_scan_index_rel(scan), buf, BUFFER_LOCK_SHARE);
			page = BufferGetPage(buf);
			maxoffno = PageGetMaxOffsetNumber(page);

			for (OffsetNumber offno = FirstOffsetNumber; offno <= maxoffno; offno = OffsetNumberNext(offno))
			{
				IndexTuple	itup;
				Datum		datum;
				bool		isnull;
				ItemId		itemid = PageGetItemId(page, offno);

				itup = (IndexTuple) PageGetItem(page, itemid);
				datum = index_getattr(itup, 1, tupdesc, &isnull);

				if (isnull)
					continue;

				/*
				 * Add virtual tuple
				 *
				 * Use procinfo from the index instead of scan key for
				 * performance
				 */
				ExecClearTuple(slot);
				pgvector_slot_set_attr(slot, 1,
									   so->distfunc(so->procinfo, so->collation, datum, value),
									   false);
				pgvector_slot_set_attr(slot, 2, PointerGetDatum(&itup->t_tid), false);
				ExecStoreVirtualTuple(slot);

				tuplesort_puttupleslot(so->sortstate, slot);
			}

			searchPage = IvfflatPageGetOpaque(page)->nextblkno;

			do { LockBuffer(pgvector_scan_index_rel(scan), buf, BUFFER_LOCK_UNLOCK); ReleaseBuffer(pgvector_scan_index_rel(scan), buf); } while (0);
		}
	}

	tuplesort_performsort(so->sortstate);

#if defined(IVFFLAT_MEMORY)
	elog(INFO, "memory: %zu MB", MemoryContextMemAllocated(CurrentMemoryContext, true) / (1024 * 1024));
#endif
}

/*
 * Zero distance
 */
static Datum
ZeroDistance(FmgrInfo *flinfo, Oid collation, Datum arg1, Datum arg2)
{
	return Float8GetDatum(0.0);
}

/*
 * Get scan value
 */
static Datum
GetScanValue(IndexScanDesc scan)
{
	IvfflatScanOpaque so = (IvfflatScanOpaque) scan->opaque;
	Datum		value;

	if (pgvector_ivfflat_orderby(scan)->sk_flags & SK_ISNULL)
	{
		value = PointerGetDatum(NULL);
		so->distfunc = ZeroDistance;
	}
	else
	{
		value = pgvector_ivfflat_orderby(scan)->sk_argument;
		so->distfunc = FunctionCall2Coll;

		if (DatumGetPointer(value) != NULL && ISINDIRECT(value))
		{
			MemoryContext oldCtx = MemoryContextSwitchTo(so->tmpCtx);

			value = materialize_blob_datum(value);
			MemoryContextSwitchTo(oldCtx);
		}
		Assert(DatumGetPointer(value) == NULL || !ISINDIRECT(value));

		/* Normalize if needed */
		if (so->normprocinfo != NULL)
		{
			MemoryContext oldCtx = MemoryContextSwitchTo(so->tmpCtx);

			value = IvfflatNormValue(so->typeInfo, so->collation, value);

			MemoryContextSwitchTo(oldCtx);
		}
	}

	return value;
}

/*
 * Initialize scan sort state
 */
static Tuplesortstate *
InitScanSortState(TupleDesc tupdesc)
{
	AttrNumber	attNums[] = {1};
	Oid			sortOperators[] = {Float8LessOperator};
	Oid			sortCollations[] = {InvalidOid};
	bool		nullsFirstFlags[] = {false};

	return tuplesort_begin_heap(tupdesc, 1, attNums, sortOperators, sortCollations, nullsFirstFlags, work_mem, NULL, false);
}

/*
 * Prepare for an index scan
 */
IndexScanDesc
ivfflat_beginscanindex(Relation index, int nkeys, int norderbys)
{
	IndexScanDesc scan;
	IvfflatScanOpaque so;
	int			lists;
	int			dimensions;
	int			probes = ivfflat_probes;
	int			maxProbes;
	MemoryContext oldCtx;

	scan = RelationGetIndexScan(index, false, (uint16) nkeys, NULL);
	so = (IvfflatScanOpaque) palloc(sizeof(IvfflatScanOpaqueData));
	memset(so, 0, sizeof(IvfflatScanOpaqueData));
	so->numberOfOrderBys = norderbys;
	so->xs_snapshot = SnapshotNow;
	scan->opaque = so;

	IvfflatGetMetaPageInfo(index, &lists, &dimensions);

	if (ivfflat_iterative_scan != IVFFLAT_ITERATIVE_SCAN_OFF)
		maxProbes = Max(ivfflat_max_probes, probes);
	else
		maxProbes = probes;

	if (probes > lists)
		probes = lists;

	if (maxProbes > lists)
		maxProbes = lists;

	so->typeInfo = IvfflatGetTypeInfo(index);
	so->first = true;
	so->probes = probes;
	so->maxProbes = maxProbes;
	so->dimensions = dimensions;

	/* Set support functions */
	so->procinfo = index_getprocinfo(index, 1, IVFFLAT_DISTANCE_PROC);
	so->normprocinfo = IvfflatOptionalProcInfo(index, IVFFLAT_NORM_PROC);
	so->collation = InvalidOid;

	so->tmpCtx = AllocSetContextCreate(CurrentMemoryContext,
									   "Ivfflat scan temporary context",
									   ALLOCSET_DEFAULT_SIZES);

	oldCtx = MemoryContextSwitchTo(so->tmpCtx);

	/* Create tuple description for sorting */
	so->tupdesc = CreateTemplateTupleDesc(2);
	TupleDescInitEntry(so->tupdesc, (AttrNumber) 1, "distance", FLOAT8OID, -1, 0, false);
	TupleDescInitEntry(so->tupdesc, (AttrNumber) 2, "heaptid", TIDOID, -1, 0, false);

	/* Prep sort */
	so->sortstate = InitScanSortState(so->tupdesc);

	/* Need separate slots for puttuple and gettuple */
	so->vslot = MakeSingleTupleTableSlot(so->tupdesc, &TTSOpsVirtual);
	so->mslot = MakeSingleTupleTableSlot(so->tupdesc, &TTSOpsMinimalTuple);

	/*
	 * Reuse same set of shared buffers for scan
	 *
	 * See postgres/src/backend/storage/buffer/README for description
	 */
	so->bas = GetAccessStrategy(BAS_BULKREAD);

	so->listQueue = pairingheap_allocate(CompareLists, scan);
	so->listPages = palloc(maxProbes * sizeof(BlockNumber));
	so->listIndex = 0;
	so->lists = palloc(maxProbes * sizeof(IvfflatScanList));

	MemoryContextSwitchTo(oldCtx);

	return scan;
}

/*
 * Start or restart an index scan
 */
void
ivfflat_rescanindex(IndexScanDesc scan, ScanKey keys, int nkeys, ScanKey orderbys, int norderbys)
{
	IvfflatScanOpaque so = (IvfflatScanOpaque) scan->opaque;

	/*
	 * RelationGetIndexScan() calls amrescan before the AM has allocated
	 * opaque (same pattern as btrescan). Skip until beginscan finishes.
	 */
	if (so == NULL)
		return;

	so->first = true;
	so->plainScan = false;
	so->plainListBlkno = InvalidBlockNumber;
	pairingheap_reset(so->listQueue);
	so->listIndex = 0;

	if (keys && scan->numberOfKeys > 0)
		memmove(scan->keyData, keys, scan->numberOfKeys * sizeof(ScanKeyData));

	if (orderbys && norderbys > 0)
		pgvector_ivfflat_set_orderbys(scan, orderbys, norderbys);
}

/*
 * Sequential index scan without ORDER BY (lazy VACUUM index stats).
 */
static bool
ivfflat_plain_gettuple(IndexScanDesc scan)
{
	IvfflatScanOpaque so = (IvfflatScanOpaque) scan->opaque;
	Relation	index = pgvector_scan_index_rel(scan);

	if (!so->plainScan)
	{
		so->plainScan = true;
		so->plainListBlkno = IVFFLAT_HEAD_BLKNO;
		so->plainListOffno = FirstOffsetNumber;
		so->plainEntryBlkno = InvalidBlockNumber;
		so->plainEntryOffno = FirstOffsetNumber;
		pgstat_count_index_scan(index);
	}

	for (;;)
	{
		if (BlockNumberIsValid(so->plainEntryBlkno))
		{
			Buffer		buf;
			Page		page;
			OffsetNumber maxoffno;

			buf = ReadBuffer(index, so->plainEntryBlkno);
			LockBuffer(index, buf, BUFFER_LOCK_SHARE);
			page = BufferGetPage(buf);
			maxoffno = PageGetMaxOffsetNumber(page);

			if (so->plainEntryOffno <= maxoffno)
			{
				IndexTuple	itup;

				itup = (IndexTuple) PageGetItem(page, PageGetItemId(page, so->plainEntryOffno));
				scan->currentItemData = itup->t_tid;
				scan->xs_ctup.t_self = itup->t_tid;
				so->plainEntryOffno = OffsetNumberNext(so->plainEntryOffno);
				if (so->plainEntryOffno > maxoffno)
				{
					so->plainEntryBlkno = IvfflatPageGetOpaque(page)->nextblkno;
					so->plainEntryOffno = FirstOffsetNumber;
				}
				UnlockReleaseBuffer(buf);
				return true;
			}

			so->plainEntryBlkno = IvfflatPageGetOpaque(page)->nextblkno;
			so->plainEntryOffno = FirstOffsetNumber;
			UnlockReleaseBuffer(buf);
			continue;
		}

		if (!BlockNumberIsValid(so->plainListBlkno))
			return false;

		{
			Buffer		cbuf;
			Page		cpage;
			OffsetNumber cmaxoffno;
			IvfflatList list;

			cbuf = ReadBuffer(index, so->plainListBlkno);
			LockBuffer(index, cbuf, BUFFER_LOCK_SHARE);
			cpage = BufferGetPage(cbuf);
			cmaxoffno = PageGetMaxOffsetNumber(cpage);

			if (so->plainListOffno > cmaxoffno)
			{
				so->plainListBlkno = IvfflatPageGetOpaque(cpage)->nextblkno;
				so->plainListOffno = FirstOffsetNumber;
				UnlockReleaseBuffer(cbuf);
				continue;
			}

			list = (IvfflatList) PageGetItem(cpage, PageGetItemId(cpage, so->plainListOffno));
			so->plainEntryBlkno = list->startPage;
			so->plainEntryOffno = FirstOffsetNumber;
			so->plainListOffno = OffsetNumberNext(so->plainListOffno);
			UnlockReleaseBuffer(cbuf);
		}
	}
}

/*
 * Fetch the next tuple in the given scan
 */
bool
ivfflat_gettupleindex(IndexScanDesc scan, ScanDirection dir)
{
	IvfflatScanOpaque so = (IvfflatScanOpaque) scan->opaque;
	ItemPointer heaptid;
	bool		isnull;

	/*
	 * Index can be used to scan backward, but Postgres doesn't support
	 * backward scan on operators
	 */
	Assert(ScanDirectionIsForward(dir));

	if (pgvector_ivfflat_norderbys(scan) <= 0)
		return ivfflat_plain_gettuple(scan);

	if (so->first)
	{
		Datum		value;

		/* Count index scan for stats */
		pgstat_count_index_scan(pgvector_scan_index_rel(scan));

		value = GetScanValue(scan);
		IvfflatBench("GetScanLists", GetScanLists(scan, value));
		IvfflatBench("GetScanItems", GetScanItems(scan, value));
		so->first = false;
		so->value = value;
	}

	while (!tuplesort_gettupleslot(so->sortstate, true, false, so->mslot, NULL))
	{
		if (so->listIndex == so->maxProbes)
			return false;

		IvfflatBench("GetScanItems", GetScanItems(scan, so->value));
	}

	heaptid = (ItemPointer) DatumGetPointer(slot_getattr(so->mslot, 2, &isnull));

	scan->currentItemData = *heaptid;
	scan->xs_ctup.t_self = *heaptid;
	return true;
}

/*
 * End a scan and release resources
 */
void
ivfflat_endscanindex(IndexScanDesc scan)
{
	IvfflatScanOpaque so = (IvfflatScanOpaque) scan->opaque;

	/* Free any temporary files */
	tuplesort_end(so->sortstate);

	MemoryContextDelete(so->tmpCtx);

	pfree(so);
	scan->opaque = NULL;
}
