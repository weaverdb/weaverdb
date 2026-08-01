/*
 * pairingheap.h — real pairing-heap API for pgvector (replaces no-op stub).
 * Adapted from PostgreSQL backend/lib/pairingheap.
 */
#ifndef LIB_PAIRINGHEAP_H
#define LIB_PAIRINGHEAP_H

#include <stddef.h>

typedef struct pairingheap_node
{
	struct pairingheap_node *first_child;
	struct pairingheap_node *next_sibling;
	struct pairingheap_node *prev_or_parent;
} pairingheap_node;

#define pairingheap_container(type, membername, ptr) \
	((type *) ((char *) (ptr) - offsetof(type, membername)))

#define pairingheap_const_container(type, membername, ptr) \
	((const type *) ((const char *) (ptr) - offsetof(type, membername)))

typedef int (*pairingheap_comparator) (const pairingheap_node *a,
									   const pairingheap_node *b,
									   void *arg);

typedef struct pairingheap
{
	pairingheap_comparator ph_compare;
	void	   *ph_arg;
	pairingheap_node *ph_root;
} pairingheap;

extern pairingheap *pairingheap_allocate(pairingheap_comparator compare, void *arg);
extern void pairingheap_free(pairingheap *heap);
extern void pairingheap_add(pairingheap *heap, pairingheap_node *node);
extern pairingheap_node *pairingheap_first(pairingheap *heap);
extern pairingheap_node *pairingheap_remove_first(pairingheap *heap);
extern void pairingheap_remove(pairingheap *heap, pairingheap_node *node);

#define pairingheap_reset(h)	((h)->ph_root = NULL)
#define pairingheap_is_empty(h) ((h)->ph_root == NULL)

#endif /* LIB_PAIRINGHEAP_H */
