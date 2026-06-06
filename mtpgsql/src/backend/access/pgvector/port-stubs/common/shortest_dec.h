/* Stub for common/shortest_dec.h used by pgvector vector/sparse/halfvec in WeaverDB */
#ifndef COMMON_SHORTEST_DEC_H
#define COMMON_SHORTEST_DEC_H

#include <stdio.h>

/*
 * Approximate Postgres' shortest-decimal output for floats.
 * FLOAT_SHORTEST_DECIMAL_LEN is used for buffer sizing in *_out functions.
 */
#define FLOAT_SHORTEST_DECIMAL_LEN 32
#define DOUBLE_SHORTEST_DECIMAL_LEN 32

static inline int
float_to_shortest_decimal_bufn(float f, char *buf)
{
	/* Use %g for short, but clamp to avoid huge exponents in bad cases */
	int n = snprintf(buf, FLOAT_SHORTEST_DECIMAL_LEN, "%.6g", (double) f);
	if (n < 0)
		n = 0;
	else if (n >= FLOAT_SHORTEST_DECIMAL_LEN)
		n = FLOAT_SHORTEST_DECIMAL_LEN - 1;
	return n;
}

static inline int
double_to_shortest_decimal_bufn(double f, char *buf)
{
	int n = snprintf(buf, DOUBLE_SHORTEST_DECIMAL_LEN, "%.6g", f);
	if (n < 0)
		n = 0;
	else if (n >= DOUBLE_SHORTEST_DECIMAL_LEN)
		n = DOUBLE_SHORTEST_DECIMAL_LEN - 1;
	return n;
}

#endif /* COMMON_SHORTEST_DEC_H */
