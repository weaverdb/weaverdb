/*-------------------------------------------------------------------------
 *
 * plancat.h
 *	  prototypes for plancat.c.
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * $Id: plancat.h,v 1.1.1.1 2006/08/12 00:22:22 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */
#ifndef PLANCAT_H
#define PLANCAT_H

#include "nodes/relation.h"


PG_EXTERN void relation_info(Query *root, Index relid,
			  bool *hasindex, long *pages, double *tuples,Size* min,Size* max,Size* ave);

PG_EXTERN List *find_secondary_indexes(Query *root, Index relid);

PG_EXTERN List *find_inheritance_children(Oid inhparent);

PG_EXTERN Selectivity restriction_selectivity(Oid functionObjectId,
						Oid operatorObjectId,
						Oid relationObjectId,
						AttrNumber attributeNumber,
						Datum constValue,
						int constFlag);

PG_EXTERN Selectivity join_selectivity(Oid functionObjectId, Oid operatorObjectId,
				 Oid relationObjectId1, AttrNumber attributeNumber1,
				 Oid relationObjectId2, AttrNumber attributeNumber2);

#endif	 /* PLANCAT_H */
