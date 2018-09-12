/*-------------------------------------------------------------------------
 *
 * dynloader.h
 *	  dynamic loader for HP-UX using the shared library mechanism
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  $Header: /cvs/weaver/mtpgsql/src/backend/port/dynloader/hpux.h,v 1.1.1.1 2006/08/12 00:21:15 synmscott Exp $
 *
 *	NOTES
 *		all functions are defined here -- it's impossible to trace the
 *		shl_* routines from the bundled HP-UX debugger.
 *
 *-------------------------------------------------------------------------
 */
/* System includes */
void	   *pg_dlopen(char *filename);
func_ptr	pg_dlsym(void *handle, char *funcname);
void		pg_dlclose(void *handle);
char	   *pg_dlerror();
