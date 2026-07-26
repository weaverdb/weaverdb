/*
 * HNSW env + lock tranche for WeaverDB (hnsw.c excluded; build/scan in hnswbuild.c / hnswscan.c).
 */

#include "postgres.h"

#include "env/env.h"

#include "access/genam.h"
#include "hnsw.h"
#include "pgvector_index.h"

static SectionId hnsw_env_id = SECTIONID("HNSW");

#ifdef TLS
TLS HnswGlobals *hnsw_globals = NULL;
#else
#define hnsw_globals GetEnv()->hnsw_globals
#endif

int			hnsw_lock_tranche_id;

HnswGlobals *
HnswGetEnv(void)
{
	HnswGlobals *info = hnsw_globals;

	if (info == NULL)
	{
		info = (HnswGlobals *) AllocateEnvSpace(hnsw_env_id, sizeof(HnswGlobals));
		info->ef_search = HNSW_DEFAULT_EF_SEARCH;
		info->iterative_scan = HNSW_ITERATIVE_SCAN_OFF;
		info->max_scan_tuples = 20000;
		info->scan_mem_multiplier = 1.0;
		hnsw_globals = info;
	}
	return info;
}

void
HnswInitLockTranche(void)
{
	if (hnsw_lock_tranche_id == 0)
		hnsw_lock_tranche_id = LWLockNewTrancheId();
}

void
HnswInit(void)
{
	(void) HnswGetEnv();
	HnswInitLockTranche();
}

#include "storage/bufmgr.h"

void
HnswWriteBuffer(Relation index, Buffer buf)
{
	LockBuffer(index, buf, BUFFER_LOCK_UNLOCK);
	WriteBuffer(index, buf);
}

IndexBulkDeleteResult *
hnsw_bulkdeleteindex(IndexVacuumInfo *info, IndexBulkDeleteResult *stats,
					 IndexBulkDeleteCallback callback, void *callback_state)
{
	(void) info;
	(void) callback;
	(void) callback_state;
	if (stats == NULL)
		stats = (IndexBulkDeleteResult *) palloc0(sizeof(IndexBulkDeleteResult));
	return stats;
}

IndexBulkDeleteResult *
hnsw_vacuumcleanupindex(IndexVacuumInfo *info, IndexBulkDeleteResult *stats)
{
	(void) info;
	return stats;
}
