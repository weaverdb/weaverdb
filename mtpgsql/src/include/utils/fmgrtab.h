/*-------------------------------------------------------------------------
 *
 * fmgrtab.h
 *
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * $Id: fmgrtab.h,v 1.1.1.1 2006/08/12 00:22:26 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */
#ifndef FMGRTAB_H
#define FMGRTAB_H


typedef struct
{
	Oid			proid;
	int			nargs;
	func_ptr	func;
	char	   *funcName;
} FmgrCall;

PG_EXTERN FmgrCall *fmgr_isbuiltin(Oid id);
PG_EXTERN func_ptr fmgr_lookupByName(char *name);
PG_EXTERN void load_file(char *filename);

#endif	 /* FMGRTAB_H */
