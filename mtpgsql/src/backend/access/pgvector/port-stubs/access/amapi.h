/* Expanded IndexAmRoutine for pgvector compile-first.
 * Uses Weaver PgvectorIndexInfo from nodes/execnodes.h — do not redefine it here.
 */
#ifndef ACCESS_AMAPI_H
#define ACCESS_AMAPI_H

#include "access/relscan.h"
#include "pgvector_index.h"
#include "access/sdir.h"

typedef struct IndexBuildResult
{
	double		heap_tuples;
	double		index_tuples;
} IndexBuildResult;

typedef int IndexUniqueCheck;
#define UNIQUE_CHECK_NO 0
#define UNIQUE_CHECK_YES 1
#define UNIQUE_CHECK_PARTIAL 2
#define UNIQUE_CHECK_EXISTING 3

#ifndef VACUUM_OPTION_PARALLEL_BULKDEL
#define VACUUM_OPTION_PARALLEL_BULKDEL 0
#endif

typedef double Cost;
typedef double Selectivity;

typedef struct IndexAmRoutine
{
	NodeTag		type;
	uint16		amstrategies;
	uint16		amsupport;
	uint16		amoptsprocnum;
	bool		amcanorder;
	bool		amcanorderbyop;
	bool		amcanbackward;
	bool		amcanunique;
	bool		amcanmulticol;
	bool		amoptionalkey;
	bool		amsearcharray;
	bool		amsearchnulls;
	bool		amstorage;
	bool		amclusterable;
	bool		ampredlocks;
	bool		amcanparallel;
	bool		amcaninclude;
	bool		amusemaintenanceworkmem;
	uint8		amparallelvacuumoptions;
	Oid			amkeytype;

	IndexBuildResult *(*ambuild) (Relation heap, Relation index, PgvectorIndexInfo *indexInfo);
	void		(*ambuildempty) (Relation index);
	bool		(*aminsert) (Relation index, Datum *values, bool *isnull,
							 ItemPointer heap_tid, Relation heap,
							 IndexUniqueCheck checkUnique, PgvectorIndexInfo *indexInfo);
	IndexBulkDeleteResult *(*ambulkdelete) (IndexVacuumInfo *info, IndexBulkDeleteResult *stats,
											IndexBulkDeleteCallback callback, void *callback_state);
	IndexBulkDeleteResult *(*amvacuumcleanup) (IndexVacuumInfo *info, IndexBulkDeleteResult *stats);
	void		(*amcostestimate) (struct PlannerInfo *root, struct IndexPath *path, double loop_count,
								   Cost *indexStartupCost, Cost *indexTotalCost,
								   Selectivity *indexSelectivity, double *indexCorrelation,
								   double *indexPages);
	bytea	   *(*amoptions) (Datum reloptions, bool validate);
	bool		(*amvalidate) (Oid opclassoid);
	char	   *(*ambuildphasename) (int64 phasenum);
	IndexScanDesc (*ambeginscan) (Relation index, int nkeys, int norderbys);
	void		(*amrescan) (IndexScanDesc scan, ScanKey keys, int nkeys, ScanKey orderbys, int norderbys);
	bool		(*amgettuple) (IndexScanDesc scan, ScanDirection dir);
	void		(*amendscan) (IndexScanDesc scan);
} IndexAmRoutine;

typedef IndexAmRoutine *IndexAmRoutinePtr;

#define AMFLAG_INSERT_AFTER 0
#define T_IndexAmRoutine 0

#ifndef makeNode
#define makeNode(x) ((x *) palloc0(sizeof(x)))
#endif

#endif /* ACCESS_AMAPI_H */
