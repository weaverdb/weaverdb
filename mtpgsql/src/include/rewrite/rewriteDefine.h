/*-------------------------------------------------------------------------
 *
 * rewriteDefine.h
 *
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * $Id: rewriteDefine.h,v 1.1.1.1 2006/08/12 00:22:23 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */
#ifndef REWRITEDEFINE_H
#define REWRITEDEFINE_H

#include "nodes/parsenodes.h"

PG_EXTERN void DefineQueryRewrite(RuleStmt *args);

#endif	 /* REWRITEDEFINE_H */
