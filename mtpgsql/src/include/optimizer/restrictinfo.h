/*-------------------------------------------------------------------------
 *
 * restrictinfo.h
 *	  prototypes for restrictinfo.c.
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 *
 *-------------------------------------------------------------------------
 */
#ifndef RESTRICTINFO_H
#define RESTRICTINFO_H

#include "nodes/relation.h"

PG_EXTERN bool restriction_is_or_clause(RestrictInfo *restrictinfo);
PG_EXTERN List *get_actual_clauses(List *restrictinfo_list);

#endif	 /* RESTRICTINFO_H */
