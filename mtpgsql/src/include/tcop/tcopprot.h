/*-------------------------------------------------------------------------
 *
 * tcopprot.h
 *	  prototypes for postgres.c.
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * $Id: tcopprot.h,v 1.1.1.1 2006/08/12 00:22:25 synmscott Exp $
 *
 * OLD COMMENTS
 *	  This file was created so that other c files could get the two
 *	  function prototypes without having to include tcop.h which single
 *	  handedly includes the whole f*cking tree -- mer 5 Nov. 1991
 *
 *-------------------------------------------------------------------------
 */
#ifndef TCOPPROT_H
#define TCOPPROT_H

#include <setjmp.h>
#include "executor/execdesc.h"
#include "parser/parse_node.h"

extern DLLIMPORT sigjmp_buf Warn_restart;
extern bool Warn_restart_ready;
extern bool InError;
extern bool ExitAfterAbort;

#ifndef BOOTSTRAP_INCLUDE
#ifdef __cplusplus
extern "C" {
#endif

extern List *pg_parse_and_rewrite(char *query_string,
					 Oid *typev, 
					 char** argnames,
					 int nargs,
					 bool aclOverride);
extern Plan *pg_plan_query(Query *querytree);
extern void pg_exec_query_acl_override(char *query_string);
extern void pg_exec_query_dest(char *query_string,
				   CommandDest dest,
				   bool aclOverride);

#endif	 /* BOOTSTRAP_INCLUDE */

extern void handle_warn(SIGNAL_ARGS);
extern void quickdie(SIGNAL_ARGS);
extern void die(SIGNAL_ARGS);
extern void FloatExceptionHandler(SIGNAL_ARGS);
extern void CancelQuery(void);
extern int PostgresMain(int argc, char *argv[],
			 int real_argc, char *real_argv[]);
extern void ResetUsage(void);
extern void ShowUsage(void);
#ifdef __cplusplus
}
#endif

#endif	 /* TCOPPROT_H */
