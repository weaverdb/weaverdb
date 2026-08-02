/*
 * pgvector tuplesort: real PG7 heap sort API plus PG13-style slot/sort entry points.
 * Include this instead of utils/tuplesort.h in pgvector sources so pgvector_executor_port
 * macros do not rewrite declarations in tuplesort.h.
 */
#ifndef PGVECTOR_TUPLESORT_H
#define PGVECTOR_TUPLESORT_H

#include "utils/tuplesort.h"
#include "pgvector_executor_port.h"

#define TTSOpsMinimalTuple ((const void *) 0)
#define TTSOpsVirtual ((const void *) 0)
#define TUPLESORT_NONE 0

#endif /* PGVECTOR_TUPLESORT_H */
