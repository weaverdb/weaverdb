/*-------------------------------------------------------------------------
 *
 * dllist.c
 *	  this is a simple doubly linked list implementation
 *	  replaces the old simplelists stuff
 *	  the elements of the lists are void*
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  $Header: /cvs/weaver/mtpgsql/src/backend/lib/dllistmutex.c,v 1.1.1.1 2006/08/12 00:20:36 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */
#include <thread.h>

#include "postgres.h"
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

	l = malloc(sizeof(Dllist));
	l->dll_head = 0;
	l->dll_tail = 0;
	mutex_init(&l->c_lock,USYNC_THREAD,NULL);
	
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
		free(curr);

	mutex_destroy(&l->c_lock);
	free(l);
}

Dlelem *
DLNewElem(void *val)
{
	Dlelem	   *e;
	
	if ( val == NULL ) return NULL;
	
	e = malloc(sizeof(Dlelem));
	e->dle_next = 0;
	e->dle_prev = 0;
	e->dle_val = val;
	e->dle_list = 0;
	return e;
}

void
DLFreeElem(Dlelem *e)
{
	free(e);
}

Dlelem *
DLGetHead(Dllist *l)
{
	Dlelem* item = 0;
	if ( l == NULL ) return item;
	mutex_lock(&l->c_lock);
	item = l->dll_head;
	mutex_unlock(&l->c_lock);
	return item;
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
	Dlelem* item = 0;
	if ( l == NULL ) return item;
	mutex_lock(&l->c_lock);
	item = l->dll_tail;
	mutex_unlock(&l->c_lock);
	return item;
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
	Dlelem* item = 0;
	if ( e == NULL ) return item;
	mutex_lock(&e->dle_list->c_lock);
	item = e->dle_prev;
	mutex_unlock(&e->dle_list->c_lock);
	return item;
}

Dlelem *
DLGetSucc(Dlelem *e)			/* get successor */
{
	Dlelem* item = 0;
	if ( e == NULL ) return item;
	mutex_lock(&e->dle_list->c_lock);
	item = e->dle_next;
	mutex_unlock(&e->dle_list->c_lock);
	return item;
}

void
DLRemove(Dlelem *e)
{
	Dllist	   *l = e->dle_list;
	mutex_lock(&l->c_lock);
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
	mutex_unlock(&l->c_lock);

	e->dle_next = 0;
	e->dle_prev = 0;
	e->dle_list = 0;
}

void
DLAddHead(Dllist *l, Dlelem *e)
{
	mutex_lock(&l->c_lock);
	e->dle_list = l;

	if (l->dll_head)
		l->dll_head->dle_prev = e;
	e->dle_next = l->dll_head;
	e->dle_prev = 0;
	l->dll_head = e;

	if (l->dll_tail == 0)		/* if this is first element added */
		l->dll_tail = e;
	mutex_unlock(&l->c_lock);
}

void
DLAddTail(Dllist *l, Dlelem *e)
{
	mutex_lock(&l->c_lock);
	e->dle_list = l;

	if (l->dll_tail)
		l->dll_tail->dle_next = e;
	e->dle_prev = l->dll_tail;
	e->dle_next = 0;
	l->dll_tail = e;

	if (l->dll_head == 0)		/* if this is first element added */
		l->dll_head = e;
	mutex_unlock(&l->c_lock);
}

Dlelem *
DLRemHead(Dllist *l)
{
	Dlelem	   *result;
	
	mutex_lock(&l->c_lock);
	/* remove and return the head */
	result = l->dll_head;

	if (result == 0) {
		mutex_unlock(&l->c_lock);
		return result;
	}

	if (result->dle_next)
		result->dle_next->dle_prev = 0;

	l->dll_head = result->dle_next;

	result->dle_next = 0;
	result->dle_list = 0;

	if (result == l->dll_tail)	/* if the head is also the tail */
		l->dll_tail = 0;

	mutex_unlock(&l->c_lock);
	return result;
}

Dlelem *
DLRemTail(Dllist *l)
{
	Dlelem	   *result;
	
	mutex_lock(&l->c_lock);
	/* remove and return the tail */
	result = l->dll_tail;

	if (result == 0) {
		mutex_unlock(&l->c_lock);
		return result;
	}
	
	if (result->dle_prev)
		result->dle_prev->dle_next = 0;

	l->dll_tail = result->dle_prev;

	result->dle_prev = 0;
	result->dle_list = 0;

	if (result == l->dll_head)	/* if the tail is also the head */
		l->dll_head = 0;

	mutex_unlock(&l->c_lock);
	return result;
}

/* Same as DLRemove followed by DLAddHead, but faster */
void
DLMoveToFront(Dlelem *e)
{
	Dllist	   *l;
	
	l = e->dle_list;
	mutex_lock(&l->c_lock);

	if (l->dll_head == e) {
		mutex_unlock(&l->c_lock);
		return;		
	}			/* Fast path if already at front */

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
	mutex_unlock(&l->c_lock);
	/* We need not check dll_tail, since there must have been > 1 entry */
}
