#ifndef PGVECTOR_INDEX_H
#define PGVECTOR_INDEX_H

#include "postgres.h"

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
	bool		ii_Concurrent;
} PgvectorIndexInfo;

PgvectorIndexInfo *BuildIndexInfo(Relation index);

#endif /* PGVECTOR_INDEX_H */
