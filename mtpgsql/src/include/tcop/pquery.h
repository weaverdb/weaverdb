/*-------------------------------------------------------------------------
 *
 * pquery.h
 *	  prototypes for pquery.c.
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * $Id: pquery.h,v 1.1.1.1 2006/08/12 00:22:25 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */
#ifndef PQUERY_H
#define PQUERY_H

#include "executor/execdesc.h"

/* moved to execdesc.h
extern QueryDesc *CreateQueryDesc(Query *parsetree, Plan *plantree,
								  CommandDest dest);

*/
#ifdef __cplusplus
extern "C" {
#endif

extern EState *CreateExecutorState(void);


extern void ProcessPortal(char *portalName, Query *parseTree,
			  Plan *plan, EState *state, TupleDesc attinfo,
			  CommandDest dest);

extern void ProcessQuery(Query *parsetree, Plan *plan, CommandDest dest);
#ifdef __cplusplus
}
#endif

#endif	 /* pqueryIncluded */
