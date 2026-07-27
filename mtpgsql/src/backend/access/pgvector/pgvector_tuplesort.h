/*
 * pgvector tuplesort: real PG7 heap sort API plus PG13-style slot/sort entry points.
 * Include this instead of utils/tuplesort.h in pgvector sources so pgvector_executor_port
 * macros do not rewrite declarations in tuplesort.h.
 */
#ifndef PGVECTOR_TUPLESORT_H
#define PGVECTOR_TUPLESORT_H

#include "utils/tuplesort.h"
#include "pgvector_executor_port.h"

typedef struct Sharedsort Sharedsort;

typedef struct TuplesortCoordination
{
	int			nParticipants;
	Sharedsort *sharedsort;
} TuplesortCoordination;

#define TTSOpsMinimalTuple ((const void *) 0)
#define TTSOpsVirtual ((const void *) 0)
#define TUPLESORT_NONE 0

static inline Size
tuplesort_estimate_shared(int n)
{
	(void) n;
	return 0;
}

static inline void
tuplesort_initialize_shared(Sharedsort *sharedsort, int n, void *seg)
{
	(void) sharedsort;
	(void) n;
	(void) seg;
}

static inline void
tuplesort_attach_shared(Sharedsort *sharedsort, void *seg)
{
	(void) sharedsort;
	(void) seg;
}

#endif /* PGVECTOR_TUPLESORT_H */
