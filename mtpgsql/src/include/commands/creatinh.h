/*-------------------------------------------------------------------------
 *
 * creatinh.h
 *	  prototypes for creatinh.c.
 *
 *
 * Portions Copyright (c) 2000-2024, Myron Scott  <myron@weaverdb.org>
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 *
 *-------------------------------------------------------------------------
 */
#ifndef CREATINH_H
#define CREATINH_H

#include "nodes/parsenodes.h"

PG_EXTERN void DefineRelation(CreateStmt *stmt, char relkind);
PG_EXTERN void RemoveRelation(char *name);
PG_EXTERN void TruncateRelation(char *name);

#endif	 /* CREATINH_H */
