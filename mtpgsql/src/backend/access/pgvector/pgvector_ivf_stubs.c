/*
 * Placeholder ivfflat AM implementation symbols until ivfbuild.c / ivfscan.c
 * are ported from modern pgvector tuple-table APIs to Weaver PG7 executor.
 * PG7 catalog entry points in pgvector_pg7_am.c call these.
 */

#include "postgres.h"

#include "access/amapi.h"
#include "access/genam.h"
#include "access/relscan.h"
#include "ivfflat.h"
#include "pgvector_index.h"

IndexBuildResult *
ivfflat_buildindex(Relation heap, Relation index, PgvectorIndexInfo *indexInfo)
{
	IndexBuildResult *result;

	(void) heap;
	(void) index;
	(void) indexInfo;

	result = (IndexBuildResult *) palloc0(sizeof(IndexBuildResult));
	return result;
}

void
ivfflat_buildemptyindex(Relation index)
{
	(void) index;
}

IndexScanDesc
ivfflat_beginscanindex(Relation index, int nkeys, int norderbys)
{
	IndexScanDesc scan;
	IvfflatScanOpaque so;

	scan = RelationGetIndexScan(index, false, (uint16) nkeys, NULL);
	so = (IvfflatScanOpaque) palloc0(sizeof(IvfflatScanOpaqueData));
	so->numberOfOrderBys = norderbys;
	so->xs_snapshot = SnapshotNow;
	scan->opaque = so;
	return scan;
}

void
ivfflat_rescanindex(IndexScanDesc scan, ScanKey keys, int nkeys,
					ScanKey orderbys, int norderbys)
{
	(void) keys;
	(void) nkeys;
	(void) orderbys;
	(void) norderbys;
	(void) scan;
}

bool
ivfflat_gettupleindex(IndexScanDesc scan, ScanDirection dir)
{
	(void) scan;
	(void) dir;
	return false;
}

void
ivfflat_endscanindex(IndexScanDesc scan)
{
	if (scan->opaque != NULL)
	{
		pfree(scan->opaque);
		scan->opaque = NULL;
	}
}
