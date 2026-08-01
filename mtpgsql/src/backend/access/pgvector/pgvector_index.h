#ifndef PGVECTOR_INDEX_H
#define PGVECTOR_INDEX_H

#include "postgres.h"
#include "access/funcindex.h"

/*
 * Index build/insert context for pgvector access methods on WeaverDB.
 * PG7 catalog build passes attribute numbers via index_build(); modern
 * pgvector sources expect this shape instead of nodes/execnodes.h IndexInfo.
 */
typedef struct PgvectorIndexInfo
{
	int			ii_NumKeyAttributes;
	int			ii_NumIndexAttrs;
	AttrNumber *ii_KeyAttributeNumbers;
	FuncIndexInfo *ii_FuncIndexInfo;
	bool		ii_Concurrent;
} PgvectorIndexInfo;

PgvectorIndexInfo *BuildIndexInfo(Relation index);

/* Shared with modern amapi / hnsw / ivfflat (not in PG7 genam). */
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

#endif /* PGVECTOR_INDEX_H */
