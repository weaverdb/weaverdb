#include "postgres.h"

#include <float.h>
#include <pthread.h>

#include "access/amapi.h"
#include "access/genam.h"
#include "access/itup.h"
#include "access/relscan.h"
#include "access/tupdesc.h"
#include "access/xact.h"
#include "catalog/index.h"
#include "commands/progress.h"
#include "access/heapam.h"
#include "fmgr.h"
#include "ivfflat.h"
#include "miscadmin.h"
#include "env/env.h"
#include "env/freespace.h"
#include "pgvector_index.h"
#include "storage/multithread.h"
#include "storage/sinvaladt.h"
#include "storage/bufmgr.h"
#include "utils/memutils.h"
#include "utils/rel.h"
#include "utils/relcache.h"
#include "utils/sampling.h"
#include "utils/syscache.h"
#include "utils/snapmgr.h"

#include "varatt.h"

#include "pgstat.h"


#define PARALLEL_KEY_IVFFLAT_SHARED		UINT64CONST(0xA000000000000001)
#define PARALLEL_KEY_TUPLESORT			UINT64CONST(0xA000000000000002)
#define PARALLEL_KEY_IVFFLAT_CENTERS	UINT64CONST(0xA000000000000003)
#define PARALLEL_KEY_QUERY_TEXT			UINT64CONST(0xA000000000000004)

/*
 * Add sample
 */
static void
AddSample(Datum *values, IvfflatBuildState * buildstate)
{
	VectorArray samples = buildstate->samples;
	int			targsamples = samples->maxlen;

	/* Materialize blob-indirect heap values once */
	Datum		value = PointerGetDatum(PG_DETOAST_DATUM(values[0]));

	/*
	 * Check with KMEANS_NORM_PROC that the value can be normalized since
	 * spherical distance function expects unit vectors
	 */
	if (buildstate->kmeansnormprocinfo != NULL)
	{
		if (!IvfflatCheckNorm(buildstate->kmeansnormprocinfo, buildstate->collation, value))
			return;
	}

	if (samples->length < targsamples)
	{
		VectorArraySet(samples, samples->length, DatumGetPointer(value));
		samples->length++;
	}
	else
	{
		if (buildstate->rowstoskip < 0)
			buildstate->rowstoskip = reservoir_get_next_S(&buildstate->rstate, buildstate->samplerows, targsamples);

		if (buildstate->rowstoskip <= 0)
		{
			int			k = (int) (targsamples * sampler_random_fract());

			Assert(k >= 0 && k < targsamples);
			VectorArraySet(samples, k, DatumGetPointer(value));
		}

		buildstate->rowstoskip -= 1;
	}

	/* Increment after reservoir_get_next_S */
	buildstate->samplerows += 1;
}

/*
 * Callback for sampling
 */
static void
SampleCallback(Relation index, ItemPointer tid, Datum *values,
			   bool *isnull, bool tupleIsAlive, void *state)
{
	IvfflatBuildState *buildstate = (IvfflatBuildState *) state;
	MemoryContext oldCtx;

	/* Skip nulls */
	if (isnull[0])
		return;

	/* Use memory context since blob materialization can allocate */
	oldCtx = MemoryContextSwitchTo(buildstate->tmpCtx);

	/* Add sample */
	AddSample(values, buildstate);

	/* Reset memory context */
	MemoryContextSwitchTo(oldCtx);
	MemoryContextReset(buildstate->tmpCtx);
}

/*
 * Sample rows with same logic as ANALYZE
 */
static void
SampleRows(IvfflatBuildState * buildstate)
{
	buildstate->samplerows = 0;
	buildstate->rowstoskip = -1;

	reservoir_init_selection_state(&buildstate->rstate, buildstate->samples->maxlen);
	table_index_build_scan(buildstate->heap, buildstate->index, buildstate->indexInfo,
						   false, false, SampleCallback, (void *) buildstate, NULL);

	/* Normalize if needed */
	if (buildstate->kmeansnormprocinfo != NULL)
	{
		VectorArray samples = buildstate->samples;

		for (int i = 0; i < samples->length; i++)
		{
			Datum		value = PointerGetDatum(VectorArrayGet(samples, i));
			Datum		normValue = IvfflatNormValue(buildstate->typeInfo, buildstate->collation, value);

			VectorArraySet(samples, i, DatumGetPointer(normValue));
			pfree(DatumGetPointer(normValue));
		}
	}
}

/*
 * Add tuple to sort
 */
static void
AddTupleToSort(ItemPointer tid, Datum *values, IvfflatBuildState * buildstate)
{
	double		distance;
	double		minDistance = DBL_MAX;
	int			closestCenter = 0;
	VectorArray centers = buildstate->centers;
	TupleTableSlot *slot = buildstate->slot;

	/* Materialize blob-indirect heap values once */
	Datum		value = PointerGetDatum(PG_DETOAST_DATUM(values[0]));

	/* Normalize if needed */
	if (buildstate->normprocinfo != NULL)
	{
		if (!IvfflatCheckNorm(buildstate->normprocinfo, buildstate->collation, value))
			return;

		value = IvfflatNormValue(buildstate->typeInfo, buildstate->collation, value);
	}

	/* Find the list that minimizes the distance */
	for (int i = 0; i < centers->length; i++)
	{
		distance = DatumGetFloat8(FunctionCall2Coll(buildstate->procinfo, buildstate->collation, value, PointerGetDatum(VectorArrayGet(centers, i))));

		if (distance < minDistance)
		{
			minDistance = distance;
			closestCenter = i;
		}
	}

#ifdef IVFFLAT_KMEANS_DEBUG
	buildstate->inertia += minDistance;
	buildstate->listSums[closestCenter] += minDistance;
	buildstate->listCounts[closestCenter]++;
#endif

	/* Create a virtual tuple */
	ExecClearTuple(slot);
	pgvector_slot_set_attr(slot, 1, Int32GetDatum(closestCenter), false);
	pgvector_slot_set_attr(slot, 2, PointerGetDatum(tid), false);
	pgvector_slot_set_attr(slot, 3, value, false);
	ExecStoreVirtualTuple(slot);

	/*
	 * Add tuple to sort
	 *
	 * tuplesort_puttupleslot comment: Input data is always copied; the caller
	 * need not save it.
	 */
	tuplesort_puttupleslot(buildstate->sortstate, slot);

	buildstate->indtuples++;
}

/*
 * Callback for table_index_build_scan
 */
static void
BuildCallback(Relation index, ItemPointer tid, Datum *values,
			  bool *isnull, bool tupleIsAlive, void *state)
{
	IvfflatBuildState *buildstate = (IvfflatBuildState *) state;
	MemoryContext oldCtx;

	/* Skip nulls */
	if (isnull[0])
		return;

	/* Use memory context since blob materialization can allocate */
	oldCtx = MemoryContextSwitchTo(buildstate->tmpCtx);

	/* Add tuple to sort */
	AddTupleToSort(tid, values, buildstate);

	/* Reset memory context */
	MemoryContextSwitchTo(oldCtx);
	MemoryContextReset(buildstate->tmpCtx);
}

/*
 * Get index tuple from sort state
 */
static inline void
GetNextTuple(Tuplesortstate *sortstate, TupleDesc tupdesc, TupleTableSlot *slot, IndexTuple *itup, int *list)
{
	if (tuplesort_gettupleslot(sortstate, true, false, slot, NULL))
	{
		Datum		value;
		bool		isnull;

		*list = DatumGetInt32(slot_getattr(slot, 1, &isnull));
		value = slot_getattr(slot, 3, &isnull);

		/* Form the index tuple */
		*itup = index_form_tuple(tupdesc, &value, &isnull);
		(*itup)->t_tid = *((ItemPointer) DatumGetPointer(slot_getattr(slot, 2, &isnull)));
	}
	else
		*list = -1;
}

/*
 * Create initial entry pages
 */
static void
InsertTuples(Relation index, IvfflatBuildState * buildstate, ForkNumber forkNum)
{
	int			list;
	IndexTuple	itup = NULL;	/* silence compiler warning */
	int64		inserted = 0;

	TupleTableSlot *slot = MakeSingleTupleTableSlot(buildstate->sortdesc, &TTSOpsMinimalTuple);
	TupleDesc	tupdesc = buildstate->tupdesc;

	pgstat_progress_update_param(PROGRESS_CREATEIDX_SUBPHASE, PROGRESS_IVFFLAT_PHASE_LOAD);

	pgstat_progress_update_param(PROGRESS_CREATEIDX_TUPLES_TOTAL, buildstate->indtuples);

	GetNextTuple(buildstate->sortstate, tupdesc, slot, &itup, &list);

	for (int i = 0; i < buildstate->centers->length; i++)
	{
		Buffer		buf;
		Page		page;
		BlockNumber startPage;
		BlockNumber insertPage;

		/* Can take a while, so ensure we can interrupt */
		/* Needs to be called when no buffer locks are held */
		CHECK_FOR_INTERRUPTS();

		buf = IvfflatNewBuffer(index, forkNum);
		IvfflatInitRegisterPage(index, &buf, &page);

		startPage = BufferGetBlockNumber(buf);

		/* Get all tuples for list */
		while (list == i)
		{
			/* Check for free space */
			Size		itemsz = MAXALIGN(IndexTupleSize(itup));

			if (PageGetFreeSpace(page) < itemsz)
				IvfflatAppendPage(index, &buf, &page, forkNum);

			/* Add the item */
			if (PageAddItem(page, (Item) itup, itemsz, InvalidOffsetNumber, LP_USED) == InvalidOffsetNumber)
				elog(ERROR, "failed to add index item to \"%s\"", RelationGetRelationName(index));

			pfree(itup);

			pgstat_progress_update_param(PROGRESS_CREATEIDX_TUPLES_DONE, ++inserted);

			GetNextTuple(buildstate->sortstate, tupdesc, slot, &itup, &list);
		}

		insertPage = BufferGetBlockNumber(buf);

		IvfflatCommitBuffer(index, buf);

		/* Set the start and insert pages */
		IvfflatUpdateList(index, buildstate->listInfo[i], insertPage, InvalidBlockNumber, startPage, forkNum);
	}
}

/*
 * Initialize the build state
 */
static void
InitBuildState(IvfflatBuildState * buildstate, Relation heap, Relation index, PgvectorIndexInfo *indexInfo)
{
	buildstate->heap = heap;
	buildstate->index = index;
	buildstate->indexInfo = indexInfo;
	buildstate->typeInfo = IvfflatGetTypeInfo(index);
	buildstate->tupdesc = RelationGetDescr(index);

	buildstate->lists = IvfflatGetLists(index);
	buildstate->dimensions = TupleDescAttr(index->rd_att, 0)->atttypmod;

	if (buildstate->dimensions < 0)
	{
		AttrNumber	heapatt;

		if (indexInfo != NULL && indexInfo->ii_KeyAttributeNumbers != NULL)
			heapatt = indexInfo->ii_KeyAttributeNumbers[0];
		else
			heapatt = TupleDescAttr(buildstate->tupdesc, 0)->attnum;

		buildstate->dimensions = IvfflatInferIndexDimensions(heap, heapatt);
	}

	/* Require column to have dimensions to be indexed */
	if (buildstate->dimensions < 0)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("column does not have dimensions")));

	if (buildstate->dimensions > buildstate->typeInfo->maxDimensions)
		ereport(ERROR,
				(errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
				 errmsg("column cannot have more than %d dimensions for ivfflat index", buildstate->typeInfo->maxDimensions)));

	buildstate->reltuples = 0;
	buildstate->indtuples = 0;

	/* Get support functions */
	buildstate->procinfo = index_getprocinfo(index, 1, IVFFLAT_DISTANCE_PROC);
	buildstate->normprocinfo = IvfflatOptionalProcInfo(index, IVFFLAT_NORM_PROC);
	buildstate->kmeansnormprocinfo = IvfflatOptionalProcInfo(index, IVFFLAT_KMEANS_NORM_PROC);
	buildstate->collation = InvalidOid;

	/* Require more than one dimension for spherical k-means */
	if (buildstate->kmeansnormprocinfo != NULL && buildstate->dimensions == 1)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("dimensions must be greater than one for this opclass")));

	/* Create tuple description for sorting */
	buildstate->sortdesc = CreateTemplateTupleDesc(3);
	TupleDescInitEntry(buildstate->sortdesc, (AttrNumber) 1, "list", INT4OID, -1, 0, false);
	TupleDescInitEntry(buildstate->sortdesc, (AttrNumber) 2, "tid", TIDOID, -1, 0, false);
	TupleDescInitEntry(buildstate->sortdesc, (AttrNumber) 3, "vector", TupleDescAttr(buildstate->tupdesc, 0)->atttypid, -1, 0, false);

	buildstate->slot = MakeSingleTupleTableSlot(buildstate->sortdesc, &TTSOpsVirtual);

	buildstate->centers = VectorArrayInit(buildstate->lists, buildstate->dimensions, buildstate->typeInfo->itemSize(buildstate->dimensions));
	buildstate->listInfo = palloc(sizeof(ListInfo) * buildstate->lists);

	buildstate->tmpCtx = AllocSetContextCreate(CurrentMemoryContext,
											   "Ivfflat build temporary context",
											   ALLOCSET_DEFAULT_SIZES);

#ifdef IVFFLAT_KMEANS_DEBUG
	buildstate->inertia = 0;
	buildstate->listSums = palloc0(sizeof(double) * buildstate->lists);
	buildstate->listCounts = palloc0(sizeof(int) * buildstate->lists);
#endif

	buildstate->ivfleader = NULL;
}

/*
 * Free resources
 */
static void
FreeBuildState(IvfflatBuildState * buildstate)
{
	VectorArrayFree(buildstate->centers);
	pfree(buildstate->listInfo);

#ifdef IVFFLAT_KMEANS_DEBUG
	pfree(buildstate->listSums);
	pfree(buildstate->listCounts);
#endif

	MemoryContextDelete(buildstate->tmpCtx);
}

/*
 * Compute centers
 */
static void
ComputeCenters(IvfflatBuildState * buildstate)
{
	int			numSamples;

	pgstat_progress_update_param(PROGRESS_CREATEIDX_SUBPHASE, PROGRESS_IVFFLAT_PHASE_KMEANS);

	/* Target 50 samples per list, with at least 10000 samples */
	/* The number of samples has a large effect on index build time */
	numSamples = buildstate->lists * 50;
	if (numSamples < 10000)
		numSamples = 10000;

	/* Skip samples for unlogged table */
	if (buildstate->heap == NULL)
		numSamples = 1;

	/* Sample rows */
	/* TODO Ensure within maintenance_work_mem */
	buildstate->samples = VectorArrayInit(numSamples, buildstate->dimensions, buildstate->centers->itemsize);
	if (buildstate->heap != NULL)
	{
		IvfflatBench("sample rows", SampleRows(buildstate));

		if (buildstate->samples->length < buildstate->lists)
		{
			ereport(NOTICE,
					(errmsg("ivfflat index created with little data"),
					 errdetail("This will cause low recall."),
					 errhint("Drop the index until the table has more data.")));
		}
	}

	/* Calculate centers */
	IvfflatBench("k-means", IvfflatKmeans(buildstate->index, buildstate->samples, buildstate->centers, buildstate->typeInfo));

	/* Free samples before we allocate more memory */
	VectorArrayFree(buildstate->samples);
}

/*
 * Create the metapage
 */
static void
CreateMetaPage(Relation index, int dimensions, int lists, ForkNumber forkNum)
{
	Buffer		buf;
	Page		page;
	IvfflatMetaPage metap;

	buf = IvfflatNewBuffer(index, forkNum);
	IvfflatInitRegisterPage(index, &buf, &page);

	/* Set metapage data */
	metap = IvfflatPageGetMeta(page);
	metap->magicNumber = IVFFLAT_MAGIC_NUMBER;
	metap->version = IVFFLAT_VERSION;
	metap->dimensions = dimensions;
	metap->lists = lists;
	((PageHeader) page)->pd_lower =
		((char *) metap + sizeof(IvfflatMetaPageData)) - (char *) page;

	IvfflatCommitBuffer(index, buf);
}

/*
 * Create list pages
 */
static void
CreateListPages(Relation index, VectorArray centers, int lists,
				ForkNumber forkNum, ListInfo * *listInfo)
{
	Buffer		buf;
	Page		page;
	Size		listSize;
	IvfflatList list;

	listSize = MAXALIGN(IVFFLAT_LIST_SIZE(centers->itemsize));
	list = palloc0(listSize);

	buf = IvfflatNewBuffer(index, forkNum);
	IvfflatInitRegisterPage(index, &buf, &page);

	for (int i = 0; i < lists; i++)
	{
		OffsetNumber offno;

		/* Zero memory for each list */
		MemSet(list, 0, listSize);

		/* Load list */
		list->startPage = InvalidBlockNumber;
		list->insertPage = InvalidBlockNumber;
		memcpy(&list->center, VectorArrayGet(centers, i), VARSIZE_ANY(VectorArrayGet(centers, i)));

		/* Ensure free space */
		if (PageGetFreeSpace(page) < listSize)
			IvfflatAppendPage(index, &buf, &page, forkNum);

		/* Add the item */
		offno = PageAddItem(page, (Item) list, listSize, InvalidOffsetNumber, LP_USED);
		if (offno == InvalidOffsetNumber)
			elog(ERROR, "failed to add index item to \"%s\"", RelationGetRelationName(index));

		/* Save location info */
		(*listInfo)[i].blkno = BufferGetBlockNumber(buf);
		(*listInfo)[i].offno = offno;
	}

	IvfflatCommitBuffer(index, buf);

	pfree(list);
}

#ifdef IVFFLAT_KMEANS_DEBUG
/*
 * Print k-means metrics
 */
static void
PrintKmeansMetrics(IvfflatBuildState * buildstate)
{
	elog(INFO, "inertia: %.3e", buildstate->inertia);

	/* Calculate Davies-Bouldin index */
	if (buildstate->lists > 1 && !buildstate->ivfleader)
	{
		double		db = 0.0;

		/* Calculate average distance */
		for (int i = 0; i < buildstate->lists; i++)
		{
			if (buildstate->listCounts[i] > 0)
				buildstate->listSums[i] /= buildstate->listCounts[i];
		}

		for (int i = 0; i < buildstate->lists; i++)
		{
			double		max = 0.0;
			double		distance;

			for (int j = 0; j < buildstate->lists; j++)
			{
				if (j == i)
					continue;

				distance = DatumGetFloat8(FunctionCall2Coll(buildstate->procinfo, buildstate->collation, PointerGetDatum(VectorArrayGet(buildstate->centers, i)), PointerGetDatum(VectorArrayGet(buildstate->centers, j))));
				distance = (buildstate->listSums[i] + buildstate->listSums[j]) / distance;

				if (distance > max)
					max = distance;
			}
			db += max;
		}
		db /= buildstate->lists;
		elog(INFO, "davies-bouldin: %.3f", db);
	}
}
#endif

/*
 * Initialize build sort state
 */
static Tuplesortstate *
InitBuildSortState(TupleDesc tupdesc, int memory, SortCoordinate coordinate)
{
	AttrNumber	attNums[] = {1};
	Oid			sortOperators[] = {Int4LessOperator};
	Oid			sortCollations[] = {InvalidOid};
	bool		nullsFirstFlags[] = {false};

	return tuplesort_begin_heap(tupdesc, 1, attNums, sortOperators, sortCollations, nullsFirstFlags, memory, coordinate, false);
}

#if 0 /* PG7: parallel ivfflat build not ported */
/*
 * Within leader, wait for end of heap scan
 */
static double
ParallelHeapScan(IvfflatBuildState * buildstate)
{
	IvfflatShared *ivfshared = buildstate->ivfleader->ivfshared;
	int			nparticipanttuplesorts;
	double		reltuples;

	nparticipanttuplesorts = buildstate->ivfleader->nparticipanttuplesorts;
	for (;;)
	{
		SpinLockAcquire(&ivfshared->mutex);
		if (ivfshared->nparticipantsdone == nparticipanttuplesorts)
		{
			buildstate->indtuples = ivfshared->indtuples;
			reltuples = ivfshared->reltuples;
#ifdef IVFFLAT_KMEANS_DEBUG
			buildstate->inertia = ivfshared->inertia;
#endif
			SpinLockRelease(&ivfshared->mutex);
			break;
		}
		SpinLockRelease(&ivfshared->mutex);

		ConditionVariableSleep(&ivfshared->workersdonecv,
							   WAIT_EVENT_PARALLEL_CREATE_INDEX_SCAN);
	}

	ConditionVariableCancelSleep();

	return reltuples;
}

/*
 * Perform a worker's portion of a parallel sort
 */
static void
IvfflatParallelScanAndSort(IvfflatSpool * ivfspool, IvfflatShared * ivfshared, Sharedsort *sharedsort, char *ivfcenters, int sortmem, bool progress)
{
	SortCoordinate coordinate;
	IvfflatBuildState buildstate;
	TableScanDesc scan;
	double		reltuples;
	PgvectorIndexInfo  *indexInfo;

	/* Initialize local tuplesort coordination state */
	coordinate = palloc0(sizeof(SortCoordinateData));
	coordinate->isWorker = true;
	coordinate->nParticipants = -1;
	coordinate->sharedsort = sharedsort;

	/* Join parallel scan */
	indexInfo = BuildIndexInfo(ivfspool->index);
	false = ivfshared->isconcurrent;
	InitBuildState(&buildstate, ivfspool->heap, ivfspool->index, indexInfo);
	memcpy(buildstate.centers->items, ivfcenters, buildstate.centers->itemsize * buildstate.centers->maxlen);
	buildstate.centers->length = buildstate.centers->maxlen;
	ivfspool->sortstate = InitBuildSortState(buildstate.sortdesc, sortmem, coordinate);
	buildstate.sortstate = ivfspool->sortstate;
	scan = table_beginscan_parallel(ivfspool->heap,
									ParallelTableScanFromIvfflatShared(ivfshared)ss
		);
	reltuples = table_index_build_scan(ivfspool->heap, ivfspool->index, indexInfo,
									   true, progress, BuildCallback,
									   (void *) &buildstate, scan);

	/* Execute this worker's part of the sort */
	tuplesort_performsort(ivfspool->sortstate);

	/* Record statistics */
	SpinLockAcquire(&ivfshared->mutex);
	ivfshared->nparticipantsdone++;
	ivfshared->reltuples += reltuples;
	ivfshared->indtuples += buildstate.indtuples;
#ifdef IVFFLAT_KMEANS_DEBUG
	ivfshared->inertia += buildstate.inertia;
#endif
	SpinLockRelease(&ivfshared->mutex);

	/* Log statistics */
	if (progress)
		ereport(DEBUG1, (errmsg("leader processed " INT64_FORMAT " tuples", (int64) reltuples)));
	else
		ereport(DEBUG1, (errmsg("worker processed " INT64_FORMAT " tuples", (int64) reltuples)));

	/* Notify leader */
	ConditionVariableSignal(&ivfshared->workersdonecv);

	/* We can end tuplesorts immediately */
	tuplesort_end(ivfspool->sortstate);

	FreeBuildState(&buildstate);
}

/*
 * Perform work within a launched parallel process
 */
void
IvfflatParallelBuildMain(dsm_segment *seg, shm_toc *toc)
{
	char	   *sharedquery;
	IvfflatSpool *ivfspool;
	IvfflatShared *ivfshared;
	Sharedsort *sharedsort;
	char	   *ivfcenters;
	Relation	heapRel;
	Relation	indexRel;
	LOCKMODE	heapLockmode;
	LOCKMODE	indexLockmode;
	int			sortmem;

	/* Set debug_query_string for individual workers first */
	sharedquery = shm_toc_lookup(toc, PARALLEL_KEY_QUERY_TEXT, true);
	debug_query_string = sharedquery;

	/* Report the query string from leader */
	pgstat_report_activity(STATE_RUNNING, debug_query_string);

	/* Look up shared state */
	ivfshared = shm_toc_lookup(toc, PARALLEL_KEY_IVFFLAT_SHARED, false);

	/* Open relations using lock modes known to be obtained by index.c */
	if (!ivfshared->isconcurrent)
	{
		heapLockmode = ShareLock;
		indexLockmode = AccessExclusiveLock;
	}
	else
	{
		heapLockmode = ShareUpdateExclusiveLock;
		indexLockmode = RowExclusiveLock;
	}

	/* Open relations within worker */
	heapRel = table_open(ivfshared->heaprelid, heapLockmode);
	indexRel = index_open(ivfshared->indexrelid, indexLockmode);

	/* Initialize worker's own spool */
	ivfspool = (IvfflatSpool *) palloc0(sizeof(IvfflatSpool));
	ivfspool->heap = heapRel;
	ivfspool->index = indexRel;

	/* Look up shared state private to tuplesort.c */
	sharedsort = shm_toc_lookup(toc, PARALLEL_KEY_TUPLESORT, false);
	tuplesort_attach_shared(sharedsort, seg);

	ivfcenters = shm_toc_lookup(toc, PARALLEL_KEY_IVFFLAT_CENTERS, false);

	/* Perform sorting */
	sortmem = maintenance_work_mem / ivfshared->scantuplesortstates;
	IvfflatParallelScanAndSort(ivfspool, ivfshared, sharedsort, ivfcenters, sortmem, false);

	/* Close relations within worker */
	index_close(indexRel, indexLockmode);
	table_close(heapRel, heapLockmode);
}

/*
 * End parallel build
 */
static void
IvfflatEndParallel(IvfflatLeader * ivfleader)
{
	/* Shutdown worker processes */
	WaitForParallelWorkersToFinish(ivfleader->pcxt);

	/* Free last reference to MVCC snapshot, if one was used */
	if (IsMVCCSnapshot(ivfleader->snapshot))
		UnregisterSnapshot(ivfleader->snapshot);
	DestroyParallelContext(ivfleader->pcxt);
	ExitParallelMode();
}

/*
 * Return size of shared memory required for parallel index build
 */
static Size
ParallelEstimateShared(Relation heap, Snapshot snapshot)
{
	return add_size(BUFFERALIGN(sizeof(IvfflatShared)), table_parallelscan_estimate(heap, snapshot));
}

/*
 * Within leader, participate as a parallel worker
 */
static void
IvfflatLeaderParticipateAsWorker(IvfflatBuildState * buildstate)
{
	IvfflatLeader *ivfleader = buildstate->ivfleader;
	IvfflatSpool *leaderworker;
	int			sortmem;

	/* Allocate memory and initialize private spool */
	leaderworker = (IvfflatSpool *) palloc0(sizeof(IvfflatSpool));
	leaderworker->heap = buildstate->heap;
	leaderworker->index = buildstate->index;

	/* Perform work common to all participants */
	sortmem = maintenance_work_mem / ivfleader->nparticipanttuplesorts;
	IvfflatParallelScanAndSort(leaderworker, ivfleader->ivfshared,
							   ivfleader->sharedsort, ivfleader->ivfcenters,
							   sortmem, true);
}

/*
 * Begin parallel build
 */
static void
IvfflatBeginParallel(IvfflatBuildState * buildstate, bool isconcurrent, int request)
{
	ParallelContext *pcxt;
	int			scantuplesortstates;
	Snapshot	snapshot;
	Size		estivfshared;
	Size		estsort;
	Size		estcenters;
	IvfflatShared *ivfshared;
	Sharedsort *sharedsort;
	char	   *ivfcenters;
	IvfflatLeader *ivfleader = (IvfflatLeader *) palloc0(sizeof(IvfflatLeader));
	bool		leaderparticipates = true;
	int			querylen;

#ifdef DISABLE_LEADER_PARTICIPATION
	leaderparticipates = false;
#endif

	/* Enter parallel mode and create context */
	EnterParallelMode();
	Assert(request > 0);
	pcxt = CreateParallelContext("vector", "IvfflatParallelBuildMain", request);

	scantuplesortstates = leaderparticipates ? request + 1 : request;

	/* Get snapshot for table scan */
	if (!isconcurrent)
		snapshot = SnapshotAny;
	else
		snapshot = RegisterSnapshot(GetTransactionSnapshot());

	/* Estimate size of workspaces */
	estivfshared = ParallelEstimateShared(buildstate->heap, snapshot);
	shm_toc_estimate_chunk(&pcxt->estimator, estivfshared);
	estsort = tuplesort_estimate_shared(scantuplesortstates);
	shm_toc_estimate_chunk(&pcxt->estimator, estsort);
	estcenters = buildstate->centers->itemsize * buildstate->centers->maxlen;
	shm_toc_estimate_chunk(&pcxt->estimator, estcenters);
	shm_toc_estimate_keys(&pcxt->estimator, 3);

	/* Finally, estimate PARALLEL_KEY_QUERY_TEXT space */
	if (debug_query_string)
	{
		querylen = strlen(debug_query_string);
		shm_toc_estimate_chunk(&pcxt->estimator, querylen + 1);
		shm_toc_estimate_keys(&pcxt->estimator, 1);
	}
	else
		querylen = 0;			/* keep compiler quiet */

	/* Everyone's had a chance to ask for space, so now create the DSM */
	InitializeParallelDSM(pcxt);

	/* If no DSM segment was available, back out (do serial build) */
	if (pcxt->seg == NULL)
	{
		if (IsMVCCSnapshot(snapshot))
			UnregisterSnapshot(snapshot);
		DestroyParallelContext(pcxt);
		ExitParallelMode();
		return;
	}

	/* Store shared build state, for which we reserved space */
	ivfshared = (IvfflatShared *) shm_toc_allocate(pcxt->toc, estivfshared);
	/* Initialize immutable state */
	ivfshared->heaprelid = RelationGetRelid(buildstate->heap);
	ivfshared->indexrelid = RelationGetRelid(buildstate->index);
	ivfshared->isconcurrent = isconcurrent;
	ivfshared->scantuplesortstates = scantuplesortstates;
	ConditionVariableInit(&ivfshared->workersdonecv);
	SpinLockInit(&ivfshared->mutex);
	/* Initialize mutable state */
	ivfshared->nparticipantsdone = 0;
	ivfshared->reltuples = 0;
	ivfshared->indtuples = 0;
#ifdef IVFFLAT_KMEANS_DEBUG
	ivfshared->inertia = 0;
#endif
	table_parallelscan_initialize(buildstate->heap,
								  ParallelTableScanFromIvfflatShared(ivfshared),
								  snapshot);

	/* Store shared tuplesort-private state, for which we reserved space */
	sharedsort = (Sharedsort *) shm_toc_allocate(pcxt->toc, estsort);
	tuplesort_initialize_shared(sharedsort, scantuplesortstates,
								pcxt->seg);

	ivfcenters = shm_toc_allocate(pcxt->toc, estcenters);
	memcpy(ivfcenters, buildstate->centers->items, estcenters);

	shm_toc_insert(pcxt->toc, PARALLEL_KEY_IVFFLAT_SHARED, ivfshared);
	shm_toc_insert(pcxt->toc, PARALLEL_KEY_TUPLESORT, sharedsort);
	shm_toc_insert(pcxt->toc, PARALLEL_KEY_IVFFLAT_CENTERS, ivfcenters);

	/* Store query string for workers */
	if (debug_query_string)
	{
		char	   *sharedquery;

		sharedquery = (char *) shm_toc_allocate(pcxt->toc, querylen + 1);
		memcpy(sharedquery, debug_query_string, querylen + 1);
		shm_toc_insert(pcxt->toc, PARALLEL_KEY_QUERY_TEXT, sharedquery);
	}

	/* Launch workers, saving status for leader/caller */
	LaunchParallelWorkers(pcxt);
	ivfleader->pcxt = pcxt;
	ivfleader->nparticipanttuplesorts = pcxt->nworkers_launched;
	if (leaderparticipates)
		ivfleader->nparticipanttuplesorts++;
	ivfleader->ivfshared = ivfshared;
	ivfleader->sharedsort = sharedsort;
	ivfleader->snapshot = snapshot;
	ivfleader->ivfcenters = ivfcenters;

	/* If no workers were successfully launched, back out (do serial build) */
	if (pcxt->nworkers_launched == 0)
	{
		IvfflatEndParallel(ivfleader);
		return;
	}

	/* Log participants */
	ereport(DEBUG1, (errmsg("using %d parallel workers", pcxt->nworkers_launched)));

	/* Save leader state now that it's clear build will be parallel */
	buildstate->ivfleader = ivfleader;

	/* Join heap scan ourselves */
	if (leaderparticipates)
		IvfflatLeaderParticipateAsWorker(buildstate);

	/* Wait for all launched workers */
	WaitForParallelWorkersToAttach(pcxt);
}
#endif /* parallel ivfflat build */

typedef struct IvfflatWeaverParallelShared
{
	pthread_mutex_t mutex;
	int			nworkers;
	double		reltuples;
	double		indtuples;
	Oid			heapOid;
	Oid			indexOid;
} IvfflatWeaverParallelShared;

typedef struct IvfflatAssignWorkerArg
{
	Env				   *parent_env;
	IvfflatBuildState  *leader;
	IvfflatWeaverParallelShared *shared;
	BlockNumber			start_block;
	BlockNumber			num_blocks;
	int					sortmem;
	Tuplesortstate	   *sortstate;
	bool				ok;
} IvfflatAssignWorkerArg;

static void
InitWorkerAssignState(IvfflatBuildState *wstate, IvfflatBuildState *leader,
					  Relation heap, Relation index, PgvectorIndexInfo *indexInfo,
					  Tuplesortstate *sortstate)
{
	MemSet(wstate, 0, sizeof(*wstate));
	wstate->heap = heap;
	wstate->index = index;
	wstate->indexInfo = indexInfo;
	wstate->typeInfo = leader->typeInfo;
	wstate->tupdesc = leader->tupdesc;
	wstate->dimensions = leader->dimensions;
	wstate->lists = leader->lists;
	wstate->centers = leader->centers;
	wstate->collation = leader->collation;
	wstate->procinfo = index_getprocinfo(index, 1, IVFFLAT_DISTANCE_PROC);
	wstate->normprocinfo = IvfflatOptionalProcInfo(index, IVFFLAT_NORM_PROC);
	wstate->sortstate = sortstate;
	wstate->sortdesc = leader->sortdesc;
	wstate->slot = MakeSingleTupleTableSlot(leader->sortdesc, &TTSOpsVirtual);
	wstate->tmpCtx = AllocSetContextCreate(CurrentMemoryContext,
										   "Ivfflat parallel assign temporary context",
										   ALLOCSET_DEFAULT_SIZES);
}

static int
IvfflatChooseWorkerCount(IvfflatBuildState *buildstate)
{
	int			want;
	BlockNumber nblocks;

	want = ivfflat_assign_workers;
	if (want <= 1 || buildstate->heap == NULL || GetEnv() == NULL)
		return 0;

	nblocks = RelationGetNumberOfBlocks(buildstate->heap);
	if (nblocks < (BlockNumber) (want * IVFFLAT_MIN_BLOCKS_PER_WORKER))
		return 0;
	if (want > IVFFLAT_MAX_ASSIGN_WORKERS)
		want = IVFFLAT_MAX_ASSIGN_WORKERS;
	return want;
}

static void
IvfflatMergeWorkerSorts(IvfflatBuildState *buildstate, Tuplesortstate **workerSorts, int nworkers)
{
	TupleTableSlot *slot;

	buildstate->sortstate = InitBuildSortState(buildstate->sortdesc,
											   maintenance_work_mem, NULL);
	slot = MakeSingleTupleTableSlot(buildstate->sortdesc, &TTSOpsMinimalTuple);

	for (int i = 0; i < nworkers; i++)
	{
		while (tuplesort_gettupleslot(workerSorts[i], true, false, slot, NULL))
			tuplesort_puttupleslot(buildstate->sortstate, slot);
		tuplesort_end(workerSorts[i]);
	}
}

static void *
IvfflatAssignWorkerMain(void *argp)
{
	IvfflatAssignWorkerArg *arg = (IvfflatAssignWorkerArg *) argp;
	Env		   *env = NULL;
	Relation	heap = NULL;
	Relation	index = NULL;
	PgvectorIndexInfo *indexInfo = NULL;
	IvfflatBuildState wstate;
	double		reltuples = 0;

	arg->ok = false;

	env = CreateEnv(arg->parent_env);
	if (env == NULL)
		return NULL;

	SetEnv(env);
	MemoryContextInit();
	InitThread(NORMAL_THREAD);
	if (!CallableInitInvalidationState())
		goto worker_failed;
	RelationInitialize();
	InitCatalogCache();
	SetProcessingMode(NormalProcessing);

	heap = heap_open(arg->shared->heapOid, AccessShareLock);
	index = index_open(arg->shared->indexOid);
	indexInfo = BuildIndexInfo(index);

	InitWorkerAssignState(&wstate, arg->leader, heap, index, indexInfo, arg->sortstate);

	reltuples = table_index_build_range_scan(heap, index, indexInfo,
											 true, true, false,
											 arg->start_block, arg->num_blocks,
											 BuildCallback, (void *) &wstate, NULL);

	tuplesort_performsort(arg->sortstate);

	pthread_mutex_lock(&arg->shared->mutex);
	arg->shared->reltuples += reltuples;
	arg->shared->indtuples += wstate.indtuples;
	pthread_mutex_unlock(&arg->shared->mutex);

	MemoryContextDelete(wstate.tmpCtx);
	index_close(index);
	heap_close(heap, AccessShareLock);
	pfree(indexInfo);
	arg->ok = true;

worker_failed:
	DestroyThread();
	SetEnv(NULL);
	DestroyEnv(env);
	return NULL;
}

static void
AssignTuplesParallel(IvfflatBuildState *buildstate, int nworkers)
{
	BlockNumber total_blocks;
	BlockNumber blocks_per_worker;
	BlockNumber extra;
	BlockNumber blk;
	pthread_t  *threads;
	IvfflatAssignWorkerArg *args;
	Tuplesortstate **worker_sorts;
	IvfflatWeaverParallelShared shared;
	Env		   *parent_env;
	int			sortmem;
	int			i;
	bool		ok = true;

	pgstat_progress_update_param(PROGRESS_CREATEIDX_SUBPHASE, PROGRESS_IVFFLAT_PHASE_ASSIGN);

	parent_env = GetEnv();
	total_blocks = RelationGetNumberOfBlocks(buildstate->heap);
	blocks_per_worker = total_blocks / nworkers;
	extra = total_blocks % nworkers;

	sortmem = maintenance_work_mem / (nworkers + 1);
	if (sortmem < 32)
		sortmem = 32;

	threads = palloc(sizeof(pthread_t) * nworkers);
	args = palloc0(sizeof(IvfflatAssignWorkerArg) * nworkers);
	worker_sorts = palloc(sizeof(Tuplesortstate *) * nworkers);

	MemSet(&shared, 0, sizeof(shared));
	pthread_mutex_init(&shared.mutex, NULL);
	shared.nworkers = nworkers;
	shared.heapOid = RelationGetRelid(buildstate->heap);
	shared.indexOid = RelationGetRelid(buildstate->index);

	blk = 0;
	for (i = 0; i < nworkers; i++)
	{
		BlockNumber nblocks = blocks_per_worker + (i < (int) extra ? 1 : 0);

		args[i].parent_env = parent_env;
		args[i].leader = buildstate;
		args[i].shared = &shared;
		args[i].start_block = blk;
		args[i].num_blocks = nblocks;
		args[i].sortmem = sortmem;
		args[i].sortstate = InitBuildSortState(buildstate->sortdesc, sortmem, NULL);
		worker_sorts[i] = args[i].sortstate;
		blk += nblocks;

		if (pthread_create(&threads[i], NULL, IvfflatAssignWorkerMain, &args[i]) != 0)
		{
			ok = false;
			args[i].ok = false;
			i++;
			break;
		}
	}

	for (int j = 0; j < i; j++)
		pthread_join(threads[j], NULL);

	for (int j = 0; j < nworkers; j++)
	{
		if (j < i && !args[j].ok)
			ok = false;
		if (j >= i && worker_sorts[j] != NULL)
			tuplesort_end(worker_sorts[j]);
	}

	pthread_mutex_destroy(&shared.mutex);

	if (!ok)
	{
		for (i = 0; i < nworkers; i++)
		{
			if (worker_sorts[i] != NULL)
				tuplesort_end(worker_sorts[i]);
		}
		ereport(ERROR,
				(errmsg("parallel ivfflat assign failed"),
				 errhint("Set IvfflatGetEnv()->assign_workers to 1 for serial assign.")));
	}

	buildstate->reltuples = shared.reltuples;
	buildstate->indtuples = shared.indtuples;
	IvfflatMergeWorkerSorts(buildstate, worker_sorts, nworkers);

	ereport(DEBUG1, (errmsg("ivfflat parallel assign: %d workers, %.0f tuples",
							nworkers, buildstate->indtuples)));
}

/*
 * Scan table for tuples to index (single-threaded)
 */
static void
AssignTuplesSerial(IvfflatBuildState * buildstate)
{
	pgstat_progress_update_param(PROGRESS_CREATEIDX_SUBPHASE, PROGRESS_IVFFLAT_PHASE_ASSIGN);

	buildstate->sortstate = InitBuildSortState(buildstate->sortdesc, maintenance_work_mem, NULL);

	if (buildstate->heap != NULL)
	{
		buildstate->reltuples = table_index_build_scan(buildstate->heap, buildstate->index,
													   buildstate->indexInfo,
													   true, true, BuildCallback,
													   (void *) buildstate, NULL);
#ifdef IVFFLAT_KMEANS_DEBUG
		PrintKmeansMetrics(buildstate);
#endif
	}
}

static void
AssignTuples(IvfflatBuildState * buildstate)
{
	int			nworkers = IvfflatChooseWorkerCount(buildstate);

	if (nworkers > 1)
		AssignTuplesParallel(buildstate, nworkers);
	else
		AssignTuplesSerial(buildstate);
}

/*
 * Create entry pages
 */
static void
CreateEntryPages(IvfflatBuildState * buildstate, ForkNumber forkNum)
{
	/* Assign */
	IvfflatBench("assign tuples", AssignTuples(buildstate));

	/* Sort */
	IvfflatBench("sort tuples", tuplesort_performsort(buildstate->sortstate));

	/* Load */
	IvfflatBench("load tuples", InsertTuples(buildstate->index, buildstate, forkNum));

	/* End sort */
	tuplesort_end(buildstate->sortstate);
}

/*
 * Build the index
 */
static void
BuildIndex(Relation heap, Relation index, PgvectorIndexInfo *indexInfo,
		   IvfflatBuildState * buildstate, ForkNumber forkNum)
{
	InitBuildState(buildstate, heap, index, indexInfo);

	ComputeCenters(buildstate);

	/* Create pages */
	CreateMetaPage(index, buildstate->dimensions, buildstate->lists, forkNum);
	CreateListPages(index, buildstate->centers, buildstate->lists, forkNum, &buildstate->listInfo);
	CreateEntryPages(buildstate, forkNum);

	/* Write WAL for initialization fork since GenericXLog functions do not */
	if (forkNum == INIT_FORKNUM)
		log_newpage_range(index, forkNum, 0, RelationGetNumberOfBlocks(index), true);

	FreeBuildState(buildstate);
}

/*
 * Build the index for a logged table
 */
IndexBuildResult *
ivfflat_buildindex(Relation heap, Relation index, PgvectorIndexInfo *indexInfo)
{
	IndexBuildResult *result;
	IvfflatBuildState buildstate;

#ifdef IVFFLAT_BENCH
	SeedRandom(42);
#endif

	BuildIndex(heap, index, indexInfo, &buildstate, MAIN_FORKNUM);

	result = (IndexBuildResult *) palloc(sizeof(IndexBuildResult));
	result->heap_tuples = buildstate.reltuples;
	result->index_tuples = buildstate.indtuples;

	return result;
}

/*
 * Build the index for an unlogged table
 */
void
ivfflat_buildemptyindex(Relation index)
{
	PgvectorIndexInfo  *indexInfo = BuildIndexInfo(index);
	IvfflatBuildState buildstate;

	BuildIndex(NULL, index, indexInfo, &buildstate, INIT_FORKNUM);
}
