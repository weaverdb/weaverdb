/*-------------------------------------------------------------------------
 *
 * syscache.h
 *	  System catalog cache definitions.
 *
 * See also lsyscache.h, which provides convenience routines for
 * common cache-lookup operations.
 *
 * Portions Copyright (c) 2000-2024, Myron Scott  <myron@weaverdb.org>
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 *
 *-------------------------------------------------------------------------
 */
#ifndef SYSCACHE_H
#define SYSCACHE_H

#include "access/htup.h"

 /* #define CACHEDEBUG *//* turns DEBUG elogs on */


/*
 *		Declarations for util/syscache.c.
 *
 *		SysCache identifiers.
 *
 *		The order of these must match the order
 *		they are entered into the structure cacheinfo[] in syscache.c
 *		Keep them in alphabeticall order.
 */

#define AGGNAME			0
#define AMNAME			1
#define AMOPOPID		2
#define AMOPSTRATEGY            3
#define ATTNAME			4
#define ATTNUM			5
#define CLADEFTYPE		6
#define CLANAME			7
#define EXTSTORE		8
#define GRONAME			9
#define GROSYSID		10
#define INDEXRELID		11
#define INHRELID		12
#define LANGNAME		13
#define LANGOID			14
#define LISTENREL		15
#define OPERNAME		16
#define OPEROID			17
#define PROCNAME		18
#define PROCOID			19
#define RELNAME			20
#define RELOID			21
#define RULENAME		22
#define RULEOID			23
#define SHADOWNAME		24
#define SHADOWSYSID		25
#define STATRELID		26
#define TYPENAME		27
#define TYPEOID			28

/* ----------------
 *		struct cachedesc:		information needed for a call to InitSysCache()
 * ----------------
 */
struct cachedesc
{
	char	   *name;			/* this is Name so that we can initialize
								 * it */
	int			nkeys;
	int			key[4];
	int			size;			/* sizeof(appropriate struct) */
	char	   *indname;		/* index relation for this cache, if
								 * exists */
	HeapTuple	(*iScanFunc) (Relation, ...);/* function to handle index scans */
};
#ifdef __cplusplus
extern "C" {
#endif

PG_EXTERN void zerocaches(void);
PG_EXTERN void InitCatalogCache(void);
PG_EXTERN HeapTuple SearchSysCacheTupleCopy(int cacheId,
						Datum key1, Datum key2, Datum key3, Datum key4);
PG_EXTERN HeapTuple SearchSysCacheTuple(int cacheId,
					Datum key1, Datum key2, Datum key3, Datum key4);
PG_EXTERN Datum SysCacheGetAttr(int cacheId, HeapTuple tup,
				AttrNumber attributeNumber,
				bool *isnull);
#ifdef __cplusplus
}
#endif

#endif	 /* SYSCACHE_H */
