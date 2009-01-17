/*-------------------------------------------------------------------------
 *
 * geqo_pool.h
 *	  pool representation in optimizer/geqo
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * $Id: geqo_pool.h,v 1.1.1.1 2006/08/12 00:22:21 synmscott Exp $
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


#ifndef GEQO_POOL_H
#define GEQO_POOL_H

#include "optimizer/geqo_gene.h"
#include "nodes/parsenodes.h"

PG_EXTERN Pool *alloc_pool(int pool_size, int string_length);
PG_EXTERN void free_pool(Pool *pool);

PG_EXTERN void random_init_pool(Query *root, Pool *pool, int strt, int stop);
PG_EXTERN Chromosome *alloc_chromo(int string_length);
PG_EXTERN void free_chromo(Chromosome *chromo);

PG_EXTERN void spread_chromo(Chromosome *chromo, Pool *pool);

PG_EXTERN void sort_pool(Pool *pool);

#endif	 /* GEQO_POOL_H */
