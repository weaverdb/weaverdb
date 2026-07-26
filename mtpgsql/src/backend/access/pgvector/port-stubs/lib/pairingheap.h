/* Stub pairingheap.h for pgvector hnsw/ivfflat in WeaverDB.
 * Provides enough of the pairingheap API for the headers (hnsw.h, ivfflat.h)
 * and .c files that use it (mostly internal lists during build/insert/vacuum).
 */

#ifndef LIB_PAIRINGHEAP_H
#define LIB_PAIRINGHEAP_H

#include <stddef.h>

struct pairingheap;

#ifndef palloc0
extern void *palloc0(size_t size);
#endif

typedef struct pairingheap_node
{
	struct pairingheap_node *first_child;
	struct pairingheap_node *next_sibling;
	struct pairingheap_node *prev_or_parent;
} pairingheap_node;

typedef struct pairingheap pairingheap;

/* pairingheap itself is opaque for our purposes; only pointers are stored in structs */
struct pairingheap
{
	pairingheap_node *root;
	/* comparator etc not needed for type checking in headers */
};

/* Macros used by pgvector code (hnsw/ivf) */
#define pairingheap_container(type, membername, ptr) \
	((type *) ((char *) (ptr) - offsetof(type, membername)))

#define pairingheap_const_container(type, membername, ptr) \
	((const type *) ((const char *) (ptr) - offsetof(type, membername)))

/* The node embedded in candidate structs */
#define pairingheap_node_init(node)		((void)0)
#define pairingheap_is_empty(h)			((h)->root == NULL)

static inline pairingheap *
pairingheap_allocate(int (*compare) (const pairingheap_node *, const pairingheap_node *, void *),
					 void *arg)
{
	(void) compare;
	(void) arg;
	return (pairingheap *) palloc0(sizeof(pairingheap));
}

static inline void
pairingheap_add(pairingheap *heap, pairingheap_node *node)
{
	(void) heap;
	(void) node;
}

static inline pairingheap_node *
pairingheap_first(pairingheap *heap)
{
	return heap->root;
}

static inline pairingheap_node *
pairingheap_remove_first(pairingheap *heap)
{
	pairingheap_node *n = heap->root;
	heap->root = NULL;
	return n;
}

static inline void
pairingheap_reset(pairingheap *heap)
{
	if (heap)
		heap->root = NULL;
}

#endif /* LIB_PAIRINGHEAP_H */