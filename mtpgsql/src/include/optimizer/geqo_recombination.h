/*-------------------------------------------------------------------------
 *
 * geqo_recombination.h
 *	  prototypes for recombination in the genetic query optimizer
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * $Id: geqo_recombination.h,v 1.1.1.1 2006/08/12 00:22:21 synmscott Exp $
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

/* -- parts of this are adapted from D. Whitley's Genitor algorithm -- */

#ifndef GEQO_RECOMBINATION_H
#define GEQO_RECOMBINATION_H

#include "optimizer/geqo_gene.h"

PG_EXTERN void init_tour(Gene *tour, int num_gene);


/* edge recombination crossover [ERX] */

typedef struct Edge
{
	Gene		edge_list[4];	/* list of edges */
	int			total_edges;
	int			unused_edges;
} Edge;

PG_EXTERN Edge *alloc_edge_table(int num_gene);
PG_EXTERN void free_edge_table(Edge *edge_table);

PG_EXTERN float gimme_edge_table(Gene *tour1, Gene *tour2, int num_gene, Edge *edge_table);

PG_EXTERN int	gimme_tour(Edge *edge_table, Gene *new_gene, int num_gene);


/* partially matched crossover [PMX] */

#define DAD 1					/* indicator for gene from dad */
#define MOM 0					/* indicator for gene from mom */

PG_EXTERN void pmx(Gene *tour1, Gene *tour2, Gene *offspring, int num_gene);


typedef struct City
{
	int			tour2_position;
	int			tour1_position;
	int			used;
	int			select_list;
} City;

PG_EXTERN City *alloc_city_table(int num_gene);
PG_EXTERN void free_city_table(City *city_table);

/* cycle crossover [CX] */
PG_EXTERN int	cx(Gene *tour1, Gene *tour2, Gene *offspring, int num_gene, City *city_table);

/* position crossover [PX] */
PG_EXTERN void px(Gene *tour1, Gene *tour2, Gene *offspring, int num_gene, City *city_table);

/* order crossover [OX1] according to Davis */
PG_EXTERN void ox1(Gene *mom, Gene *dad, Gene *offspring, int num_gene, City *city_table);

/* order crossover [OX2] according to Syswerda */
PG_EXTERN void ox2(Gene *mom, Gene *dad, Gene *offspring, int num_gene, City *city_table);


#endif	 /* GEQO_RECOMBINATION_H */
