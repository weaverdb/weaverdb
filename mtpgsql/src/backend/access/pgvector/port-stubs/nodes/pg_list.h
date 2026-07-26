#ifndef PGVECTOR_PG_LIST_H
#define PGVECTOR_PG_LIST_H

#include "../../../include/nodes/pg_list.h"

typedef List ListCell;

#undef foreach
#define foreach(lc, list) \
	for ((lc) = (list); (lc) != NIL; (lc) = lnext(lc))

#define list_copy(l)			listCopy(l)
#define linitial(l)				((l) == NIL ? NULL : lfirst(l))

typedef int (*pgvector_list_sort_cmp) (const ListCell *a, const ListCell *b);

static inline List *
list_sort(List *list, pgvector_list_sort_cmp cmp)
{
	long		n;
	void	  **items;
	ListCell   *lc;
	long		i,
				j;

	if (list == NIL)
		return NIL;
	n = length(list);
	if (n <= 1)
		return list;

	items = (void **) palloc(sizeof(void *) * n);
	i = 0;
	foreach(lc, list)
		items[i++] = lfirst(lc);

	for (i = 0; i < n - 1; i++)
	{
		for (j = i + 1; j < n; j++)
		{
			ListCell	a,
						b;

			memset(&a, 0, sizeof(a));
			memset(&b, 0, sizeof(b));
			a.elem.ptr_value = items[i];
			b.elem.ptr_value = items[j];
			if (cmp(&a, &b) > 0)
			{
				void	   *tmp = items[i];

				items[i] = items[j];
				items[j] = tmp;
			}
		}
	}

	freeList(list);
	list = NIL;
	for (i = 0; i < n; i++)
		list = lappend(list, items[i]);
	pfree(items);
	return list;
}

#define list_length(l)			length(l)

static inline List *
list_make1_impl(void *datum)
{
	return lcons(datum, NIL);
}

#define list_make1(datum)		list_make1_impl((void *) (datum))

static inline void *
llast(List *list)
{
	long		n = length(list);

	if (n <= 0)
		return NULL;
	return nth(n - 1, list);
}

static inline List *
list_delete_last(List *list)
{
	long		n = length(list);

	if (n <= 0)
		return NIL;
	if (n == 1)
		return NIL;
	return ltruncate(n - 1, list);
}

#endif /* PGVECTOR_PG_LIST_H */
