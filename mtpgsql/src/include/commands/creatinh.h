/*-------------------------------------------------------------------------
 *
 * creatinh.h
 *	  prototypes for creatinh.c.
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * $Id: creatinh.h,v 1.1.1.1 2006/08/12 00:22:17 synmscott Exp $
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
