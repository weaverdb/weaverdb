/* utils/float.h stub for pgvector compilation in WeaverDB
 *
 * In modern Postgres this declares float4/float8 in/out, comparisons,
 * and isnan wrappers.  We rely on <math.h> + project headers for basics.
 */
#ifndef UTILS_FLOAT_H
#define UTILS_FLOAT_H

#include <math.h>
#include "c.h"          /* for float4/float8 typedefs if present */
#include "postgres.h"

/* Ensure float4/float8 are available (project uses them via c.h or directly) */
#ifndef FLOAT4_DEFINED
typedef float float4;
typedef double float8;
#define FLOAT4_DEFINED 1
#endif

/* Common macros that may be referenced */
#ifndef isinf
#define isinf(x)    ((x) != 0 && (x) == (x)*2)
#endif
/* isnan usually from math.h; provide fallback */
#ifndef isnan
#define isnan(x)    ((x) != (x))
#endif

/* Used in some vector distance code paths */
#define FLOAT4_EQ(x,y)   (fabsf((x)-(y)) <= (FLT_EPSILON))
#define FLOAT8_EQ(x,y)   (fabs((x)-(y)) <= (DBL_EPSILON))

/* NOTE: Do not redeclare float4in/float*out here - the project's builtins.h
   (pulled via postgres.h) already provides them with the correct signatures
   for this fork. Declaring them with PG_FUNCTION_ARGS here caused conflicts. */

#endif /* UTILS_FLOAT_H */
