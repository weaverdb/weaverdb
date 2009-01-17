/*-------------------------------------------------------------------------
 *
 * explain.h
 *	  prototypes for explain.c
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994-5, Regents of the University of California
 *
 * $Id: explain.h,v 1.1.1.1 2006/08/12 00:22:17 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */
#ifndef EXPLAIN_H
#define EXPLAIN_H

#include "nodes/parsenodes.h"
#include "tcop/dest.h"

PG_EXTERN void ExplainQuery(Query *query, bool verbose, CommandDest dest);

#endif	 /* EXPLAIN_H */
