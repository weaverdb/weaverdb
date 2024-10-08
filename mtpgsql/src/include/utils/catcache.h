/*-------------------------------------------------------------------------
 *
 * catcache.h
 *	  Low-level catalog cache definitions.
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
#ifndef CATCACHE_H
#define CATCACHE_H

/* #define		CACHEDEBUG		 turns DEBUG elogs on */

#include "access/htup.h"
#include "lib/dllist.h"

/*
 *		struct catctup:			tuples in the cache.
 *		struct catcache:		information for managing a cache.
 */

typedef struct catctup
{
	HeapTuple	ct_tup;			/* A pointer to a tuple			*/
	/*
	 * Each tuple in the cache has two catctup items, one in the LRU list
	 * and one in the hashbucket list for its hash value.  ct_node in each
	 * one points to the other one.
	 */
	Dlelem	   *ct_node;		/* the other catctup for this tuple */
        int         refcount;
} CatCTup;

/* voodoo constants */
#define NCCBUCK 500				/* CatCache buckets */
#define MAXTUP  900				/* Maximum # of tuples cached per cache */

typedef uint32 (*CCHashFunc) (Datum);

typedef struct catcache
{
	Oid			relationId;
	Oid			indexId;
	char	   *cc_relname;		/* relation name for defered open */
	char	   *cc_indname;		/* index name for defered open */
	HeapTuple	(*cc_iscanfunc) (Relation, ...);		/* index scanfunction */
	TupleDesc	cc_tupdesc;		/* tuple descriptor from reldesc */
	int			id;				/* XXX could be improved -hirohama */
	bool		busy;			/* for detecting recursive lookups */
	short		cc_ntup;		/* # of tuples in this cache	*/
	short		cc_maxtup;		/* max # of tuples allowed (LRU) */
	short		cc_nkeys;
	short		cc_size;
	short		cc_key[4];		/* AttrNumber of each key */
	CCHashFunc	cc_hashfunc[4]; /* hash function to use for each key */
	ScanKeyData cc_skey[4];
	struct catcache *cc_next;
	Dllist	   *cc_lrulist;		/* LRU list, most recent first */
	Dllist	   *cc_cache[NCCBUCK + 1];	/* hash buckets */
        MemoryContext   cachecxt;
} CatCache;

#define InvalidCatalogCacheId	(-1)

PG_EXTERN void CatalogCacheIdInvalidate(int cacheId, Index hashIndex,
						 ItemPointer pointer);
PG_EXTERN void ResetSystemCache(void);
PG_EXTERN void ResetCatalogCacheMemory(void);

PG_EXTERN CatCache *InitSysCache(char *relname, char *indname, int id, int nkeys,
			 int *key, HeapTuple (*iScanfuncP) (Relation, ...));
PG_EXTERN HeapTuple SearchSysCache(struct catcache * cache, Datum v1, Datum v2,
			   Datum v3, Datum v4);
PG_EXTERN void RelationInvalidateCatalogCacheTuple(Relation relation,
									HeapTuple tuple, void (*function) ());

#endif	 /* CATCACHE_H */
