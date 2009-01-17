/*-------------------------------------------------------------------------
 *
 * fcache2.h
 *
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * $Id: fcache2.h,v 1.1.1.1 2006/08/12 00:22:26 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */
#ifndef FCACHE2_H
#define FCACHE2_H

#include "nodes/execnodes.h"

PG_EXTERN void setFcache(Node *node, Oid foid, List *argList, ExprContext *econtext);

#endif	 /* FCACHE2_H */
