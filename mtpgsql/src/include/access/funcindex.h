/*-------------------------------------------------------------------------
 *
 * funcindex.h
 *
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
#ifndef _FUNC_INDEX_INCLUDED_
#define _FUNC_INDEX_INCLUDED_

#include "postgres.h"

typedef struct
{
	int			nargs;
	Oid			arglist[FUNC_MAX_ARGS];
	Oid			procOid;
	NameData	funcName;
} FuncIndexInfo;

typedef FuncIndexInfo *FuncIndexInfoPtr;

/*
 * some marginally useful macro definitions
 */
/* #define FIgetname(FINFO) (&((FINFO)->funcName.data[0]))*/
#define FIgetname(FINFO) (FINFO)->funcName.data
#define FIgetnArgs(FINFO) (FINFO)->nargs
#define FIgetProcOid(FINFO) (FINFO)->procOid
#define FIgetArg(FINFO, argnum) (FINFO)->arglist[argnum]
#define FIgetArglist(FINFO) (FINFO)->arglist

#define FIsetnArgs(FINFO, numargs) ((FINFO)->nargs = numargs)
#define FIsetProcOid(FINFO, id) ((FINFO)->procOid = id)
#define FIsetArg(FINFO, argnum, argtype) ((FINFO)->arglist[argnum] = argtype)

#define FIisFunctionalIndex(FINFO) (FINFO->procOid != InvalidOid)

#endif	 /* FUNCINDEX_H */
