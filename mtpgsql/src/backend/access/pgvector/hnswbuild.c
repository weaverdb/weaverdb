/*
 * The HNSW build happens in two phases:
 *
 * 1. In-memory phase
 *
 * In this first phase, the graph is held completely in memory. When the graph
 * is fully built, or we run out of memory reserved for the build (determined
 * by maintenance_work_mem), we materialize the graph to disk (see
 * FlushPages()), and switch to the on-disk phase.
 *
 * In a parallel build, a large contiguous chunk of shared memory is allocated
 * to hold the graph. Each worker process has its own HnswBuildState struct in
 * private memory, which contains information that doesn't change throughout
 * the build, and pointers to the shared structs in shared memory. The shared
 * memory area is mapped to a different address in each worker process, and
 * 'HnswBuildState.hnswarea' points to the beginning of the shared area in the
 * worker process's address space. All pointers used in the graph are
 * "relative pointers", stored as an offset from 'hnswarea'.
 *
 * Each element is protected by an LWLock. It must be held when reading or
 * modifying the element's neighbors or 'heaptids'.
 *
 * In a non-parallel build, the graph is held in backend-private memory. All
 * the elements are allocated in a dedicated memory context, 'graphCtx', and
 * the pointers used in the graph are regular pointers.
 *
 * 2. On-disk phase
 *
 * In the on-disk phase, the index is built by inserting each vector to the
 * index one by one, just like on INSERT. The only difference is that we don't
 * WAL-log the individual inserts. If the graph fit completely in memory and
 * was fully built in the in-memory phase, the on-disk phase is skipped.
 *
 * After we have finished building the graph, we perform one more scan through
 * the index and write all the pages to the WAL.
 */
#include "postgres.h"

#include <limits.h>
#include <pthread.h>

#include "access/amapi.h"
#include "access/genam.h"
#include "access/relscan.h"
#include "access/tupdesc.h"
#include "access/heapam.h"
#include "catalog/index.h"
#include "hnsw.h"
#include "ivfflat.h"
#include "miscadmin.h"
#include "env/env.h"
#include "env/freespace.h"
#include "pgvector_executor_port.h"
#include "pgvector_index.h"
#include "storage/bufmgr.h"
#include "storage/multithread.h"
#include "storage/sinvaladt.h"
#include "utils/datum.h"
#include "utils/memutils.h"
#include "utils/rel.h"
#include "utils/relcache.h"
#include "utils/syscache.h"

#include "varatt.h"

#define HNSW_MAX_GRAPH_MEMORY (SIZE_MAX / 2)

/*
 * Create the metapage
 */
static void
CreateMetaPage(HnswBuildState * buildstate)
{
	Relation	index = buildstate->index;
	ForkNumber	forkNum = buildstate->forkNum;
	Buffer		buf;
	Page		page;
	HnswMetaPage metap;

	buf = HnswNewBuffer(index, forkNum);
	page = BufferGetPage(buf);
	HnswInitPage(buf, page);

	/* Set metapage data */
	metap = HnswPageGetMeta(page);
	metap->magicNumber = HNSW_MAGIC_NUMBER;
	metap->version = HNSW_VERSION;
	metap->dimensions = buildstate->dimensions;
	metap->m = buildstate->m;
	metap->efConstruction = buildstate->efConstruction;
	metap->entryBlkno = InvalidBlockNumber;
	metap->entryOffno = InvalidOffsetNumber;
	metap->entryLevel = -1;
	metap->insertPage = InvalidBlockNumber;
	((PageHeader) page)->pd_lower =
		((char *) metap + sizeof(HnswMetaPageData)) - (char *) page;

	WriteBuffer(index, buf);
}

/*
 * Add a new page
 */
static void
HnswBuildAppendPage(Relation index, Buffer *buf, Page *page, ForkNumber forkNum)
{
	/* Add a new page */
	Buffer		newbuf = HnswNewBuffer(index, forkNum);

	/* Update previous page */
	HnswPageGetOpaque(*page)->nextblkno = BufferGetBlockNumber(newbuf);

	/* Commit */
	HnswWriteBuffer(index, *buf);

	/* Can take a while, so ensure we can interrupt */
	/* Needs to be called when no buffer locks are held */
	LockBuffer(index, newbuf, BUFFER_LOCK_UNLOCK);
	CHECK_FOR_INTERRUPTS();
	LockBuffer(index, newbuf, BUFFER_LOCK_EXCLUSIVE);

	/* Prepare new page */
	*buf = newbuf;
	*page = BufferGetPage(*buf);
	HnswInitPage(*buf, *page);
}

/*
 * Create graph pages
 */
static void
CreateGraphPages(HnswBuildState * buildstate)
{
	Relation	index = buildstate->index;
	ForkNumber	forkNum = buildstate->forkNum;
	Size		maxSize;
	HnswElementTuple etup;
	HnswNeighborTuple ntup;
	BlockNumber insertPage;
	HnswElement entryPoint;
	Buffer		buf;
	Page		page;
	HnswElementPtr iter = buildstate->graph->head;
	char	   *base = buildstate->hnswarea;

	/* Calculate sizes */
	maxSize = HNSW_MAX_SIZE;

	/* Allocate once */
	etup = palloc0(HNSW_TUPLE_ALLOC_SIZE);
	ntup = palloc0(HNSW_TUPLE_ALLOC_SIZE);

	/* Prepare first page */
	buf = HnswNewBuffer(index, forkNum);
	page = BufferGetPage(buf);
	HnswInitPage(buf, page);

	while (!HnswPtrIsNull(base, iter))
	{
		HnswElement element = HnswPtrAccess(base, iter);
		Size		etupSize;
		Size		ntupSize;
		Size		combinedSize;
		Pointer		valuePtr;

		if (element == NULL)
			elog(ERROR, "hnsw CreateGraphPages: null element in graph list");

		valuePtr = HnswPtrAccess(base, element->value);
		if (valuePtr == NULL)
			elog(ERROR, "hnsw CreateGraphPages: element missing value");

		/* Update iterator */
		iter = element->next;

		/* Zero memory for each element */
		MemSet(etup, 0, HNSW_TUPLE_ALLOC_SIZE);

		/* Calculate sizes */
		etupSize = HNSW_ELEMENT_TUPLE_SIZE(VARSIZE_ANY(valuePtr));
		ntupSize = HNSW_NEIGHBOR_TUPLE_SIZE(element->level, buildstate->m);
		combinedSize = etupSize + ntupSize + sizeof(ItemIdData);

		/* Initial size check */
		if (etupSize > HNSW_TUPLE_ALLOC_SIZE)
			ereport(ERROR,
					(errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
					 errmsg("index tuple too large")));

		HnswSetElementTuple(base, etup, element);

		/* Keep element and neighbors on the same page if possible */
		if (PageGetFreeSpace(page) < etupSize || (combinedSize <= maxSize && PageGetFreeSpace(page) < combinedSize))
			HnswBuildAppendPage(index, &buf, &page, forkNum);

		/* Calculate offsets */
		element->blkno = BufferGetBlockNumber(buf);
		element->offno = OffsetNumberNext(PageGetMaxOffsetNumber(page));
		if (combinedSize <= maxSize)
		{
			element->neighborPage = element->blkno;
			element->neighborOffno = OffsetNumberNext(element->offno);
		}
		else
		{
			element->neighborPage = element->blkno + 1;
			element->neighborOffno = FirstOffsetNumber;
		}

		ItemPointerSet(&etup->neighbortid, element->neighborPage, element->neighborOffno);

		/* Add element */
		if (PageAddItem(page, (Item) etup, etupSize, InvalidOffsetNumber, LP_USED) != element->offno)
			elog(ERROR, "failed to add index item to \"%s\"", RelationGetRelationName(index));

		/* Add new page if needed */
		if (PageGetFreeSpace(page) < ntupSize)
			HnswBuildAppendPage(index, &buf, &page, forkNum);

		/* Add placeholder for neighbors */
		if (PageAddItem(page, (Item) ntup, ntupSize, InvalidOffsetNumber, LP_USED) != element->neighborOffno)
			elog(ERROR, "failed to add index item to \"%s\"", RelationGetRelationName(index));
	}

	insertPage = BufferGetBlockNumber(buf);

	/* Commit */
	HnswWriteBuffer(index, buf);

	entryPoint = HnswPtrAccess(base, buildstate->graph->entryPoint);
	HnswUpdateMetaPage(index, HNSW_UPDATE_ENTRY_ALWAYS, entryPoint, insertPage, forkNum, true);

	pfree(etup);
	pfree(ntup);
}

/*
 * Write neighbor tuples
 */
static void
WriteNeighborTuples(HnswBuildState * buildstate)
{
	Relation	index = buildstate->index;
	ForkNumber	forkNum = buildstate->forkNum;
	int			m = buildstate->m;
	HnswElementPtr iter = buildstate->graph->head;
	char	   *base = buildstate->hnswarea;
	HnswNeighborTuple ntup;

	/* Allocate once */
	ntup = palloc0(HNSW_TUPLE_ALLOC_SIZE);

	while (!HnswPtrIsNull(base, iter))
	{
		HnswElement element = HnswPtrAccess(base, iter);
		Buffer		buf;
		Page		page;
		Size		ntupSize = HNSW_NEIGHBOR_TUPLE_SIZE(element->level, m);

		/* Update iterator */
		iter = element->next;

		/* Zero memory for each element */
		MemSet(ntup, 0, HNSW_TUPLE_ALLOC_SIZE);

		/* Can take a while, so ensure we can interrupt */
		/* Needs to be called when no buffer locks are held */
		CHECK_FOR_INTERRUPTS();

		buf = ReadBufferExtended(index, forkNum, element->neighborPage, RBM_NORMAL, NULL);
		LockBuffer(index, buf, BUFFER_LOCK_EXCLUSIVE);
		page = BufferGetPage(buf);

		HnswSetNeighborTuple(base, ntup, element, m);

		if (!PageIndexTupleOverwrite(page, element->neighborOffno, (Item) ntup, ntupSize))
			elog(ERROR, "failed to add index item to \"%s\"", RelationGetRelationName(index));

		/* Commit */
		HnswWriteBuffer(index, buf);
	}

	pfree(ntup);
}

/*
 * Flush pages
 */
static void
FlushPages(HnswBuildState * buildstate)
{
#ifdef HNSW_MEMORY
	elog(INFO, "memory: %zu MB", buildstate->graph->memoryUsed / (1024 * 1024));
#endif

	CreateMetaPage(buildstate);
	CreateGraphPages(buildstate);
	WriteNeighborTuples(buildstate);

	buildstate->graph->flushed = true;
	MemoryContextReset(buildstate->graphCtx);
}

/*
 * Add a heap TID to an existing element
 */
static bool
AddDuplicateInMemory(HnswElement element, HnswElement dup)
{
	LWLockAcquire(&dup->lock, LW_EXCLUSIVE);

	if (dup->heaptidsLength == HNSW_HEAPTIDS)
	{
		LWLockRelease(&dup->lock);
		return false;
	}

	HnswAddHeapTid(dup, &element->heaptids[0]);

	LWLockRelease(&dup->lock);

	return true;
}

/*
 * Find duplicate element
 */
static bool
FindDuplicateInMemory(char *base, HnswElement element)
{
	HnswNeighborArray *neighbors = HnswGetNeighbors(base, element, 0);
	Datum		value = HnswGetValue(base, element);

	for (int i = 0; i < neighbors->length; i++)
	{
		HnswCandidate *neighbor = &neighbors->items[i];
		HnswElement neighborElement = HnswPtrAccess(base, neighbor->element);
		Datum		neighborValue = HnswGetValue(base, neighborElement);

		/* Exit early since ordered by distance */
		if (!datumIsEqual(value, neighborValue, (Oid) 0, false, (Size) -1))
			return false;

		/* Check for space */
		if (AddDuplicateInMemory(element, neighborElement))
			return true;
	}

	return false;
}

/*
 * Add to element list
 */
static void
AddElementInMemory(char *base, HnswGraph * graph, HnswElement element)
{
	SpinLockAcquire(&graph->lock);
	element->next = graph->head;
	HnswPtrStore(base, graph->head, element);
	SpinLockRelease(&graph->lock);
}

/*
 * Update neighbors
 */
static void
UpdateNeighborsInMemory(char *base, HnswSupport * support, HnswElement e, int m)
{
	for (int lc = e->level; lc >= 0; lc--)
	{
		int			lm = HnswGetLayerM(m, lc);
		Size		neighborsSize = HNSW_NEIGHBOR_ARRAY_SIZE(lm);
		HnswNeighborArray *neighbors = palloc(neighborsSize);

		/* Copy neighbors to local memory */
		LWLockAcquire(&e->lock, LW_SHARED);
		memcpy(neighbors, HnswGetNeighbors(base, e, lc), neighborsSize);
		LWLockRelease(&e->lock);

		for (int i = 0; i < neighbors->length; i++)
		{
			HnswCandidate *hc = &neighbors->items[i];
			HnswElement neighborElement = HnswPtrAccess(base, hc->element);

			/* Keep scan-build happy on Mac x86-64 */
			Assert(neighborElement);

			LWLockAcquire(&neighborElement->lock, LW_EXCLUSIVE);
			HnswUpdateConnection(base, HnswGetNeighbors(base, neighborElement, lc), e, hc->distance, lm, NULL, NULL, support);
			LWLockRelease(&neighborElement->lock);
		}
	}
}

/*
 * Update graph in memory
 */
static void
UpdateGraphInMemory(HnswSupport * support, HnswElement element, int m, HnswElement entryPoint, HnswBuildState * buildstate)
{
	HnswGraph  *graph = buildstate->graph;
	char	   *base = buildstate->hnswarea;

	/* Look for duplicate */
	if (FindDuplicateInMemory(base, element))
		return;

	/* Add element */
	AddElementInMemory(base, graph, element);

	/* Update neighbors */
	UpdateNeighborsInMemory(base, support, element, m);

	/* Update entry point if needed (already have lock) */
	if (entryPoint == NULL || element->level > entryPoint->level)
		HnswPtrStore(base, graph->entryPoint, element);
}

/*
 * Insert tuple in memory
 */
static void
InsertTupleInMemory(HnswBuildState * buildstate, HnswElement element)
{
	HnswGraph  *graph = buildstate->graph;
	HnswSupport *support = &buildstate->support;
	HnswElement entryPoint;
	LWLock	   *entryLock = &graph->entryLock;
	LWLock	   *entryWaitLock = &graph->entryWaitLock;
	int			efConstruction = buildstate->efConstruction;
	int			m = buildstate->m;
	char	   *base = buildstate->hnswarea;

	/* Wait if another process needs exclusive lock on entry lock */
	LWLockAcquire(entryWaitLock, LW_EXCLUSIVE);
	LWLockRelease(entryWaitLock);

	/* Get entry point */
	LWLockAcquire(entryLock, LW_SHARED);
	entryPoint = HnswPtrAccess(base, graph->entryPoint);

	/* Prevent concurrent inserts when likely updating entry point */
	if (entryPoint == NULL || element->level > entryPoint->level)
	{
		/* Release shared lock */
		LWLockRelease(entryLock);

		/* Tell other processes to wait and get exclusive lock */
		LWLockAcquire(entryWaitLock, LW_EXCLUSIVE);
		LWLockAcquire(entryLock, LW_EXCLUSIVE);
		LWLockRelease(entryWaitLock);

		/* Get latest entry point after lock is acquired */
		entryPoint = HnswPtrAccess(base, graph->entryPoint);
	}

	/* Find neighbors for element */
	HnswFindElementNeighbors(base, element, entryPoint, NULL, support, m, efConstruction, false);

	/* Update graph in memory */
	UpdateGraphInMemory(support, element, m, entryPoint, buildstate);

	/* Release entry lock */
	LWLockRelease(entryLock);
}

/*
 * Insert tuple
 */
static bool
InsertTuple(Relation index, Datum *values, bool *isnull, ItemPointer heaptid, HnswBuildState * buildstate)
{
	HnswGraph  *graph = buildstate->graph;
	HnswElement element;
	HnswAllocator *allocator = &buildstate->allocator;
	HnswSupport *support = &buildstate->support;
	Size		valueSize;
	Pointer		valuePtr;
	LWLock	   *flushLock = &graph->flushLock;
	char	   *base = buildstate->hnswarea;
	Datum		value;
	Size		memoryMargin;

	/* Form index value */
	if (!HnswFormIndexValue(&value, values, isnull, buildstate->typeInfo, support))
		return false;

	/* Get datum size */
	valueSize = VARSIZE_ANY(DatumGetPointer(value));

	/*
	 * Parallel builds reserve headroom so HnswSharedMemoryAlloc never races
	 * past memoryTotal. Cap the margin below memoryTotal — a fixed 1MB margin
	 * with a small SortMem budget forces an immediate flush (and unsafe
	 * concurrent on-disk inserts).
	 */
	if (base == NULL)
		memoryMargin = 0;
	else
	{
		memoryMargin = 1024 * 1024;
		if (graph->memoryTotal > 0 && memoryMargin > graph->memoryTotal / 8)
			memoryMargin = graph->memoryTotal / 8;
	}

	/* Ensure graph not flushed when inserting */
	LWLockAcquire(flushLock, LW_SHARED);

	/* Are we in the on-disk phase? */
	if (graph->flushed)
	{
		LWLockRelease(flushLock);
		/* Serialize on-disk inserts: shared index Relation is not MT-safe. */
		LWLockAcquire(flushLock, LW_EXCLUSIVE);
		if (!HnswInsertTupleOnDisk(index, support, value, heaptid, true))
		{
			LWLockRelease(flushLock);
			return false;
		}
		LWLockRelease(flushLock);
		return true;
	}

	/*
	 * In a parallel build, the HnswElement is allocated from the shared
	 * memory area, so we need to coordinate with other processes.
	 */
	LWLockAcquire(&graph->allocatorLock, LW_EXCLUSIVE);

	/*
	 * Check that we have enough memory available for the new element now that
	 * we have the allocator lock, and flush pages if needed.
	 */
		if (graph->memoryUsed + memoryMargin >= graph->memoryTotal)
	{
		LWLockRelease(&graph->allocatorLock);

		LWLockRelease(flushLock);
		LWLockAcquire(flushLock, LW_EXCLUSIVE);

		if (!graph->flushed)
		{
			ereport(NOTICE,
					(errmsg("hnsw graph no longer fits into maintenance_work_mem after " INT64_FORMAT " tuples", (int64) graph->indtuples),
					 errdetail("Building will take significantly more time."),
					 errhint("Increase maintenance_work_mem to speed up builds.")));

			FlushPages(buildstate);
		}

		/* Stay exclusive for on-disk insert (shared Relation / buffers). */
		if (!HnswInsertTupleOnDisk(index, support, value, heaptid, true))
		{
			LWLockRelease(flushLock);
			return false;
		}
		LWLockRelease(flushLock);
		return true;
	}

	/* Ok, we can proceed to allocate the element */
	element = HnswInitElement(base, heaptid, buildstate->m, buildstate->ml, buildstate->maxLevel, allocator);
	valuePtr = HnswAlloc(allocator, valueSize);

	/*
	 * We have now allocated the space needed for the element, so we don't
	 * need the allocator lock anymore. Release it and initialize the rest of
	 * the element.
	 */
	LWLockRelease(&graph->allocatorLock);

	/* Copy the datum */
	memcpy(valuePtr, DatumGetPointer(value), valueSize);
	HnswPtrStore(base, element->value, (char *) valuePtr);

	/* Create a lock for the element */
	LWLockInitialize(&element->lock, hnsw_lock_tranche_id);

	/* Insert tuple */
	InsertTupleInMemory(buildstate, element);

	/* Release flush lock */
	LWLockRelease(flushLock);

	return true;
}

/*
 * Callback for table_index_build_scan
 */
static void
BuildCallback(Relation index, ItemPointer tid, Datum *values,
			  bool *isnull, bool tupleIsAlive, void *state)
{
	HnswBuildState *buildstate = (HnswBuildState *) state;
	HnswGraph  *graph = buildstate->graph;
	MemoryContext oldCtx;

	CHECK_FOR_INTERRUPTS();

	/* Skip nulls */
	if (isnull[0])
		return;

	/* Use memory context */
	oldCtx = MemoryContextSwitchTo(buildstate->tmpCtx);

	/* Insert tuple */
	if (InsertTuple(index, values, isnull, tid, buildstate))
	{
		/* Update progress */
		SpinLockAcquire(&graph->lock);
		pgstat_progress_update_param(PROGRESS_CREATEIDX_TUPLES_DONE, ++graph->indtuples);
		SpinLockRelease(&graph->lock);
	}

	/* Reset memory context */
	MemoryContextSwitchTo(oldCtx);
	MemoryContextReset(buildstate->tmpCtx);
}

/*
 * Initialize the graph
 */
static void
InitGraph(HnswGraph * graph, char *base, Size memoryTotal)
{
	/* Initialize the lock tranche if needed */
	HnswInitLockTranche();

	HnswPtrStore(base, graph->head, (HnswElement) NULL);
	HnswPtrStore(base, graph->entryPoint, (HnswElement) NULL);
	graph->memoryUsed = 0;
	graph->memoryTotal = Min(memoryTotal, HNSW_MAX_GRAPH_MEMORY);
	graph->flushed = false;
	graph->indtuples = 0;
	SpinLockInit(&graph->lock);
	LWLockInitialize(&graph->entryLock, hnsw_lock_tranche_id);
	LWLockInitialize(&graph->entryWaitLock, hnsw_lock_tranche_id);
	LWLockInitialize(&graph->allocatorLock, hnsw_lock_tranche_id);
	LWLockInitialize(&graph->flushLock, hnsw_lock_tranche_id);
}

/*
 * Initialize an allocator
 */
static void
InitAllocator(HnswAllocator * allocator, void *(*alloc) (Size size, void *state), void *state)
{
	allocator->alloc = alloc;
	allocator->state = state;
}

/*
 * Memory context allocator
 */
static void *
HnswMemoryContextAlloc(Size size, void *state)
{
	HnswBuildState *buildstate = (HnswBuildState *) state;
	void	   *chunk = MemoryContextAlloc(buildstate->graphCtx, size);

	buildstate->graphData.memoryUsed += MAXALIGN(size);

	return chunk;
}

/*
 * Shared memory allocator
 */
static void *
HnswSharedMemoryAlloc(Size size, void *state)
{
	HnswBuildState *buildstate = (HnswBuildState *) state;
	Size		alignedSize = MAXALIGN(size);
	void	   *chunk;

	if (alignedSize > 1024 * 1024)
		elog(ERROR, "hnsw allocation too large");

	if (buildstate->graph->memoryUsed + alignedSize > buildstate->graph->memoryTotal)
		elog(ERROR, "hnsw allocator out of memory");

	chunk = buildstate->hnswarea + buildstate->graph->memoryUsed;
	buildstate->graph->memoryUsed += alignedSize;
	return chunk;
}

/*
 * Initialize the build state
 */
static void
InitBuildState(HnswBuildState * buildstate, Relation heap, Relation index, PgvectorIndexInfo *indexInfo, ForkNumber forkNum)
{
	buildstate->heap = heap;
	buildstate->index = index;
	buildstate->indexInfo = indexInfo;
	buildstate->forkNum = forkNum;
	buildstate->typeInfo = HnswGetTypeInfo(index);

	buildstate->m = HnswGetM(index);
	buildstate->efConstruction = HnswGetEfConstruction(index);
	buildstate->dimensions = TupleDescAttr(index->rd_att, 0)->atttypmod;

	if (buildstate->dimensions < 0)
	{
		TupleDesc	inddesc = RelationGetDescr(index);
		AttrNumber	heapatt;

		if (indexInfo != NULL && indexInfo->ii_KeyAttributeNumbers != NULL)
			heapatt = indexInfo->ii_KeyAttributeNumbers[0];
		else
			heapatt = TupleDescAttr(inddesc, 0)->attnum;

		if (indexInfo != NULL && indexInfo->ii_FuncIndexInfo != NULL)
			buildstate->dimensions = IvfflatInferIndexDimensionsEx(heap, heapatt,
																   indexInfo->ii_KeyAttributeNumbers,
																   indexInfo->ii_FuncIndexInfo);
		else
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
				 errmsg("column cannot have more than %d dimensions for hnsw index", buildstate->typeInfo->maxDimensions)));

	if (buildstate->efConstruction < 2 * buildstate->m)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("ef_construction must be greater than or equal to 2 * m")));

	buildstate->reltuples = 0;
	buildstate->indtuples = 0;

	/* Get support functions */
	HnswInitSupport(&buildstate->support, index);

	InitGraph(&buildstate->graphData, NULL, (Size) Max(maintenance_work_mem, 8192) * 1024L);
	buildstate->graph = &buildstate->graphData;
	buildstate->ml = HnswGetMl(buildstate->m);
	buildstate->maxLevel = HnswGetMaxLevel(buildstate->m);

	buildstate->graphCtx = GenerationContextCreate(CurrentMemoryContext,
												   "Hnsw build graph context",
												   1024 * 1024);
	buildstate->tmpCtx = AllocSetContextCreate(CurrentMemoryContext,
											   "Hnsw build temporary context",
											   ALLOCSET_DEFAULT_SIZES);

	InitAllocator(&buildstate->allocator, &HnswMemoryContextAlloc, buildstate);

	buildstate->hnswarea = NULL;
	buildstate->weaverParallel = NULL;
}

struct HnswWeaverParallelShared
{
	pthread_mutex_t mutex;
	double		reltuples;
	HnswGraph	graphData;
	char	   *hnswarea;
	Size		hnswarea_size;
	Oid			heapOid;
	Oid			indexOid;
};

typedef struct HnswBuildWorkerArg
{
	Env						*env;		/* CreateEnv(parent) on leader */
	HnswBuildState			*leader;
	HnswWeaverParallelShared *shared;
	BlockNumber				start_block;
	BlockNumber				num_blocks;
	bool					ok;
} HnswBuildWorkerArg;

/*
 * Free resources
 */
static void
FreeBuildState(HnswBuildState * buildstate)
{
	if (buildstate->weaverParallel != NULL)
	{
		pfree(buildstate->weaverParallel->hnswarea);
		pthread_mutex_destroy(&buildstate->weaverParallel->mutex);
		pfree(buildstate->weaverParallel);
		buildstate->weaverParallel = NULL;
		buildstate->hnswarea = NULL;
		buildstate->graph = &buildstate->graphData;
	}

	MemoryContextDelete(buildstate->graphCtx);
	MemoryContextDelete(buildstate->tmpCtx);
}

static Size
HnswSharedGraphBytes(void)
{
	Size		est;
	int			memkb = Max(maintenance_work_mem, 8192);

	est = (Size) memkb * 1024L;
	if (est > 3 * 1024 * 1024)
		est -= 3 * 1024 * 1024;
	return Min(est, HNSW_MAX_GRAPH_MEMORY);
}

static void
HnswAttachWeaverParallelGraph(HnswBuildState *buildstate, HnswWeaverParallelShared *shared)
{
	buildstate->weaverParallel = shared;
	buildstate->graph = &shared->graphData;
	buildstate->hnswarea = shared->hnswarea;
	InitAllocator(&buildstate->allocator, HnswSharedMemoryAlloc, buildstate);
}

static void
InitWorkerHnswBuildState(HnswBuildState *wstate, HnswBuildState *leader,
						 Relation heap, Relation index, PgvectorIndexInfo *indexInfo,
						 HnswWeaverParallelShared *shared)
{
	/*
	 * Do not call InitBuildState here: with typmod -1 it scans the heap via
	 * IvfflatInferIndexDimensions, and concurrent LockBuffer on the shared
	 * leader Relation hits ExceptionalCondition.
	 */
	MemSet(wstate, 0, sizeof(*wstate));
	wstate->heap = heap;
	wstate->index = index;
	wstate->indexInfo = indexInfo;
	wstate->forkNum = leader->forkNum;
	wstate->typeInfo = leader->typeInfo;
	wstate->m = leader->m;
	wstate->efConstruction = leader->efConstruction;
	wstate->dimensions = leader->dimensions;
	wstate->ml = leader->ml;
	wstate->maxLevel = leader->maxLevel;
	wstate->reltuples = 0;
	wstate->indtuples = 0;

	HnswInitSupport(&wstate->support, index);

	wstate->graphCtx = GenerationContextCreate(CurrentMemoryContext,
											   "Hnsw worker graph context",
											   1024 * 1024);
	wstate->tmpCtx = AllocSetContextCreate(CurrentMemoryContext,
										   "Hnsw worker temporary context",
										   ALLOCSET_DEFAULT_SIZES);

	HnswAttachWeaverParallelGraph(wstate, shared);
}

static int
HnswChooseWorkerCount(HnswBuildState *buildstate)
{
	int			want;
	BlockNumber nblocks;

	want = hnsw_build_workers;
	if (want <= 1 || buildstate->heap == NULL || GetEnv() == NULL)
		return 0;

	nblocks = RelationGetNumberOfBlocks(buildstate->heap);
	if (nblocks < (BlockNumber) (want * HNSW_MIN_BLOCKS_PER_WORKER))
		return 0;
	if (want > HNSW_MAX_BUILD_WORKERS)
		want = HNSW_MAX_BUILD_WORKERS;
	return want;
}

static void *
HnswBuildWorkerMain(void *argp)
{
	HnswBuildWorkerArg	   *arg = (HnswBuildWorkerArg *) argp;
	Env					   *env = arg->env;
	HnswBuildState			wstate;
	double					reltuples = 0;
	bool					inited_thread = false;

	arg->ok = false;

	if (env == NULL)
		return NULL;

	/*
	 * DOL-style: Env was CreateEnv(parent) on the leader. This thread only
	 * binds it; DestroyEnv stays on the leader after join.
	 */
	SetEnv(env);
	MemoryContextInit();
	SetProcessingMode(InitProcessing);
	InitThread(NORMAL_THREAD);
	inited_thread = true;
	if (!CallableInitInvalidationState())
		goto worker_failed;
	RelationInitialize();
	InitCatalogCache();
	SetProcessingMode(NormalProcessing);

	/*
	 * Catch elog(ERROR) inside this worker. Without setjmp, ERROR longjmps to
	 * the leader's PostgresMain frame (wrong thread) and hangs pthread_join.
	 */
	if (setjmp(env->errorContext) != 0)
		goto worker_failed;

	/*
	 * The index being built is not visible via worker catalog caches (same
	 * xact as the leader). Reuse the leader's already-open Relations.
	 */
	InitWorkerHnswBuildState(&wstate, arg->leader,
							 arg->leader->heap, arg->leader->index,
							 arg->leader->indexInfo, arg->shared);

	reltuples = table_index_build_range_scan(arg->leader->heap, arg->leader->index,
											 arg->leader->indexInfo,
											 true, true, false,
											 arg->start_block, arg->num_blocks,
											 BuildCallback, (void *) &wstate, NULL);

	pthread_mutex_lock(&arg->shared->mutex);
	arg->shared->reltuples += reltuples;
	pthread_mutex_unlock(&arg->shared->mutex);

	MemoryContextDelete(wstate.tmpCtx);
	MemoryContextDelete(wstate.graphCtx);
	arg->ok = true;

worker_failed:
	if (inited_thread)
		DestroyThread();
	SetEnv(NULL);
	/* Leader DestroyEnv(arg->env) after join. */
	return NULL;
}

static void
BuildGraphParallel(HnswBuildState *buildstate, int nworkers)
{
	BlockNumber total_blocks;
	BlockNumber blocks_per_worker;
	BlockNumber extra;
	BlockNumber blk;
	pthread_t  *threads;
	HnswBuildWorkerArg *args;
	HnswWeaverParallelShared *shared;
	Env		   *parent_env;
	Size		hnswarea_size;
	int			i;
	int			nstarted = 0;
	bool		ok = true;

	parent_env = GetEnv();
	total_blocks = RelationGetNumberOfBlocks(buildstate->heap);
	blocks_per_worker = total_blocks / nworkers;
	extra = total_blocks % nworkers;
	hnswarea_size = HnswSharedGraphBytes();

	shared = (HnswWeaverParallelShared *) palloc0(sizeof(HnswWeaverParallelShared));
	pthread_mutex_init(&shared->mutex, NULL);
	shared->hnswarea_size = hnswarea_size;
	shared->hnswarea = (char *) palloc(hnswarea_size);
	shared->heapOid = RelationGetRelid(buildstate->heap);
	shared->indexOid = RelationGetRelid(buildstate->index);
	InitGraph(&shared->graphData, shared->hnswarea, hnswarea_size);
	shared->graphData.memoryUsed += MAXALIGN(1);

	HnswAttachWeaverParallelGraph(buildstate, shared);

	threads = palloc(sizeof(pthread_t) * nworkers);
	args = palloc0(sizeof(HnswBuildWorkerArg) * nworkers);

	blk = 0;
	for (i = 0; i < nworkers; i++)
	{
		BlockNumber nblocks = blocks_per_worker + (i < (int) extra ? 1 : 0);

		args[i].env = pgvector_worker_create_env(parent_env);
		if (args[i].env == NULL)
		{
			ok = false;
			break;
		}
		args[i].leader = buildstate;
		args[i].shared = shared;
		args[i].start_block = blk;
		args[i].num_blocks = nblocks;
		blk += nblocks;

		if (pthread_create(&threads[i], NULL, HnswBuildWorkerMain, &args[i]) != 0)
		{
			ok = false;
			args[i].ok = false;
			break;
		}
		nstarted++;
	}

	for (i = 0; i < nstarted; i++)
		pthread_join(threads[i], NULL);

	for (i = 0; i < nworkers; i++)
	{
		if (i < nstarted && !args[i].ok)
			ok = false;
		/* Leader-only DestroyEnv — safe parent context unlink. */
		if (args[i].env != NULL)
		{
			DestroyEnv(args[i].env);
			args[i].env = NULL;
		}
	}

	if (!ok)
		ereport(ERROR,
				(errmsg("parallel hnsw build failed"),
				 errhint("SET hnsw.build_workers = 1 for a serial build.")));

	buildstate->reltuples = shared->reltuples;

	ereport(NOTICE, (errmsg("hnsw parallel build: %d workers, %.0f index tuples",
							 nworkers, buildstate->graph->indtuples)));
}

static void
BuildGraphSerial(HnswBuildState * buildstate)
{
	buildstate->reltuples = table_index_build_scan(buildstate->heap, buildstate->index,
												   buildstate->indexInfo,
												   true, true, BuildCallback,
												   (void *) buildstate, NULL);
}

/*
 * Build graph
 */
static void
BuildGraph(HnswBuildState * buildstate)
{
	int			nworkers;

	pgstat_progress_update_param(PROGRESS_CREATEIDX_SUBPHASE, PROGRESS_HNSW_PHASE_LOAD);

	if (buildstate->heap != NULL)
	{
		nworkers = HnswChooseWorkerCount(buildstate);
		if (nworkers > 1)
			BuildGraphParallel(buildstate, nworkers);
		else
			BuildGraphSerial(buildstate);
		buildstate->indtuples = buildstate->graph->indtuples;
	}

	if (!buildstate->graph->flushed)
		FlushPages(buildstate);
}

/*
 * Build the index
 */
static void
BuildIndex(Relation heap, Relation index, PgvectorIndexInfo *indexInfo,
		   HnswBuildState * buildstate, ForkNumber forkNum)
{
#ifdef HNSW_MEMORY
	SeedRandom(42);
#endif

	InitBuildState(buildstate, heap, index, indexInfo, forkNum);

	BuildGraph(buildstate);

	if (forkNum == INIT_FORKNUM)
		log_newpage_range(index, forkNum, 0, RelationGetNumberOfBlocksInFork(index, forkNum), true);

	FreeBuildState(buildstate);
}

/*
 * Build the index for a logged table
 */
IndexBuildResult *
hnsw_buildindex(Relation heap, Relation index, PgvectorIndexInfo *indexInfo)
{
	IndexBuildResult *result;
	HnswBuildState buildstate;

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
hnsw_buildemptyindex(Relation index)
{
	PgvectorIndexInfo  *indexInfo = BuildIndexInfo(index);
	HnswBuildState buildstate;

	BuildIndex(NULL, index, indexInfo, &buildstate, INIT_FORKNUM);
}
