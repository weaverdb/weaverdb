/*-------------------------------------------------------------------------
 *
 * utility.h
 *	  prototypes for utility.c.
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
#ifndef UTILITY_H
#define UTILITY_H

#include "executor/execdesc.h"
#ifdef __cplusplus
extern "C" {
#endif

extern void ProcessUtility(Node *parsetree, CommandDest dest);

#ifdef __cplusplus
}
#endif

#endif	 /* UTILITY_H */
