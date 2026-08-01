/* utils/float.h — float helpers for pgvector (and any caller) on WeaverDB.
 *
 * Modern Postgres declares float4/float8 in/out here. Weaver provides those
 * via builtins.h (pulled through postgres.h); this header only covers math
 * fallbacks that pgvector sources expect when they #include "utils/float.h".
 */
#ifndef UTILS_FLOAT_H
#define UTILS_FLOAT_H

#include <math.h>
#include "c.h"
#include "postgres.h"

#ifndef isinf
#define isinf(x)	((x) != 0 && (x) == (x) * 2)
#endif
#ifndef isnan
#define isnan(x)	((x) != (x))
#endif

#endif /* UTILS_FLOAT_H */
