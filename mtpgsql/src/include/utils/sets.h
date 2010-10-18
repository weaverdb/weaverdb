/*-------------------------------------------------------------------------
 *
 * sets.h
 *
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * $Id: sets.h,v 1.1.1.1 2006/08/12 00:22:28 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */
#ifndef SETS_H
#define SETS_H

/* Temporary name of set, before SetDefine changes it. */
#define GENERICSETNAME "zyxset"

PG_EXTERN Oid	SetDefine(char *querystr, char *typename);
PG_EXTERN int	seteval(Oid funcoid);

#endif	 /* SETS_H */
