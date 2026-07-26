/* PG7 tuplesort bridge for pgvector (shadows modern API; see pgvector_executor_port.c). */
#ifndef UTILS_TUPLESORT_H
#define UTILS_TUPLESORT_H

#include "access/tupdesc.h"
#include "pgvector_executor_port.h"

typedef struct Tuplesortstate Tuplesortstate;
typedef struct Sharedsort Sharedsort;

typedef struct TuplesortCoordination
{
	int			nParticipants;
	Sharedsort *sharedsort;
} TuplesortCoordination;

#define TTSOpsMinimalTuple ((const void *) 0)
#define TTSOpsVirtual ((const void *) 0)
#define TUPLESORT_NONE 0

extern void tuplesort_performsort(Tuplesortstate *state);
extern void tuplesort_end(Tuplesortstate *state);
extern void tuplesort_rescan(Tuplesortstate *state);

#define tuplesort_performsort tuplesort_performsort
#define tuplesort_end tuplesort_end

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

#endif /* UTILS_TUPLESORT_H */
