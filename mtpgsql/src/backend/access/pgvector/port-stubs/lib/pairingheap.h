/* Stub pairingheap.h for pgvector hnsw/ivfflat in WeaverDB.
 * Provides enough of the pairingheap API for the headers (hnsw.h, ivfflat.h)
 * and .c files that use it (mostly internal lists during build/insert/vacuum).
 */

#ifndef LIB_PAIRINGHEAP_H
#define LIB_PAIRINGHEAP_H

#include <stddef.h>   /* for offsetof in the container macros */

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

#endif /* LIB_PAIRINGHEAP_H */