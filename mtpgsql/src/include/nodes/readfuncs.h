/*-------------------------------------------------------------------------
 *
 * readfuncs.h
 *	  header file for read.c and readfuncs.c. These functions are internal
 *	  to the stringToNode interface and should not be used by anyone else.
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * $Id: readfuncs.h,v 1.1.1.1 2006/08/12 00:22:21 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */
#ifndef READFUNCS_H
#define READFUNCS_H

#include "nodes/nodes.h"

/*
 * prototypes for functions in read.c (the lisp token parser)
 */
PG_EXTERN char *lsptok(char *string, int *length);
PG_EXTERN char *debackslash(char *token, int length);
PG_EXTERN void *nodeRead(bool read_car_only);

/*
 * prototypes for functions in readfuncs.c
 */
PG_EXTERN Node *parsePlanString(void);

#endif	 /* READFUNCS_H */
