/* Stub lwlock.h for pgvector */

#ifndef STORAGE_LWLOCK_H
#define STORAGE_LWLOCK_H

struct LWLock
{
	int			dummy;
	/* real has tranche, lock etc; dummy for struct layout in hnsw headers */
};
typedef struct LWLock LWLock;

#endif /* STORAGE_LWLOCK_H */
