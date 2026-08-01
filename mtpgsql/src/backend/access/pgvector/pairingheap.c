/*
 * pairingheap.c — pairing heap for pgvector HNSW/IVFFlat priority queues.
 * Adapted from PostgreSQL src/backend/lib/pairingheap.c.
 */

#include "postgres.h"

#include "lib/pairingheap.h"

static pairingheap_node *merge(pairingheap *heap, pairingheap_node *a,
							   pairingheap_node *b);
static pairingheap_node *merge_children(pairingheap *heap,
										pairingheap_node *children);

pairingheap *
pairingheap_allocate(pairingheap_comparator compare, void *arg)
{
	pairingheap *heap;

	heap = (pairingheap *) palloc(sizeof(pairingheap));
	heap->ph_compare = compare;
	heap->ph_arg = arg;
	heap->ph_root = NULL;
	return heap;
}

void
pairingheap_free(pairingheap *heap)
{
	pfree(heap);
}

static pairingheap_node *
merge(pairingheap *heap, pairingheap_node *a, pairingheap_node *b)
{
	if (a == NULL)
		return b;
	if (b == NULL)
		return a;

	if (heap->ph_compare(a, b, heap->ph_arg) < 0)
	{
		pairingheap_node *tmp = a;

		a = b;
		b = tmp;
	}

	if (a->first_child)
		a->first_child->prev_or_parent = b;
	b->prev_or_parent = a;
	b->next_sibling = a->first_child;
	a->first_child = b;

	return a;
}

void
pairingheap_add(pairingheap *heap, pairingheap_node *node)
{
	node->first_child = NULL;
	heap->ph_root = merge(heap, heap->ph_root, node);
	heap->ph_root->prev_or_parent = NULL;
	heap->ph_root->next_sibling = NULL;
}

pairingheap_node *
pairingheap_first(pairingheap *heap)
{
	Assert(!pairingheap_is_empty(heap));
	return heap->ph_root;
}

pairingheap_node *
pairingheap_remove_first(pairingheap *heap)
{
	pairingheap_node *result;
	pairingheap_node *children;

	Assert(!pairingheap_is_empty(heap));

	result = heap->ph_root;
	children = result->first_child;

	heap->ph_root = merge_children(heap, children);
	if (heap->ph_root)
	{
		heap->ph_root->prev_or_parent = NULL;
		heap->ph_root->next_sibling = NULL;
	}

	return result;
}

void
pairingheap_remove(pairingheap *heap, pairingheap_node *node)
{
	pairingheap_node *children;
	pairingheap_node *replacement;
	pairingheap_node *next_sibling;
	pairingheap_node **prev_ptr;

	if (node == heap->ph_root)
	{
		(void) pairingheap_remove_first(heap);
		return;
	}

	children = node->first_child;
	next_sibling = node->next_sibling;

	if (node->prev_or_parent->first_child == node)
		prev_ptr = &node->prev_or_parent->first_child;
	else
		prev_ptr = &node->prev_or_parent->next_sibling;
	Assert(*prev_ptr == node);

	if (children)
	{
		replacement = merge_children(heap, children);
		replacement->prev_or_parent = node->prev_or_parent;
		replacement->next_sibling = node->next_sibling;
		*prev_ptr = replacement;
		if (next_sibling)
			next_sibling->prev_or_parent = replacement;
	}
	else
	{
		*prev_ptr = next_sibling;
		if (next_sibling)
			next_sibling->prev_or_parent = node->prev_or_parent;
	}
}

static pairingheap_node *
merge_children(pairingheap *heap, pairingheap_node *children)
{
	pairingheap_node *curr;
	pairingheap_node *next;
	pairingheap_node *pairs;
	pairingheap_node *newroot;

	if (children == NULL || children->next_sibling == NULL)
		return children;

	next = children;
	pairs = NULL;
	for (;;)
	{
		curr = next;

		if (curr == NULL)
			break;

		if (curr->next_sibling == NULL)
		{
			curr->next_sibling = pairs;
			pairs = curr;
			break;
		}

		next = curr->next_sibling->next_sibling;
		curr = merge(heap, curr, curr->next_sibling);
		curr->next_sibling = pairs;
		pairs = curr;
	}

	newroot = pairs;
	next = pairs->next_sibling;
	while (next)
	{
		curr = next;
		next = curr->next_sibling;
		newroot = merge(heap, newroot, curr);
	}

	return newroot;
}
