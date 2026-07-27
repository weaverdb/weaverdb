#include "postgres.h"

#include <float.h>

#include "env/env.h"

#include "access/amapi.h"
#include "access/genam.h"
#include "access/reloptions.h"
#include "commands/progress.h"
#include "commands/vacuum.h"
#include "fmgr.h"
#include "ivfflat.h"
#include "pgvector_module.h"
#include "nodes/pg_list.h"
#include "utils/float.h"
#include "utils/guc.h"
#include "utils/relcache.h"
#include "utils/spccache.h"
#include "vector.h"

#define MarkGUCPrefixReserved(x) EmitWarningsOnPlaceholders(x)

static SectionId ivfflat_env_id = SECTIONID("IVFL");

#ifdef TLS
TLS IvfflatGlobals *ivfflat_globals = NULL;
#else
#define ivfflat_globals GetEnv()->ivfflat_globals
#endif

IvfflatGlobals *
IvfflatGetEnv(void)
{
	PgvectorEnsureInit();
	IvfflatGlobals *info = ivfflat_globals;

	if (info == NULL)
	{
		info = (IvfflatGlobals *) AllocateEnvSpace(ivfflat_env_id, sizeof(IvfflatGlobals));
		info->probes = IVFFLAT_DEFAULT_PROBES;
		info->iterative_scan = IVFFLAT_ITERATIVE_SCAN_OFF;
		info->max_probes = IVFFLAT_DEFAULT_LISTS;
		info->assign_workers = IVFFLAT_DEFAULT_ASSIGN_WORKERS;
		ivfflat_globals = info;
	}
	return info;
}

static relopt_kind ivfflat_relopt_kind;

static const struct config_enum_entry ivfflat_iterative_scan_options[] = {
	{"off", IVFFLAT_ITERATIVE_SCAN_OFF, false},
	{"relaxed_order", IVFFLAT_ITERATIVE_SCAN_RELAXED, false},
	{NULL, 0, false}
};

/*
 * Initialize index options and variables
 */
void
IvfflatInit(void)
{
	(void) IvfflatGetEnv();
	/* reloptions/GUC registration deferred until ivfflat GUC APIs are ported */
	(void) ivfflat_iterative_scan_options;
}

/*
 * Get the name of index build phase
 */
static char *
ivfflatbuildphasename(int64 phasenum)
{
	switch (phasenum)
	{
		case PROGRESS_CREATEIDX_SUBPHASE_INITIALIZE:
			return "initializing";
		case PROGRESS_IVFFLAT_PHASE_KMEANS:
			return "performing k-means";
		case PROGRESS_IVFFLAT_PHASE_ASSIGN:
			return "assigning tuples";
		case PROGRESS_IVFFLAT_PHASE_LOAD:
			return "loading tuples";
		default:
			return NULL;
	}
}

/*
 * Estimate the cost of an index scan
 */
static void
ivfflatcostestimate(PlannerInfo *root, IndexPath *path, double loop_count,
					Cost *indexStartupCost, Cost *indexTotalCost,
					Selectivity *indexSelectivity, double *indexCorrelation,
					double *indexPages)
{
	/* Stub cost estimate for build integration; full genericcostestimate etc later */
	(void)root; (void)path; (void)loop_count;
	*indexStartupCost = 0;
	*indexTotalCost = 0;
	*indexSelectivity = 0;
	*indexCorrelation = 0;
	*indexPages = 0;
	return;  /* original body removed for build */
}

/*
 * Parse and validate the reloptions
 */
static bytea *
ivfflatoptions(Datum reloptions, bool validate)
{
	/* stub for build */
	(void)reloptions; (void)validate;
	return (bytea *) 0;
}

/*
 * Validate catalog entries for the specified operator class
 */
static bool
ivfflatvalidate(Oid opclassoid)
{
	return true;
}

/*
 * Define index handler
 *
 * See https://www.postgresql.org/docs/current/index-api.html
 */
FUNCTION_PREFIX PG_FUNCTION_INFO_V1(ivfflathandler);
Datum
ivfflathandler(PG_FUNCTION_ARGS)
{
	/* stubbed handler body for build system */
	(void) PG_GETARG_DATUM(0);
	PG_RETURN_POINTER(NULL);

}

