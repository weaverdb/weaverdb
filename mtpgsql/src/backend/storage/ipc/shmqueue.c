/*-------------------------------------------------------------------------
 *
 * shmqueue.c
 *	  shared memory linked lists
 *
 * Portions Copyright (c) 2000-2024, Myron Scott  <myron@weaverdb.org>
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *
 *
 * NOTES
 *
 * Package for managing doubly-linked lists in shared memory.
 * The only tricky thing is that SHM_QUEUE will usually be a field
 * in a larger record.	SHMQueueGetFirst has to return a pointer
 * to the record itself instead of a pointer to the SHMQueue field
 * of the record.  It takes an extra pointer and does some extra
 * pointer arithmetic to do this correctly.
 *
 * NOTE: These are set up so they can be turned into macros some day.
 *
 *-------------------------------------------------------------------------
 */

#include <pthread.h>

#include "postgres.h"
#include "storage/shmem.h"

/*#define SHMQUEUE_DEBUG*/
#ifdef SHMQUEUE_DEBUG
#define SHMQUEUE_DEBUG_DEL		/* deletions */
#define SHMQUEUE_DEBUG_HD		/* head inserts */
#define SHMQUEUE_DEBUG_TL		/* tail inserts */
#define SHMQUEUE_DEBUG_ELOG NOTICE
#endif	 /* SHMQUEUE_DEBUG */

/*
 * ShmemQueueInit -- make the head of a new queue point
 *		to itself
 */
void
SHMQueueInit(SHM_QUEUE *queue, pthread_mutex_t* lock)
{
	Assert(SHM_PTR_VALID(queue));
	(queue)->prev = (queue)->next = MAKE_OFFSET(queue);
        (queue)->lock = lock;
}

/*
 * SHMQueueElemInit -- clear an element's links
 */
void
SHMQueueElemInit(SHM_QUEUE *queue)
{
	Assert(SHM_PTR_VALID(queue));
	(queue)->prev = (queue)->next = INVALID_OFFSET;
        (queue)->lock = NULL;
}

/*
 * SHMQueueDelete -- remove an element from the queue and
 *		close the links
 */
void
SHMQueueDelete(SHM_QUEUE *queue)
{
	SHM_QUEUE  *nextElem = (SHM_QUEUE *) MAKE_PTR((queue)->next);
	SHM_QUEUE  *prevElem = (SHM_QUEUE *) MAKE_PTR((queue)->prev);

	Assert(SHM_PTR_VALID(queue));
	Assert(SHM_PTR_VALID(nextElem));
	Assert(SHM_PTR_VALID(prevElem));

#ifdef SHMQUEUE_DEBUG_DEL
	dumpQ(queue, "in SHMQueueDelete: begin");
#endif	 /* SHMQUEUE_DEBUG_DEL */

	prevElem->next = (queue)->next;
	nextElem->prev = (queue)->prev;

#ifdef SHMQUEUE_DEBUG_DEL
	dumpQ((SHM_QUEUE *) MAKE_PTR(queue->prev), "in SHMQueueDelete: end");
#endif	 /* SHMQUEUE_DEBUG_DEL */
}

#ifdef SHMQUEUE_DEBUG
void
dumpQ(SHM_QUEUE *q, char *s)
{
	char		elem[NAMEDATALEN];
	char		buf[1024];
	SHM_QUEUE  *start = q;
	int			count = 0;

	sprintf(buf, "q prevs: %x", MAKE_OFFSET(q));
	q = (SHM_QUEUE *) MAKE_PTR(q->prev);
	while (q != start)
	{
		sprintf(elem, "--->%x", MAKE_OFFSET(q));
		strcat(buf, elem);
		q = (SHM_QUEUE *) MAKE_PTR(q->prev);
		if (q->prev == MAKE_OFFSET(q))
			break;
		if (count++ > 40)
		{
			strcat(buf, "BAD PREV QUEUE!!");
			break;
		}
	}
	sprintf(elem, "--->%x", MAKE_OFFSET(q));
	strcat(buf, elem);
	elog(SHMQUEUE_DEBUG_ELOG, "%s: %s", s, buf);

	sprintf(buf, "q nexts: %x", MAKE_OFFSET(q));
	count = 0;
	q = (SHM_QUEUE *) MAKE_PTR(q->next);
	while (q != start)
	{
		sprintf(elem, "--->%x", MAKE_OFFSET(q));
		strcat(buf, elem);
		q = (SHM_QUEUE *) MAKE_PTR(q->next);
		if (q->next == MAKE_OFFSET(q))
			break;
		if (count++ > 10)
		{
			strcat(buf, "BAD NEXT QUEUE!!");
			break;
		}
	}
	sprintf(elem, "--->%x", MAKE_OFFSET(q));
	strcat(buf, elem);
	elog(SHMQUEUE_DEBUG_ELOG, "%s: %s", s, buf);
}

#endif	 /* SHMQUEUE_DEBUG */

void
SHMQueueInsertTL(SHM_QUEUE *queue, SHM_QUEUE *elem)
{
	SHM_QUEUE  *nextPtr = (SHM_QUEUE *) MAKE_PTR((queue)->next);
	SHMEM_OFFSET elemOffset = MAKE_OFFSET(elem);

	(elem)->prev = nextPtr->prev;
	(elem)->next = queue->next;
	(queue)->next = elemOffset;
	nextPtr->prev = elemOffset;

        (elem)->lock = (queue)->lock;
}

/*
 * SHMQueueFirst -- Get the first element from a queue
 *
 * First element is queue->next.  If SHMQueue is part of
 * a larger structure, we want to return a pointer to the
 * whole structure rather than a pointer to its SHMQueue field.
 * I.E. struct {
 *		int				stuff;
 *		SHMQueue		elem;
 * } ELEMType;
 * when this element is in a queue (queue->next) is struct.elem.
 * nextQueue allows us to calculate the offset of the SHMQueue
 * field in the structure.
 *
 * call to SHMQueueFirst should take these parameters:
 *
 *	 &(queueHead),&firstElem,&(firstElem->next)
 *
 * Note that firstElem may well be uninitialized.  if firstElem
 * is initially K, &(firstElem->next) will be K+ the offset to
 * next.
 */
void
SHMQueueFirst(SHM_QUEUE *queue, Pointer *nextPtrPtr, SHM_QUEUE *nextQueue)
{
	SHM_QUEUE  *elemPtr = (SHM_QUEUE *) MAKE_PTR((queue)->next);

	Assert(SHM_PTR_VALID(queue));
	*nextPtrPtr = (Pointer) (((unsigned long) *nextPtrPtr) +
				((unsigned long) elemPtr) - ((unsigned long) nextQueue));

	/*
	 * nextPtrPtr a ptr to a structure linked in the queue nextQueue is
	 * the SHMQueue field of the structure nextPtrPtr - nextQueue is 0
	 * minus the offset of the queue field n the record elemPtr +
	 * (*nextPtrPtr - nexQueue) is the start of the structure containing
	 * elemPtr.
	 */
}

/*
 * SHMQueueEmpty -- TRUE if queue head is only element, FALSE otherwise
 */
bool
SHMQueueEmpty(SHM_QUEUE *queue)
{
	Assert(SHM_PTR_VALID(queue));

	if (queue->prev == MAKE_OFFSET(queue))
	{
		Assert(queue->next = MAKE_OFFSET(queue));
		return TRUE;
	}
	return FALSE;
}

int
SHMQueueLock(SHM_QUEUE *queue) {
    return pthread_mutex_lock(queue->lock);
}

int
SHMQueueRelease(SHM_QUEUE *queue) {
    return pthread_mutex_unlock(queue->lock);
}
