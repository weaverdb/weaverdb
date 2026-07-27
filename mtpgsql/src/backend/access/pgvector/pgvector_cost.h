#ifndef PGVECTOR_COST_H
#define PGVECTOR_COST_H

#include "nodes/relation.h"

void		ivfflatcostestimate(Query *root, RelOptInfo *rel, IndexOptInfo *index,
								List *indexQuals, Cost *indexStartupCost,
								Cost *indexTotalCost, Selectivity *indexSelectivity);
void		hnswcostestimate(Query *root, RelOptInfo *rel, IndexOptInfo *index,
						   List *indexQuals, Cost *indexStartupCost,
						   Cost *indexTotalCost, Selectivity *indexSelectivity);

#endif /* PGVECTOR_COST_H */
