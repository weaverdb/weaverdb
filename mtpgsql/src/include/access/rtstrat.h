/*-------------------------------------------------------------------------
 *
 * rtstrat.h
 *	  routines defined in access/rtree/rtstrat.c
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * $Id: rtstrat.h,v 1.1.1.1 2006/08/12 00:22:10 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */
#ifndef RTSTRAT_H
#define RTSTRAT_H

#include "utils/rel.h"

/* extern */ RegProcedure RTMapOperator(Relation r, AttrNumber attnum,
			  RegProcedure proc);

#endif	 /* RTSTRAT_H */
