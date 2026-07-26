#ifndef IVFFLAT_H
#define IVFFLAT_H

#include "postgres.h"

#include "access/genam.h"
#include "access/parallel.h"
#include "lib/pairingheap.h"
#include "pgvector_index.h"
#include "storage/condition_variable.h"
#include "utils/sampling.h"
#include "utils/tuplesort.h"
#include "vector.h"

#ifdef IVFFLAT_BENCH
#include "portability/instr_time.h"
#endif

#define IVFFLAT_MAX_DIM 2000

/* Support functions */
#define IVFFLAT_DISTANCE_PROC 1
#define IVFFLAT_NORM_PROC 2
#define IVFFLAT_KMEANS_DISTANCE_PROC 3
#define IVFFLAT_KMEANS_NORM_PROC 4
#define IVFFLAT_TYPE_INFO_PROC 5

#define IVFFLAT_VERSION	1
#define IVFFLAT_MAGIC_NUMBER 0x14FF1A7
#define IVFFLAT_PAGE_ID	0xFF84

/* Preserved page numbers */
#define IVFFLAT_METAPAGE_BLKNO	0
#define IVFFLAT_HEAD_BLKNO		1	/* first list page */

/* IVFFlat parameters */
#define IVFFLAT_DEFAULT_LISTS	100
#define IVFFLAT_MIN_LISTS		1
#define IVFFLAT_MAX_LISTS		32768
#define IVFFLAT_DEFAULT_PROBES	1
/* In-process parallel assign (Weaver pthread workers; 1 = serial only) */
#define IVFFLAT_DEFAULT_ASSIGN_WORKERS	4
#define IVFFLAT_MAX_ASSIGN_WORKERS		8
#define IVFFLAT_MIN_BLOCKS_PER_WORKER	8

/* Build phases */
/* PROGRESS_CREATEIDX_SUBPHASE_INITIALIZE is 1 */
#define PROGRESS_IVFFLAT_PHASE_KMEANS	2
#define PROGRESS_IVFFLAT_PHASE_ASSIGN	3
#define PROGRESS_IVFFLAT_PHASE_LOAD		4

#define IVFFLAT_LIST_SIZE(size)	(offsetof(IvfflatListData, center) + size)

#define IvfflatPageGetOpaque(page)	((IvfflatPageOpaque) PageGetSpecialPointer(page))
#define IvfflatPageGetMeta(page)	((IvfflatMetaPageData *) PageGetContents(page))

#ifdef IVFFLAT_BENCH
#define IvfflatBench(name, code) \
	do { \
		instr_time	start; \
		instr_time	duration; \
		INSTR_TIME_SET_CURRENT(start); \
		(code); \
		INSTR_TIME_SET_CURRENT(duration); \
		INSTR_TIME_SUBTRACT(duration, start); \
		elog(INFO, "%s: %.3f ms", name, INSTR_TIME_GET_MILLISEC(duration)); \
	} while (0)
#else
#define IvfflatBench(name, code) (code)
#endif

#define RandomDouble() (((double) random()) / MAX_RANDOM_VALUE)
#define RandomInt() random()
#define SeedRandom(seed) srandom(seed)

/* Per-thread scan settings (via IvfflatGetEnv) */
typedef struct IvfflatGlobals
{
	int			probes;
	int			iterative_scan;
	int			max_probes;
	int			assign_workers;
} IvfflatGlobals;

IvfflatGlobals *IvfflatGetEnv(void);

#define ivfflat_probes           (IvfflatGetEnv()->probes)
#define ivfflat_iterative_scan   (IvfflatGetEnv()->iterative_scan)
#define ivfflat_max_probes       (IvfflatGetEnv()->max_probes)
#define ivfflat_assign_workers   (IvfflatGetEnv()->assign_workers)

typedef enum IvfflatIterativeScanMode
{
	IVFFLAT_ITERATIVE_SCAN_OFF,
	IVFFLAT_ITERATIVE_SCAN_RELAXED
}			IvfflatIterativeScanMode;

typedef struct VectorArrayData
{
	int			length;
	int			maxlen;
	int			dim;
	Size		itemsize;
	char	   *items;
}			VectorArrayData;

typedef VectorArrayData * VectorArray;

typedef struct ListInfo
{
	BlockNumber blkno;
	OffsetNumber offno;
}			ListInfo;

/* IVFFlat index options */
typedef struct IvfflatOptions
{
	int32		vl_len_;		/* varlena header (do not touch directly!) */
	int			lists;			/* number of lists */
}			IvfflatOptions;

typedef struct IvfflatSpool
{
	Tuplesortstate *sortstate;
	Relation	heap;
	Relation	index;
}			IvfflatSpool;

typedef struct IvfflatShared
{
	/* Immutable state */
	Oid			heaprelid;
	Oid			indexrelid;
	bool		isconcurrent;
	int			scantuplesortstates;

	/* Worker progress */
	ConditionVariable workersdonecv;

	/* Mutex for mutable state */
	slock_t		mutex;

	/* Mutable state */
	int			nparticipantsdone;
	double		reltuples;
	double		indtuples;

#ifdef IVFFLAT_KMEANS_DEBUG
	double		inertia;
#endif
}			IvfflatShared;

#define ParallelTableScanFromIvfflatShared(shared) \
	(ParallelTableScanDesc) ((char *) (shared) + BUFFERALIGN(sizeof(IvfflatShared)))

typedef struct IvfflatLeader
{
	ParallelContext *pcxt;
	int			nparticipanttuplesorts;
	IvfflatShared *ivfshared;
	Sharedsort *sharedsort;
	Snapshot	snapshot;
	char	   *ivfcenters;
}			IvfflatLeader;

typedef struct IvfflatTypeInfo
{
	int			maxDimensions;
	Datum		(*normalize) (PG_FUNCTION_ARGS);
	Size		(*itemSize) (int dimensions);
	void		(*updateCenter) (Pointer v, int dimensions, float *x);
	void		(*sumCenter) (Pointer v, float *x);
}			IvfflatTypeInfo;

typedef struct IvfflatBuildState
{
	/* Info */
	Relation	heap;
	Relation	index;
	PgvectorIndexInfo  *indexInfo;
	const		IvfflatTypeInfo *typeInfo;
	TupleDesc	tupdesc;

	/* Settings */
	int			dimensions;
	int			lists;

	/* Statistics */
	double		indtuples;
	double		reltuples;

	/* Support functions */
	FmgrInfo   *procinfo;
	FmgrInfo   *normprocinfo;
	FmgrInfo   *kmeansnormprocinfo;
	Oid			collation;

	/* Variables */
	VectorArray samples;
	VectorArray centers;
	ListInfo   *listInfo;

#ifdef IVFFLAT_KMEANS_DEBUG
	double		inertia;
	double	   *listSums;
	int		   *listCounts;
#endif

	/* Sampling */
	BlockSamplerData bs;
	ReservoirStateData rstate;
	double		samplerows;
	double		rowstoskip;

	/* Sorting */
	Tuplesortstate *sortstate;
	TupleDesc	sortdesc;
	TupleTableSlot *slot;

	/* Memory */
	MemoryContext tmpCtx;

	/* Parallel builds */
	IvfflatLeader *ivfleader;
}			IvfflatBuildState;

typedef struct IvfflatMetaPageData
{
	uint32		magicNumber;
	uint32		version;
	uint16		dimensions;
	uint16		lists;
}			IvfflatMetaPageData;

typedef IvfflatMetaPageData * IvfflatMetaPage;

typedef struct IvfflatPageOpaqueData
{
	BlockNumber nextblkno;
	uint16		unused;
	uint16		page_id;		/* for identification of IVFFlat indexes */
}			IvfflatPageOpaqueData;

typedef IvfflatPageOpaqueData * IvfflatPageOpaque;

typedef struct IvfflatListData
{
	BlockNumber startPage;
	BlockNumber insertPage;
	Vector		center;
}			IvfflatListData;

typedef IvfflatListData * IvfflatList;

typedef struct IvfflatScanList
{
	pairingheap_node ph_node;
	BlockNumber startPage;
	double		distance;
}			IvfflatScanList;

typedef struct IvfflatScanOpaqueData
{
	const		IvfflatTypeInfo *typeInfo;
	int			probes;
	int			maxProbes;
	int			dimensions;
	bool		first;
	Datum		value;
	MemoryContext tmpCtx;

	/* Sorting */
	Tuplesortstate *sortstate;
	TupleDesc	tupdesc;
	TupleTableSlot *vslot;
	TupleTableSlot *mslot;
	BufferAccessStrategy bas;

	/* Support functions */
	FmgrInfo   *procinfo;
	FmgrInfo   *normprocinfo;
	Oid			collation;
	Datum		(*distfunc) (FmgrInfo *flinfo, Oid collation, Datum arg1, Datum arg2);

	/* Lists */
	pairingheap *listQueue;
	BlockNumber *listPages;
	int			listIndex;
	IvfflatScanList *lists;

	/* PG7 scan bridge: order-by / snapshot not in IndexScanDescData */
	ScanKeyData orderByData;
	int			numberOfOrderBys;
	Snapshot	xs_snapshot;
}			IvfflatScanOpaqueData;

typedef IvfflatScanOpaqueData * IvfflatScanOpaque;

#define VECTOR_ARRAY_SIZE(_length, _size) (sizeof(VectorArrayData) + (_length) * MAXALIGN(_size))

/* Use functions instead of macros to avoid double evaluation */

static inline Pointer
VectorArrayGet(VectorArray arr, int offset)
{
	return ((char *) arr->items) + (offset * arr->itemsize);
}

static inline void
VectorArraySet(VectorArray arr, int offset, Pointer val)
{
	memcpy(VectorArrayGet(arr, offset), val, VARSIZE_ANY(val));
}

/* Methods */
VectorArray VectorArrayInit(int maxlen, int dimensions, Size itemsize);
void		VectorArrayFree(VectorArray arr);
void		IvfflatKmeans(Relation index, VectorArray samples, VectorArray centers, const IvfflatTypeInfo * typeInfo);
FmgrInfo   *IvfflatOptionalProcInfo(Relation index, uint16 procnum);
Datum		IvfflatNormValue(const IvfflatTypeInfo * typeInfo, Oid collation, Datum value);
bool		IvfflatCheckNorm(FmgrInfo *procinfo, Oid collation, Datum value);
int			IvfflatGetLists(Relation index);
int			IvfflatInferIndexDimensions(Relation heap, AttrNumber attnum);
void		IvfflatGetMetaPageInfo(Relation index, int *lists, int *dimensions);
void		IvfflatUpdateList(Relation index, ListInfo listInfo, BlockNumber insertPage, BlockNumber originalInsertPage, BlockNumber startPage, ForkNumber forkNum);
void		IvfflatCommitBuffer(Relation index, Buffer buf);
void		IvfflatAppendPage(Relation index, Buffer *buf, Page *page, ForkNumber forkNum);
Buffer		IvfflatNewBuffer(Relation index, ForkNumber forkNum);
void		IvfflatInitPage(Buffer buf, Page page);
void		IvfflatInitRegisterPage(Relation index, Buffer *buf, Page *page);
void		IvfflatInit(void);
const		IvfflatTypeInfo *IvfflatGetTypeInfo(Relation index);
PGDLLEXPORT void IvfflatParallelBuildMain(dsm_segment *seg, shm_toc *toc);

/* Index access method implementation (called from pgvector_pg7_am.c entry points) */
IndexBuildResult *ivfflat_buildindex(Relation heap, Relation index, PgvectorIndexInfo *indexInfo);
void		ivfflat_buildemptyindex(Relation index);
bool		ivfflat_insertindex(Relation index, Datum *values, bool *isnull, ItemPointer heap_tid, Relation heap, IndexUniqueCheck checkUnique,
							PgvectorIndexInfo *indexInfo);
IndexBulkDeleteResult *ivfflat_bulkdeleteindex(IndexVacuumInfo *info, IndexBulkDeleteResult *stats,
											   IndexBulkDeleteCallback callback, void *callback_state);
IndexBulkDeleteResult *ivfflat_vacuumcleanupindex(IndexVacuumInfo *info, IndexBulkDeleteResult *stats);
IndexScanDesc ivfflat_beginscanindex(Relation index, int nkeys, int norderbys);
void		ivfflat_rescanindex(IndexScanDesc scan, ScanKey keys, int nkeys, ScanKey orderbys, int norderbys);
bool		ivfflat_gettupleindex(IndexScanDesc scan, ScanDirection dir);
void		ivfflat_endscanindex(IndexScanDesc scan);

#endif
