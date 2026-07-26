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
#include "access/skey.h"
#include "catalog/index.h"
#include "fmgr.h"
#include "hnsw.h"
#include "ivfflat.h"
#include "pgvector_index.h"
#include "pgvector_scan.h"
#include "nodes/primnodes.h"
#include "nodes/relation.h"
#include "utils/builtins.h"
#include "utils/rel.h"

#include "catalog/pg_am.h"
#include "nodes/execnodes.h"

extern Datum ExecEvalExpr(Node *expression, ExprContext *econtext, Oid *returnType,
						  bool *isNull, bool *isDone);

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

static InsertIndexResult
pgvector_new_insert_result(void)
{
	InsertIndexResult res;

	res = (InsertIndexResult) palloc(sizeof(InsertIndexResultData));
	res->result = INDEX_INSERTED;
	return res;
}

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
	(void) finfo;
	(void) predInfo;

	{
		int			lists = IVFFLAT_DEFAULT_LISTS;

		if (pcount > 0 && params != NULL)
			lists = DatumGetInt32(params[0]);

		if (lists < IVFFLAT_MIN_LISTS || lists > IVFFLAT_MAX_LISTS)
			elog(ERROR, "ivfflat lists must be between %d and %d",
				 IVFFLAT_MIN_LISTS, IVFFLAT_MAX_LISTS);

		IvfflatSetBuildLists(RelationGetRelid(index), lists);
		indexInfo = pgvector_make_indexinfo(natts, attnum);
		result = ivfflat_buildindex(heap, index, indexInfo);
		IvfflatClearBuildLists(RelationGetRelid(index));
	}
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

	if (isnull[0])
		return (InsertIndexResult) NULL;

	indexInfo = BuildIndexInfo(rel);
	(void) ivfflat_insertindex(rel, datum, isnull, ht_ctid, heapRel,
							   UNIQUE_CHECK_NO, indexInfo);
	pfree(indexInfo);
	return pgvector_new_insert_result();
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
	(void) finfo;
	(void) predInfo;

	{
		int			m = 0;
		int			ef = 0;

		if (pcount > 0 && params != NULL)
			m = DatumGetInt32(params[0]);
		if (pcount > 1 && params != NULL)
			ef = DatumGetInt32(params[1]);

		if (m != 0 && (m < HNSW_MIN_M || m > HNSW_MAX_M))
			elog(ERROR, "hnsw m must be between %d and %d", HNSW_MIN_M, HNSW_MAX_M);
		if (ef != 0 && (ef < HNSW_MIN_EF_CONSTRUCTION || ef > HNSW_MAX_EF_CONSTRUCTION))
			elog(ERROR, "hnsw ef_construction must be between %d and %d",
				 HNSW_MIN_EF_CONSTRUCTION, HNSW_MAX_EF_CONSTRUCTION);

		HnswSetBuildParams(RelationGetRelid(index), m, ef);
		indexInfo = pgvector_make_indexinfo(natts, attnum);
		result = hnsw_buildindex(heap, index, indexInfo);
		HnswClearBuildParams(RelationGetRelid(index));
	}
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

	if (isnull[0])
		return (InsertIndexResult) NULL;

	indexInfo = BuildIndexInfo(rel);
	(void) hnsw_insertindex(rel, datum, isnull, ht_ctid, heapRel,
							UNIQUE_CHECK_NO, indexInfo);
	pfree(indexInfo);
	return pgvector_new_insert_result();
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

void
pgvector_bind_index_orderby(IndexScanDesc scan, Oid relam, Expr *orderExpr,
							ExprContext *econtext, Snapshot snapshot)
{
	ScanKeyData orderKey;
	Expr	   *expr;
	Oper	   *oper;
	List	   *arglist;
	Datum		val;
	bool		isnull;
	bool		isDone;

	if (scan == NULL || orderExpr == NULL || !IsA(orderExpr, Expr))
		return;

	expr = (Expr *) orderExpr;
	if (expr->opType != OP_EXPR || !IsA(expr->oper, Oper))
		return;

	oper = (Oper *) expr->oper;
	val = (Datum) 0;
	isnull = true;

	foreach(arglist, expr->args)
	{
		Node	   *arg = (Node *) lfirst(arglist);

		if (IsA(arg, Var))
			continue;

		val = ExecEvalExpr(arg, econtext, NULL, &isnull, &isDone);
		break;
	}

	ScanKeyEntryInitialize(&orderKey, isnull ? SK_ISNULL : 0,
						   (AttrNumber) 1, InvalidOid, val);

	if (relam == IVFFLAT_AM_OID)
	{
		IvfflatScanOpaque so = (IvfflatScanOpaque) scan->opaque;

		if (snapshot != NULL)
			so->xs_snapshot = snapshot;
		ivfflat_rescanindex(scan, scan->keyData, scan->numberOfKeys, &orderKey, 1);
	}
	else if (relam == HNSW_AM_OID)
	{
		HnswScanOpaque so = (HnswScanOpaque) scan->opaque;

		if (snapshot != NULL)
			so->xs_snapshot = snapshot;
		hnsw_rescanindex(scan, scan->keyData, scan->numberOfKeys, &orderKey, 1);
	}
}
