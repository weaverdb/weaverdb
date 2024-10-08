/*-------------------------------------------------------------------------
 *
 * dllist.c
 *	  this is a simple doubly linked list implementation
 *	  replaces the old simplelists stuff
 *	  the elements of the lists are void*
 *
 * Portions Copyright (c) 2000-2024, Myron Scott  <myron@weaverdb.org>
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"
#ifndef FRONTEND
#include "env/env.h"
#else
#undef palloc
#undef pfree
#define pfree free
#define palloc malloc
#endif
#include "utils/palloc.h"
#include "lib/dllist.h"

/* When this file is compiled for inclusion in libpq,
 * it can't use assert checking.  Probably this fix ought to be
 * in c.h or somewhere like that...
 */
#ifdef FRONTEND
#undef Assert
#define Assert(condition)
#endif


Dllist *
DLNewList(void)
{
	Dllist	   *l;

	l = palloc(sizeof(Dllist));
	l->dll_head = 0;
	l->dll_tail = 0;

	return l;
}

/* free up a list and all the nodes in it --- but *not* whatever the nodes
 * might point to!
 */
void
DLFreeList(Dllist *l)
{
	Dlelem	   *curr;

	while ((curr = DLRemHead(l)) != 0)
		pfree(curr);

	pfree(l);
}

Dlelem *
DLNewElem(void *val)
{
	Dlelem	   *e;

	e = palloc(sizeof(Dlelem));
	e->dle_next = 0;
	e->dle_prev = 0;
	e->dle_val = val;
	e->dle_list = 0;
	return e;
}

void
DLFreeElem(Dlelem *e)
{
	pfree(e);
}

Dlelem *
DLGetHead(Dllist *l)
{
	return l ? l->dll_head : 0;
}

/* get the value stored in the first element */
#ifdef NOT_USED
void *
DLGetHeadVal(Dllist *l)
{
	Dlelem	   *e = DLGetHead(l);

	return e ? e->dle_val : 0;
}

#endif

Dlelem *
DLGetTail(Dllist *l)
{
	return l ? l->dll_tail : 0;
}

/* get the value stored in the last element */
#ifdef NOT_USED
void *
DLGetTailVal(Dllist *l)
{
	Dlelem	   *e = DLGetTail(l);

	return e ? e->dle_val : 0;
}

#endif

Dlelem *
DLGetPred(Dlelem *e)			/* get predecessor */
{
	return e ? e->dle_prev : 0;
}

Dlelem *
DLGetSucc(Dlelem *e)			/* get successor */
{
	return e ? e->dle_next : 0;
}

void
DLRemove(Dlelem *e)
{
	Dllist	   *l = e->dle_list;

	if (e->dle_prev)
		e->dle_prev->dle_next = e->dle_next;
	else
/* must be the head element */
	{
		Assert(e == l->dll_head);
		l->dll_head = e->dle_next;
	}
	if (e->dle_next)
		e->dle_next->dle_prev = e->dle_prev;
	else
/* must be the tail element */
	{
		Assert(e == l->dll_tail);
		l->dll_tail = e->dle_prev;
	}

	e->dle_next = 0;
	e->dle_prev = 0;
	e->dle_list = 0;
}

void
DLAddHead(Dllist *l, Dlelem *e)
{
	e->dle_list = l;

	if (l->dll_head)
		l->dll_head->dle_prev = e;
	e->dle_next = l->dll_head;
	e->dle_prev = 0;
	l->dll_head = e;

	if (l->dll_tail == 0)		/* if this is first element added */
		l->dll_tail = e;
}

void
DLAddTail(Dllist *l, Dlelem *e)
{
	e->dle_list = l;

	if (l->dll_tail)
		l->dll_tail->dle_next = e;
	e->dle_prev = l->dll_tail;
	e->dle_next = 0;
	l->dll_tail = e;

	if (l->dll_head == 0)		/* if this is first element added */
		l->dll_head = e;
}

Dlelem *
DLRemHead(Dllist *l)
{
	/* remove and return the head */
	Dlelem	   *result = l->dll_head;

	if (result == 0)
		return result;

	if (result->dle_next)
		result->dle_next->dle_prev = 0;

	l->dll_head = result->dle_next;

	result->dle_next = 0;
	result->dle_list = 0;

	if (result == l->dll_tail)	/* if the head is also the tail */
		l->dll_tail = 0;

	return result;
}

Dlelem *
DLRemTail(Dllist *l)
{
	/* remove and return the tail */
	Dlelem	   *result = l->dll_tail;

	if (result == 0)
		return result;

	if (result->dle_prev)
		result->dle_prev->dle_next = 0;

	l->dll_tail = result->dle_prev;

	result->dle_prev = 0;
	result->dle_list = 0;

	if (result == l->dll_head)	/* if the tail is also the head */
		l->dll_head = 0;

	return result;
}

/* Same as DLRemove followed by DLAddHead, but faster */
void
DLMoveToFront(Dlelem *e)
{
	Dllist	   *l = e->dle_list;

	if (l->dll_head == e)
		return;					/* Fast path if already at front */

	Assert(e->dle_prev != 0);	/* since it's not the head */
	e->dle_prev->dle_next = e->dle_next;

	if (e->dle_next)
		e->dle_next->dle_prev = e->dle_prev;
	else
/* must be the tail element */
	{
		Assert(e == l->dll_tail);
		l->dll_tail = e->dle_prev;
	}

	l->dll_head->dle_prev = e;
	e->dle_next = l->dll_head;
	e->dle_prev = 0;
	l->dll_head = e;
	/* We need not check dll_tail, since there must have been > 1 entry */
}


#ifdef FRONTEND
#undef palloc
#undef pfree
#endif
