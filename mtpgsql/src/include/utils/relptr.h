/* Relative pointers for parallel HNSW shared-memory graphs.
 *
 * Layout matches upstream Postgres: offset 0 means NULL; otherwise the
 * pointer is (base + offset - 1). Storing NULL must clear the offset —
 * casting (NULL - base) to Size wraps and produces a non-null garbage link.
 */

#ifndef UTILS_RELPTR_H
#define UTILS_RELPTR_H

#include "postgres.h"

#define relptr_declare(type, relptrtype) \
	typedef struct { Size relptr_off; } relptrtype

#define relptr_is_null(rp)		((rp).relptr_off == 0)

#define relptr_store(base, rp, value) \
	((void) ((rp).relptr_off = \
		((void *) (value) == NULL ? (Size) 0 : \
		 ((Size) ((char *) (value) - (char *) (base)) + (Size) 1))))

#define relptr_access(base, rp) \
	((void *) ((rp).relptr_off == 0 ? NULL : \
		((char *) (base) + (rp).relptr_off - 1)))

#endif /* UTILS_RELPTR_H */
