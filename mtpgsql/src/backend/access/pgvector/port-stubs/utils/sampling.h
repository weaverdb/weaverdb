/* Reservoir sampling for ivfflat build (minimal port of analyze logic). */
#ifndef UTILS_SAMPLING_H
#define UTILS_SAMPLING_H

#include <math.h>
#include <stdlib.h>

#ifndef MAX_RANDOM_VALUE
#define MAX_RANDOM_VALUE 2147483647
#endif

typedef struct BlockSamplerData
{
	int			unused;
} BlockSamplerData;
typedef struct ReservoirStateData ReservoirStateData;

struct ReservoirStateData
{
	double		W;
	int			reservoir_size;
};

static inline double
sampler_random_fract(void)
{
	return ((double) random()) / MAX_RANDOM_VALUE;
}

static inline void
reservoir_init_selection_state(ReservoirStateData *state, int n)
{
	state->reservoir_size = n;
	state->W = pow(sampler_random_fract(), 1.0 / (double) n);
}

static inline double
reservoir_get_next_S(ReservoirStateData *state, double rownum, double targrows)
{
	(void) rownum;
	(void) targrows;
	/* Skip 0 tuples before next reservoir replacement decision */
	(void) state;
	return 0.0;
}

#endif /* UTILS_SAMPLING_H */
