/*
 * PG7-style pg_am entry points for ivfflat and hnsw.
 *
 * WeaverDB registers access methods via pg_am regprocs (btbuild pattern),
 * not PostgreSQL 9.6+ amhandler / IndexAmRoutine. These wrappers adapt the
 * catalog calling convention to the pgvector index implementation.
 */

#include "postgres.h"

#include "access/amapi.h"
#include "access/funcindex.h"
#include "access/genam.h"
#include "access/istrat.h"
#include "catalog/index.h"
#include "fmgr.h"
#include "hnsw.h"
#include "ivfflat.h"
#include "pgvector_index.h"
#include "pgvector_scan.h"
#include "nodes/relation.h"
#include "utils/builtins.h"
#include "utils/rel.h"

#include "catalog/pg_am.h"

PgvectorIndexInfo *
BuildIndexInfo(Relation index)
{
	PgvectorIndexInfo  *info;
	int			natts;

	natts = RelationGetNumberOfAttributes(index);
	info = (PgvectorIndexInfo *) palloc0(sizeof(PgvectorIndexInfo));
	info->ii_NumKeyAttributes = natts;
	info->ii_NumIndexAttrs = natts;
	info->ii_Concurrent = false;
	return info;
}

static PgvectorIndexInfo *
pgvector_make_indexinfo(int natts, AttrNumber *attnum)
{
	PgvectorIndexInfo  *info;

	info = (PgvectorIndexInfo *) palloc0(sizeof(PgvectorIndexInfo));
	info->ii_NumKeyAttributes = natts;
	info->ii_NumIndexAttrs = natts;
	info->ii_KeyAttributeNumbers = attnum;
	info->ii_Concurrent = false;
	return info;
}

typedef struct IvfflatBulkDelState
{
	int			delcount;
	ItemPointerData *items;
	int			cursor;
} IvfflatBulkDelState;

static bool
ivfflat_bulkdel_callback(void *tid, void *state)
{
	IvfflatBulkDelState *st = (IvfflatBulkDelState *) state;
	ItemPointer htid = (ItemPointer) tid;
	int			i;

	for (i = 0; i < st->delcount; i++)
	{
		if (ItemPointerEquals(htid, &st->items[i]))
			return true;
	}
	return false;
}

void
ivfflatbuild(Relation heap,
			 Relation index,
			 int natts,
			 AttrNumber *attnum,
			 IndexStrategy istrat,
			 uint16 pcount,
			 Datum *params,
			 FuncIndexInfo *finfo,
			 PredInfo *predInfo)
{
	PgvectorIndexInfo  *indexInfo;
	IndexBuildResult *result;

	(void) istrat;
	(void) pcount;
	(void) params;
	(void) finfo;
	(void) predInfo;

	indexInfo = pgvector_make_indexinfo(natts, attnum);
	result = ivfflat_buildindex(heap, index, indexInfo);
	if (result != NULL)
		pfree(result);
}

InsertIndexResult
ivfflatinsert(Relation rel,
			  Datum *datum,
			  char *nulls,
			  ItemPointer ht_ctid,
			  Relation heapRel,
			  bool is_put)
{
	bool		isnull[INDEX_MAX_KEYS];
	int			natts;
	PgvectorIndexInfo  *indexInfo;

	(void) is_put;

	natts = RelationGetNumberOfAttributes(rel);
	if (natts > INDEX_MAX_KEYS)
		natts = INDEX_MAX_KEYS;

	for (int i = 0; i < natts; i++)
		isnull[i] = (nulls[i] == 'n');

	indexInfo = BuildIndexInfo(rel);
	(void) ivfflat_insertindex(rel, datum, isnull, ht_ctid, heapRel,
							   UNIQUE_CHECK_NO, indexInfo);
	pfree(indexInfo);
	return (InsertIndexResult) NULL;
}

bool
ivfflatgettuple(IndexScanDesc scan, ScanDirection dir)
{
	return ivfflat_gettupleindex(scan, dir);
}

char *
ivfflatbeginscan(Relation rel, bool fromEnd, uint16 keysz, ScanKey scankey)
{
	IndexScanDesc scan;

	scan = ivfflat_beginscanindex(rel, (int) keysz, 0);
	if (scankey != NULL && keysz > 0)
		memmove(scan->keyData, scankey, keysz * sizeof(ScanKeyData));
	scan->scanFromEnd = fromEnd;
	return (char *) scan;
}

void
ivfflatrescan(IndexScanDesc scan, bool fromEnd, ScanKey scankey)
{
	(void) fromEnd;
	ivfflat_rescanindex(scan, scankey, scan->numberOfKeys, NULL, 0);
}

void
ivfflatendscan(IndexScanDesc scan)
{
	ivfflat_endscanindex(scan);
}

TupleCount
ivfflatbulkdelete(Relation rel, int delcount, ItemPointerData *del_heappointers)
{
	IndexVacuumInfo info;
	IndexBulkDeleteResult *stats;
	IvfflatBulkDelState st;

	memset(&info, 0, sizeof(info));
	info.index = rel;

	st.delcount = delcount;
	st.items = del_heappointers;
	st.cursor = 0;

	stats = ivfflat_bulkdeleteindex(&info, NULL, ivfflat_bulkdel_callback, &st);
	if (stats == NULL)
		return 0;
	return (TupleCount) stats->tuples_removed;
}

void
ivfflatdelete(Relation rel, ItemPointer tid)
{
	(void) rel;
	(void) tid;
}

void
ivfflatcostestimate(Query *root,
					RelOptInfo *rel,
					IndexOptInfo *index,
					List *indexQuals,
					Cost *indexStartupCost,
					Cost *indexTotalCost,
					Selectivity *indexSelectivity)
{
	(void) root;
	(void) rel;
	(void) index;
	(void) indexQuals;
	if (indexStartupCost)
		*indexStartupCost = 0;
	if (indexTotalCost)
		*indexTotalCost = 0;
	if (indexSelectivity)
		*indexSelectivity = 0;
}

/* -------- HNSW PG7 entry points -------- */

typedef struct HnswBulkDelState
{
	int			delcount;
	ItemPointerData *items;
} HnswBulkDelState;

static bool
hnsw_bulkdel_callback(void *tid, void *state)
{
	HnswBulkDelState *st = (HnswBulkDelState *) state;
	ItemPointer htid = (ItemPointer) tid;
	int			i;

	for (i = 0; i < st->delcount; i++)
	{
		if (ItemPointerEquals(htid, &st->items[i]))
			return true;
	}
	return false;
}

void
hnswbuild(Relation heap,
		  Relation index,
		  int natts,
		  AttrNumber *attnum,
		  IndexStrategy istrat,
		  uint16 pcount,
		  Datum *params,
		  FuncIndexInfo *finfo,
		  PredInfo *predInfo)
{
	PgvectorIndexInfo  *indexInfo;
	IndexBuildResult *result;

	(void) istrat;
	(void) pcount;
	(void) params;
	(void) finfo;
	(void) predInfo;

	indexInfo = pgvector_make_indexinfo(natts, attnum);
	result = hnsw_buildindex(heap, index, indexInfo);
	if (result != NULL)
		pfree(result);
}

InsertIndexResult
hnswinsert(Relation rel,
		   Datum *datum,
		   char *nulls,
		   ItemPointer ht_ctid,
		   Relation heapRel,
		   bool is_put)
{
	bool		isnull[INDEX_MAX_KEYS];
	int			natts;
	PgvectorIndexInfo  *indexInfo;

	(void) is_put;

	natts = RelationGetNumberOfAttributes(rel);
	if (natts > INDEX_MAX_KEYS)
		natts = INDEX_MAX_KEYS;

	for (int i = 0; i < natts; i++)
		isnull[i] = (nulls[i] == 'n');

	indexInfo = BuildIndexInfo(rel);
	(void) hnsw_insertindex(rel, datum, isnull, ht_ctid, heapRel,
							UNIQUE_CHECK_NO, indexInfo);
	pfree(indexInfo);
	return (InsertIndexResult) NULL;
}

bool
hnswgettuple(IndexScanDesc scan, ScanDirection dir)
{
	return hnsw_gettupleindex(scan, dir);
}

char *
hnswbeginscan(Relation rel, bool fromEnd, uint16 keysz, ScanKey scankey)
{
	IndexScanDesc scan;

	scan = hnsw_beginscanindex(rel, (int) keysz, 0);
	if (scankey != NULL && keysz > 0)
		memmove(scan->keyData, scankey, keysz * sizeof(ScanKeyData));
	scan->scanFromEnd = fromEnd;
	return (char *) scan;
}

void
hnswrescan(IndexScanDesc scan, bool fromEnd, ScanKey scankey)
{
	(void) fromEnd;
	hnsw_rescanindex(scan, scankey, scan->numberOfKeys, NULL, 0);
}

void
hnswendscan(IndexScanDesc scan)
{
	hnsw_endscanindex(scan);
}

TupleCount
hnswbulkdelete(Relation rel, int delcount, ItemPointerData *del_heappointers)
{
	IndexVacuumInfo info;
	IndexBulkDeleteResult *stats;
	HnswBulkDelState st;

	memset(&info, 0, sizeof(info));
	info.index = rel;

	st.delcount = delcount;
	st.items = del_heappointers;

	stats = hnsw_bulkdeleteindex(&info, NULL, hnsw_bulkdel_callback, &st);
	if (stats == NULL)
		return 0;
	return (TupleCount) stats->tuples_removed;
}

void
hnswdelete(Relation rel, ItemPointer tid)
{
	(void) rel;
	(void) tid;
}

void
hnswcostestimate(Query *root,
				 RelOptInfo *rel,
				 IndexOptInfo *index,
				 List *indexQuals,
				 Cost *indexStartupCost,
				 Cost *indexTotalCost,
				 Selectivity *indexSelectivity)
{
	(void) root;
	(void) rel;
	(void) index;
	(void) indexQuals;
	if (indexStartupCost)
		*indexStartupCost = 0;
	if (indexTotalCost)
		*indexTotalCost = 0;
	if (indexSelectivity)
		*indexSelectivity = 0;
}
