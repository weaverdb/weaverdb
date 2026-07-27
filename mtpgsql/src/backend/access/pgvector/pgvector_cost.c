/*
 * PG7 index cost estimators for ivfflat and hnsw (ported from pgvector 0.8).
 */

#include "postgres.h"

#include <math.h>

#include "access/genam.h"
#include "catalog/pg_am.h"
#include "commands/variable.h"
#include "hnsw.h"
#include "ivfflat.h"
#include "optimizer/clauses.h"
#include "optimizer/cost.h"
#include "optimizer/pgvector.h"
#include "utils/builtins.h"

typedef struct PgvectorIndexCosts
{
	double		numIndexTuples;
	double		numIndexPages;
	Cost		indexTotalCost;
	Selectivity indexSelectivity;
} PgvectorIndexCosts;

static void
pgvector_set_infinite_cost(Cost *indexStartupCost, Cost *indexTotalCost,
						   Selectivity *indexSelectivity)
{
	double		inf = (double) 1.0 / 0.0;

	if (indexStartupCost)
		*indexStartupCost = inf;
	if (indexTotalCost)
		*indexTotalCost = inf;
	if (indexSelectivity)
		*indexSelectivity = 0;
}

static void
pgvector_base_index_cost(Query *root, RelOptInfo *rel, IndexOptInfo *index,
						 List *indexQuals, PgvectorIndexCosts *costs)
{
	double		evalcost = cost_qual_eval(indexQuals);

	costs->indexSelectivity = clauselist_selectivity(root, indexQuals,
													 lfirsti(rel->relids));
	costs->numIndexTuples = costs->indexSelectivity * index->tuples;
	costs->numIndexPages = costs->indexSelectivity * index->pages;

	if (costs->numIndexTuples < 1.0)
		costs->numIndexTuples = 1.0;
	if (costs->numIndexPages < 1.0)
		costs->numIndexPages = 1.0;

	costs->indexTotalCost = costs->numIndexPages +
		((GetCostInfo()->cpu_index_tuple_cost + evalcost) * costs->numIndexTuples);
}

static Cost
pgvector_ivfflat_startup_cost(RelOptInfo *rel, PgvectorIndexCosts *costs, double ratio)
{
	double		sequentialRatio = 0.5;
	double		random_page_cost = GetCostInfo()->random_page_cost;
	double		spc_seq_page_cost = 1.0;
	double		startupPages;
	Cost		startup;

	costs->indexTotalCost -= sequentialRatio * costs->numIndexPages *
		(random_page_cost - spc_seq_page_cost);

	startup = costs->indexTotalCost * ratio;
	startupPages = costs->numIndexPages * ratio;
	if (startupPages > rel->pages && ratio < 0.5)
	{
		startup -= (1.0 - sequentialRatio) * startupPages *
			(random_page_cost - spc_seq_page_cost);
		startup -= (startupPages - rel->pages) * spc_seq_page_cost;
	}
	return startup;
}

static Cost
pgvector_hnsw_startup_cost(RelOptInfo *rel, PgvectorIndexCosts *costs, double ratio)
{
	double		random_page_cost = GetCostInfo()->random_page_cost;
	double		spc_seq_page_cost = 1.0;
	double		startupPages;
	Cost		startup;

	startup = costs->indexTotalCost * ratio;
	startupPages = costs->numIndexPages * ratio;
	if (startupPages > rel->pages && ratio < 0.5)
	{
		startup -= startupPages * (random_page_cost - spc_seq_page_cost);
		startup -= (startupPages - rel->pages) * spc_seq_page_cost;
	}
	return startup;
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
	PgvectorIndexCosts costs;
	Relation	indexRel;
	int			lists;
	double		ratio;

	if (!pgvector_index_useful_for_ordering(root, rel, index))
	{
		pgvector_set_infinite_cost(indexStartupCost, indexTotalCost, indexSelectivity);
		return;
	}

	pgvector_base_index_cost(root, rel, index, indexQuals, &costs);

	indexRel = index_open(index->indexoid);
	IvfflatGetMetaPageInfo(indexRel, &lists, NULL);
	index_close(indexRel);

	if (lists <= 0)
		lists = IVFFLAT_DEFAULT_LISTS;

	ratio = ((double) ivfflat_probes) / lists;
	if (ratio > 1.0)
		ratio = 1.0;

	if (indexStartupCost)
		*indexStartupCost = pgvector_ivfflat_startup_cost(rel, &costs, ratio);
	if (indexTotalCost)
		*indexTotalCost = costs.indexTotalCost;
	if (indexSelectivity)
		*indexSelectivity = costs.indexSelectivity;
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
	PgvectorIndexCosts costs;
	Relation	indexRel;
	int			m;
	double		ratio;

	if (!pgvector_index_useful_for_ordering(root, rel, index))
	{
		pgvector_set_infinite_cost(indexStartupCost, indexTotalCost, indexSelectivity);
		return;
	}

	pgvector_base_index_cost(root, rel, index, indexQuals, &costs);

	indexRel = index_open(index->indexoid);
	HnswGetMetaPageInfo(indexRel, &m, NULL);
	index_close(indexRel);

	if (m <= 0)
		m = HNSW_DEFAULT_M;

	if (index->tuples > 0)
	{
		double		scalingFactor = 0.55;
		int			entryLevel = (int) (log(index->tuples) * HnswGetMl(m));
		int			layer0TuplesMax = HnswGetLayerM(m, 0) * hnsw_ef_search;
		double		layer0Selectivity = scalingFactor * log(index->tuples) /
			(log(m) * (1 + log(hnsw_ef_search)));

		ratio = (entryLevel * m + layer0TuplesMax * layer0Selectivity) / index->tuples;
		if (ratio > 1)
			ratio = 1;
	}
	else
		ratio = 1;

	if (indexStartupCost)
		*indexStartupCost = pgvector_hnsw_startup_cost(rel, &costs, ratio);
	if (indexTotalCost)
		*indexTotalCost = costs.indexTotalCost;
	if (indexSelectivity)
		*indexSelectivity = costs.indexSelectivity;
}
