#ifndef PGVECTOR_PLANNER_H
#define PGVECTOR_PLANNER_H

#include "nodes/relation.h"

/*
 * True when the query's primary sort key is a distance operator over this
 * index's indexed column (ivfflat / hnsw).
 */
extern bool pgvector_index_useful_for_ordering(Query *root, RelOptInfo *rel,
											   IndexOptInfo *index);

#endif /* PGVECTOR_PLANNER_H */
