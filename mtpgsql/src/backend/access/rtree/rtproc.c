/*-------------------------------------------------------------------------
 *
 * rtproc.c
 *	  pg_amproc entries for rtrees.
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  $Header: /cvs/weaver/mtpgsql/src/backend/access/rtree/rtproc.c,v 1.1.1.1 2006/08/12 00:20:04 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"
#include "env/env.h"
#include "utils/builtins.h"


BOX
		   *
rt_rect_union(BOX *a, BOX *b)
{
	BOX		   *n;

	if ((n = (BOX *) palloc(sizeof(*n))) == (BOX *) NULL)
		elog(ERROR, "Cannot allocate box for union");

	n->higH.x = Max(a->higH.x, b->higH.x);
	n->higH.y = Max(a->higH.y, b->higH.y);
	n->loW.x = Min(a->loW.x, b->loW.x);
	n->loW.y = Min(a->loW.y, b->loW.y);

	return n;
}

BOX *
rt_rect_inter(BOX *a, BOX *b)
{
	BOX		   *n;

	if ((n = (BOX *) palloc(sizeof(*n))) == (BOX *) NULL)
		elog(ERROR, "Cannot allocate box for union");

	n->higH.x = Min(a->higH.x, b->higH.x);
	n->higH.y = Min(a->higH.y, b->higH.y);
	n->loW.x = Max(a->loW.x, b->loW.x);
	n->loW.y = Max(a->loW.y, b->loW.y);

	if (n->higH.x < n->loW.x || n->higH.y < n->loW.y)
	{
		pfree(n);
		return (BOX *) NULL;
	}

	return n;
}

void
rt_rect_size(BOX *a, float *size)
{
	if (a == (BOX *) NULL || a->higH.x <= a->loW.x || a->higH.y <= a->loW.y)
		*size = 0.0;
	else
		*size = (float) ((a->higH.x - a->loW.x) * (a->higH.y - a->loW.y));

	return;
}

/*
 *	rt_bigbox_size() -- Compute a size for big boxes.
 *
 *		In an earlier release of the system, this routine did something
 *		different from rt_box_size.  We now use floats, rather than ints,
 *		as the return type for the size routine, so we no longer need to
 *		have a special return type for big boxes.
 */
void
rt_bigbox_size(BOX *a, float *size)
{
	rt_rect_size(a, size);
}

POLYGON    *
rt_poly_union(POLYGON *a, POLYGON *b)
{
	POLYGON    *p;

	p = (POLYGON *) palloc(sizeof(POLYGON));

	if (!PointerIsValid(p))
		elog(ERROR, "Cannot allocate polygon for union");

	MemSet((char *) p, 0, sizeof(POLYGON));		/* zero any holes */
	p->size = sizeof(POLYGON);
	p->npts = 0;
	p->boundbox.higH.x = Max(a->boundbox.higH.x, b->boundbox.higH.x);
	p->boundbox.higH.y = Max(a->boundbox.higH.y, b->boundbox.higH.y);
	p->boundbox.loW.x = Min(a->boundbox.loW.x, b->boundbox.loW.x);
	p->boundbox.loW.y = Min(a->boundbox.loW.y, b->boundbox.loW.y);
	return p;
}

void
rt_poly_size(POLYGON *a, float *size)
{
	double		xdim,
				ydim;

	size = (float *) palloc(sizeof(float));
	if (a == (POLYGON *) NULL ||
		a->boundbox.higH.x <= a->boundbox.loW.x ||
		a->boundbox.higH.y <= a->boundbox.loW.y)
		*size = 0.0;
	else
	{
		xdim = (a->boundbox.higH.x - a->boundbox.loW.x);
		ydim = (a->boundbox.higH.y - a->boundbox.loW.y);

		*size = (float) (xdim * ydim);
	}

	return;
}

POLYGON    *
rt_poly_inter(POLYGON *a, POLYGON *b)
{
	POLYGON    *p;

	p = (POLYGON *) palloc(sizeof(POLYGON));

	if (!PointerIsValid(p))
		elog(ERROR, "Cannot allocate polygon for intersection");

	MemSet((char *) p, 0, sizeof(POLYGON));		/* zero any holes */
	p->size = sizeof(POLYGON);
	p->npts = 0;
	p->boundbox.higH.x = Min(a->boundbox.higH.x, b->boundbox.higH.x);
	p->boundbox.higH.y = Min(a->boundbox.higH.y, b->boundbox.higH.y);
	p->boundbox.loW.x = Max(a->boundbox.loW.x, b->boundbox.loW.x);
	p->boundbox.loW.y = Max(a->boundbox.loW.y, b->boundbox.loW.y);

	if (p->boundbox.higH.x < p->boundbox.loW.x || p->boundbox.higH.y < p->boundbox.loW.y)
	{
		pfree(p);
		return (POLYGON *) NULL;
	}

	return p;
}
