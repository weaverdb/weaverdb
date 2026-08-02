/*
 * PG7 executor / tuplesort / index-build adapters for pgvector ivfflat.
 */

#include "postgres.h"

#include "access/heapam.h"
#include "access/skey.h"
#include "catalog/index.h"
#include "env/env.h"
#include "env/freespace.h"
#include "fmgr.h"
#include "pgvector_executor_port.h"
#include "storage/bufmgr.h"
#include "storage/bufpage.h"
#include "utils/lsyscache.h"

Env *
pgvector_worker_create_env(Env *parent)
{
	Env		   *env;

	if (parent == NULL)
		return NULL;

	/*
	 * Must run on the leader thread: CreateEnv(parent) → AllocSetContextCreate
	 * → MemoryContextSwitchTo requires GetEnv(). Parent link gives workers
	 * CheckForCancel() visibility of WCancel on the leader.
	 */
	env = CreateEnv(parent);
	if (env == NULL)
		return NULL;

	env->DatabaseId = parent->DatabaseId;
	env->DatabaseName = parent->DatabaseName;
	env->DatabasePath = parent->DatabasePath;
	env->UserName = parent->UserName;
	env->UserId = parent->UserId;
	return env;
}

#undef tuplesort_begin_heap
#undef tuplesort_puttupleslot
#undef tuplesort_gettupleslot
#undef tuplesort_reset
#undef tuplesort_performsort
#undef tuplesort_end

extern Tuplesortstate *tuplesort_begin_heap(TupleDesc tupDesc, int nkeys,
											ScanKey keys, bool randomAccess);
extern void tuplesort_puttuple(Tuplesortstate *state, void *tuple);
extern void *tuplesort_gettuple(Tuplesortstate *state, bool forward, bool *should_free);
extern void tuplesort_reset(Tuplesortstate *state);
extern void tuplesort_performsort(Tuplesortstate *state);
extern void tuplesort_end(Tuplesortstate *state);

#define tuplesort_getheaptuple(state, forward, should_free) \
	((HeapTuple) tuplesort_gettuple((state), (forward), (should_free)))

static PgvectorSlot *
pgvector_slot_ptr(TupleTableSlot *slot)
{
	return (PgvectorSlot *) slot;
}

TupleTableSlot *
pgvector_make_slot(TupleDesc tupdesc)
{
	PgvectorSlot *s;

	s = (PgvectorSlot *) palloc0(sizeof(PgvectorSlot));
	s->tupdesc = tupdesc;
	s->natts = tupdesc->natts;
	return (TupleTableSlot *) s;
}

void
pgvector_clear_slot(TupleTableSlot *slot)
{
	PgvectorSlot *s = pgvector_slot_ptr(slot);
	int			i;

	for (i = 0; i < s->natts; i++)
	{
		s->values[i] = (Datum) 0;
		s->isnull[i] = 'n';
	}
}

void
pgvector_store_virtual_tuple(TupleTableSlot *slot)
{
	(void) slot;
}

void
pgvector_slot_set_attr(TupleTableSlot *slot, int attnum, Datum value, bool isnull)
{
	PgvectorSlot *s = pgvector_slot_ptr(slot);

	if (attnum < 1 || attnum > s->natts)
		elog(ERROR, "pgvector_slot_set_attr: bad attnum %d", attnum);
	s->values[attnum - 1] = value;
	s->isnull[attnum - 1] = isnull ? 'n' : ' ';
}

Datum
pgvector_slot_getattr(TupleTableSlot *slot, int attnum, bool *isnull)
{
	PgvectorSlot *s = pgvector_slot_ptr(slot);

	if (attnum < 1 || attnum > s->natts)
		elog(ERROR, "pgvector_slot_getattr: bad attnum %d", attnum);
	if (isnull)
		*isnull = (s->isnull[attnum - 1] == 'n');
	return s->values[attnum - 1];
}

static void
pgvector_fill_scankey(int nkeys, AttrNumber *attNums, Oid *sortOperators,
					  ScanKey scankeys)
{
	int			i;

	for (i = 0; i < nkeys; i++)
	{
		RegProcedure sortproc = get_opcode(sortOperators[i]);

		if (!RegProcedureIsValid(sortproc))
			elog(ERROR, "pgvector_fill_scankey: no procedure for sort operator %lu",
				 sortOperators[i]);

		ScanKeyEntryInitialize(&scankeys[i],
							   0,
							   attNums[i],
							   sortproc,
							   (Datum) 0);
	}
}

Tuplesortstate *
pgvector_tuplesort_begin_heap(TupleDesc tupdesc, int nkeys,
							  AttrNumber *attNums, Oid *sortOperators,
							  Oid *sortCollations, bool *nullsFirstFlags,
							  int workMem, SortCoordinate coordinate,
							  bool randomAccess)
{
	ScanKeyData *scankeys;

	(void) sortCollations;
	(void) nullsFirstFlags;
	(void) workMem;
	(void) coordinate;

	scankeys = (ScanKeyData *) palloc0(sizeof(ScanKeyData) * nkeys);
	pgvector_fill_scankey(nkeys, attNums, sortOperators, scankeys);
	return tuplesort_begin_heap(tupdesc, nkeys, scankeys, randomAccess);
}

void
pgvector_tuplesort_puttupleslot(Tuplesortstate *state, TupleTableSlot *slot)
{
	PgvectorSlot *s = pgvector_slot_ptr(slot);
	HeapTuple	tup;

	tup = heap_formtuple(s->tupdesc, s->values, s->isnull);
	tuplesort_puttuple(state, (void *) tup);
	heap_freetuple(tup);
}

bool
pgvector_tuplesort_gettupleslot(Tuplesortstate *state, bool forward, bool copy,
								TupleTableSlot *slot, Datum *abbrev)
{
	PgvectorSlot *s = pgvector_slot_ptr(slot);
	HeapTuple	tup;
	bool		shouldfree;

	(void) copy;
	(void) abbrev;

	tup = tuplesort_getheaptuple(state, forward, &shouldfree);
	if (tup == NULL)
		return false;

	for (int i = 0; i < s->natts; i++)
	{
		bool		isnull;

		s->values[i] = heap_getattr(tup, (AttrNumber) (i + 1), s->tupdesc, &isnull);
		s->isnull[i] = isnull ? 'n' : ' ';
	}
	if (shouldfree)
		heap_freetuple(tup);
	return true;
}

void
pgvector_tuplesort_reset(Tuplesortstate *state)
{
	/* Must clear for a new put/performsort batch, not rewind sorted output. */
	tuplesort_reset(state);
}

static void
pgvector_form_index_datum(PgvectorIndexInfo *indexInfo, Relation heap,
						  HeapTuple htup, Datum *values, bool *isnull)
{
	TupleDesc	heapDesc = RelationGetDescr(heap);
	char		nulls[INDEX_MAX_KEYS];
	AttrNumber	attnums[INDEX_MAX_KEYS];
	int			natts;
	int			i;
	FuncIndexInfo *finfo;

	natts = indexInfo->ii_NumIndexAttrs;
	if (natts > INDEX_MAX_KEYS)
		natts = INDEX_MAX_KEYS;

	if (indexInfo->ii_KeyAttributeNumbers != NULL)
	{
		for (i = 0; i < natts; i++)
			attnums[i] = indexInfo->ii_KeyAttributeNumbers[i];
	}
	else
	{
		for (i = 0; i < natts; i++)
			attnums[i] = (AttrNumber) (i + 1);
	}

	finfo = indexInfo->ii_FuncIndexInfo;
	FormIndexDatum(natts, attnums, htup, heapDesc, values, nulls, finfo);
	for (i = 0; i < natts; i++)
		isnull[i] = (nulls[i] == 'n');
}

static double
pgvector_heap_scan(Relation heap, Relation index, PgvectorIndexInfo *indexInfo,
				   PgvectorIndexBuildCallback callback, void *callback_state,
				   HeapScanDesc scan, bool close_scan)
{
	HeapTuple	htup;
	Datum		values[INDEX_MAX_KEYS];
	bool		isnull[INDEX_MAX_KEYS];
	double		reltuples = 0;

	while ((htup = heap_getnext(scan)) != NULL)
	{
		CHECK_FOR_INTERRUPTS();
		pgvector_form_index_datum(indexInfo, heap, htup, values, isnull);
		callback(index, &htup->t_self, values, isnull, true, callback_state);
		reltuples += 1.0;
	}

	if (close_scan)
		heap_endscan(scan);
	return reltuples;
}

static double
pgvector_heap_scan_block_range(Relation heap, Relation index,
							   PgvectorIndexInfo *indexInfo,
							   PgvectorIndexBuildCallback callback,
							   void *callback_state,
							   BlockNumber start_block, BlockNumber num_blocks)
{
	BlockNumber total_blocks;
	BlockNumber end_block;
	BlockNumber blk;
	double		reltuples = 0;

	total_blocks = RelationGetNumberOfBlocks(heap);
	if (start_block >= total_blocks || num_blocks == 0)
		return 0;

	end_block = start_block + num_blocks;
	if (end_block > total_blocks)
		end_block = total_blocks;

	for (blk = start_block; blk < end_block; blk++)
	{
		Buffer		buf;
		Page		page;
		OffsetNumber maxoff;
		OffsetNumber offnum;

		CHECK_FOR_INTERRUPTS();

		buf = ReadBuffer(heap, blk);
		LockBuffer(heap, buf, BUFFER_LOCK_SHARE);
		page = BufferGetPage(buf);
		maxoff = PageGetMaxOffsetNumber(page);

		for (offnum = FirstOffsetNumber; offnum <= maxoff; offnum = OffsetNumberNext(offnum))
		{
			HeapTupleData htup;
			ItemId		itemid;
			Datum		values[INDEX_MAX_KEYS];
			bool		isnull[INDEX_MAX_KEYS];

			itemid = PageGetItemId(page, offnum);
			if (!ItemIdIsUsed(itemid))
				continue;

			htup.t_data = (HeapTupleHeader) PageGetItem(page, itemid);
			htup.t_len = ItemIdGetLength(itemid);
			ItemPointerSet(&htup.t_self, blk, offnum);
			htup.t_datamcxt = NULL;
			htup.t_datasrc = NULL;
			htup.t_info = 0;

			pgvector_form_index_datum(indexInfo, heap, &htup, values, isnull);
			/*
			 * Match heap_getnext: drop the share lock before the callback so
			 * CHECK_FOR_INTERRUPTS / elog(ERROR) cannot leave it held. The
			 * pin keeps by-reference Datums stable for the callback.
			 */
			LockBuffer(heap, buf, BUFFER_LOCK_UNLOCK);
			callback(index, &htup.t_self, values, isnull, true, callback_state);
			reltuples += 1.0;
			LockBuffer(heap, buf, BUFFER_LOCK_SHARE);
			page = BufferGetPage(buf);
		}

		LockBuffer(heap, buf, BUFFER_LOCK_UNLOCK);
		ReleaseBuffer(heap, buf);
	}

	return reltuples;
}

double
pgvector_table_index_build_scan(Relation heap, Relation index,
								PgvectorIndexInfo *indexInfo,
								bool allow_sync, bool anyvisible,
								PgvectorIndexBuildCallback callback,
								void *callback_state, void *scan)
{
	HeapScanDesc hscan;

	(void) allow_sync;
	(void) anyvisible;

	if (scan != NULL)
		return pgvector_heap_scan(heap, index, indexInfo, callback,
								  callback_state, (HeapScanDesc) scan, false);

	hscan = heap_beginscan(heap, SnapshotAny, 0, (ScanKey) NULL);
	return pgvector_heap_scan(heap, index, indexInfo, callback,
							  callback_state, hscan, true);
}

double
pgvector_table_index_build_range_scan(Relation heap, Relation index,
									  PgvectorIndexInfo *indexInfo,
									  bool allow_sync, bool anyvisible,
									  bool progress,
									  BlockNumber start_block,
									  BlockNumber num_blocks,
									  PgvectorIndexBuildCallback callback,
									  void *callback_state, void *scan)
{
	(void) progress;
	(void) scan;

	if (num_blocks == 0)
		return 0;

	return pgvector_heap_scan_block_range(heap, index, indexInfo, callback,
										  callback_state, start_block, num_blocks);
}
