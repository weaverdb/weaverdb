/* Stub relptr.h for pgvector (relative pointers in parallel HNSW shared memory) */

#ifndef UTILS_RELPTR_H
#define UTILS_RELPTR_H

#include "postgres.h"

#define relptr_declare(type, relptrtype) \
	typedef struct { Size relptr_off; } relptrtype

#define relptr_is_null(rp)		((rp).relptr_off == 0)

#define relptr_store(base, rp, value) \
	((void) ((rp).relptr_off = ((base) == NULL ? 0 : \
		((Size) ((char *) (value) - (char *) (base)) + 1))))

#define relptr_access(base, rp) \
	((void *) ((base) == NULL ? NULL : ((char *) (base) + (rp).relptr_off - 1)))

#endif /* UTILS_RELPTR_H */
