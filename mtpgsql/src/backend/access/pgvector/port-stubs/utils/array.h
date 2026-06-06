/* Interposer for utils/array.h when building pgvector.
 *
 * Because pgvector target puts port-stubs early in -I, this file is found
 * by the #include "utils/array.h" in vector.c / sparsevec.c / halfvec.c / shims.
 *
 * We pull the *real* project array definitions (so ArrayType layout + ARR_* macros
 * and the project's own array funcs are known), then declare the shim functions
 * that pgvector code calls (deconstruct_array, construct_array) and the small
 * helpers (array_contains_nulls, get_typlenbyvalalign) that we implement in
 * pgvector_shims.c .
 *
 * This lets us adapt without editing the imported pgvector sources and without
 * shadowing breaking the rest of the build.
 */

#ifndef PGVECTOR_ARRAY_INTERPOSER_H
#define PGVECTOR_ARRAY_INTERPOSER_H

/* Pull the real one using a path relative to this file's location in the tree.
   (src/backend/access/pgvector/port-stubs/utils/array.h  ->  src/include/utils/array.h) */
#include "../../../include/utils/array.h"

/* Now ArrayType and the ARR_ macros are from the project. Declare our shims. */
extern bool array_contains_nulls(ArrayType *array);
extern void get_typlenbyvalalign(Oid elemtype, int16 *typlen, bool *typbyval, char *typalign);

extern void deconstruct_array(ArrayType *array,
							  Oid elmtype, int elmlen, bool elmbyval, char elmalign,
							  Datum **elemsp, bool **nullsp, int *nelemsp);

extern ArrayType *construct_array(Datum *elems, int nelems,
								  Oid elmtype, int elmlen, bool elmbyval, char elmalign);

/* palloc0 is heavily used by pgvector sources (ivfbuild, hnswbuild, halfvec, etc).
 * The fork's palloc.h does not declare it (only palloc + pfree etc). Provide decl here
 * so every pgvector TU (via their #include "utils/array.h") sees it before use.
 * Body is provided once in pgvector_shims.c . */
extern void *palloc0(Size size);

#endif /* PGVECTOR_ARRAY_INTERPOSER_H */
