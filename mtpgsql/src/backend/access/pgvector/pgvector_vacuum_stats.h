#ifndef PGVECTOR_VACUUM_STATS_H
#define PGVECTOR_VACUUM_STATS_H

#include "access/genam.h"
#include "utils/rel.h"

TupleCount pgvector_lazy_index_tuple_count(Relation index);

#endif
