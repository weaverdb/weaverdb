#ifndef PGVECTOR_SCAN_H
#define PGVECTOR_SCAN_H

#include <string.h>
#include "access/genam.h"
#include "access/relscan.h"
#include "ivfflat.h"
#include "hnsw.h"
#include "utils/tqual.h"

/*
 * Map modern pgvector scan code onto PG7 IndexScanDesc (relation, keyData, …).
 * Order-by keys and snapshot live in AM opaque, not in the global scan struct.
 */
static inline Relation
pgvector_scan_index_rel(IndexScanDesc scan)
{
	return scan->relation;
}

static inline ScanKey
pgvector_ivfflat_orderby(IndexScanDesc scan)
{
	IvfflatScanOpaque so = (IvfflatScanOpaque) scan->opaque;

	return &so->orderByData;
}

static inline int
pgvector_ivfflat_norderbys(IndexScanDesc scan)
{
	IvfflatScanOpaque so = (IvfflatScanOpaque) scan->opaque;

	return so->numberOfOrderBys;
}

static inline void
pgvector_ivfflat_set_orderbys(IndexScanDesc scan, ScanKey orderbys, int norderbys)
{
	IvfflatScanOpaque so = (IvfflatScanOpaque) scan->opaque;

	so->numberOfOrderBys = norderbys;
	if (norderbys > 0 && orderbys != NULL)
		memcpy(&so->orderByData, orderbys, sizeof(ScanKeyData));
}

static inline Snapshot
pgvector_scan_snapshot(IndexScanDesc scan)
{
	IvfflatScanOpaque so = (IvfflatScanOpaque) scan->opaque;

	if (so->xs_snapshot != NULL)
		return so->xs_snapshot;
	return SnapshotNow;
}

#ifndef IsMVCCSnapshot
#define IsMVCCSnapshot(s) ((s) == SnapshotNow || (s) == NULL)
#endif

static inline Relation
pgvector_hnsw_index_rel(IndexScanDesc scan)
{
	return scan->relation;
}

static inline ScanKey
pgvector_hnsw_orderby(IndexScanDesc scan)
{
	HnswScanOpaque so = (HnswScanOpaque) scan->opaque;

	return &so->orderByData;
}

static inline int
pgvector_hnsw_norderbys(IndexScanDesc scan)
{
	HnswScanOpaque so = (HnswScanOpaque) scan->opaque;

	return so->numberOfOrderBys;
}

static inline void
pgvector_hnsw_set_orderbys(IndexScanDesc scan, ScanKey orderbys, int norderbys)
{
	HnswScanOpaque so = (HnswScanOpaque) scan->opaque;

	so->numberOfOrderBys = norderbys;
	if (norderbys > 0 && orderbys != NULL)
		memcpy(&so->orderByData, orderbys, sizeof(ScanKeyData));
}

static inline Snapshot
pgvector_hnsw_snapshot(IndexScanDesc scan)
{
	HnswScanOpaque so = (HnswScanOpaque) scan->opaque;

	if (so->xs_snapshot != NULL)
		return so->xs_snapshot;
	return SnapshotNow;
}

#endif /* PGVECTOR_SCAN_H */
