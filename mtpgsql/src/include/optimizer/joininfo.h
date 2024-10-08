/*-------------------------------------------------------------------------
 *
 * joininfo.h
 *	  prototypes for joininfo.c.
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 *
 *-------------------------------------------------------------------------
 */
#ifndef JOININFO_H
#define JOININFO_H

#include "nodes/relation.h"

PG_EXTERN JoinInfo *joininfo_member(List *join_relids, List *joininfo_list);
PG_EXTERN JoinInfo *find_joininfo_node(RelOptInfo *this_rel, List *join_relids);

#endif	 /* JOININFO_H */
