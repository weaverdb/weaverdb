/*-------------------------------------------------------------------------
 *
 * prep.h
 *	  prototypes for files in optimizer/prep/
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 *
 *-------------------------------------------------------------------------
 */
#ifndef PREP_H
#define PREP_H

#include "nodes/parsenodes.h"
#include "nodes/plannodes.h"

/*
 * prototypes for prepqual.c
 */
PG_EXTERN List *canonicalize_qual(Expr *qual, bool removeAndFlag);
PG_EXTERN List *cnfify(Expr *qual, bool removeAndFlag);
PG_EXTERN Expr *dnfify(Expr *qual);

/*
 * prototypes for preptlist.c
 */
PG_EXTERN List *preprocess_targetlist(List *tlist, int command_type,
					  Index result_relation, List *range_table);

/*
 * prototypes for prepunion.c
 */
PG_EXTERN List *find_all_inheritors(Oid parentrel);
PG_EXTERN int	first_inherit_rt_entry(List *rangetable);
PG_EXTERN Append *plan_union_queries(Query *parse);
PG_EXTERN Append *plan_inherit_queries(Query *parse, List *tlist, Index rt_index);

#endif	 /* PREP_H */
