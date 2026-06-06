#ifndef HALFUTILS_H
#define HALFUTILS_H

/* Stubbed for WeaverDB integration - halfvec support can be added later */
#include "halfvec.h"

void		HalfvecInit(void);

/* Stubs to satisfy references */
static inline float HalfvecL2SquaredDistance(int dim, half *ax, half *bx) { return 0.0f; }
static inline float HalfvecInnerProduct(int dim, half *ax, half *bx) { return 0.0f; }
static inline double HalfvecCosineSimilarity(int dim, half *ax, half *bx) { return 0.0; }
static inline float HalfvecL1Distance(int dim, half *ax, half *bx) { return 0.0f; }

void HalfvecInit(void) { }

#endif /* HALFUTILS_H */
