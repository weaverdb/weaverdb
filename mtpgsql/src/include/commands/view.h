/*-------------------------------------------------------------------------
 *
 * view.h
 *
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * $Id: view.h,v 1.1.1.1 2006/08/12 00:22:17 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */
#ifndef VIEW_H
#define VIEW_H

#include "nodes/parsenodes.h"

PG_EXTERN char *MakeRetrieveViewRuleName(char *view_name);
PG_EXTERN void DefineView(char *view_name, Query *view_parse);
PG_EXTERN void RemoveView(char *view_name);

#endif	 /* VIEW_H */
