/*-------------------------------------------------------------------------
 *
 * geqo_misc.h
 *	  prototypes for printout routines in optimizer/geqo
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * $Id: geqo_misc.h,v 1.1.1.1 2006/08/12 00:22:21 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */

/* contributed by:
   =*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=
   *  Martin Utesch				 * Institute of Automatic Control	   *
   =							 = University of Mining and Technology =
   *  utesch@aut.tu-freiberg.de  * Freiberg, Germany				   *
   =*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=
 */

#ifndef GEQO_MISC_H
#define GEQO_MISC_H

#include "optimizer/geqo_recombination.h"
#include "nodes/relation.h"

PG_EXTERN void print_pool(FILE *fp, Pool *pool, int start, int stop);
PG_EXTERN void print_gen(FILE *fp, Pool *pool, int generation);
PG_EXTERN void print_edge_table(FILE *fp, Edge *edge_table, int num_gene);

PG_EXTERN void geqo_print_rel(Query *root, RelOptInfo *rel);
PG_EXTERN void geqo_print_path(Query *root, Path *path, int indent);
PG_EXTERN void geqo_print_joinclauses(Query *root, List *clauses);

#endif	 /* GEQO_MISC_H */
