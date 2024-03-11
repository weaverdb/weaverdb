/*-------------------------------------------------------------------------
 *
 * syscache.c
 *	  System cache management routines
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  $Header: /cvs/weaver/mtpgsql/src/backend/utils/cache/syscache.c,v 1.1.1.1 2006/08/12 00:21:58 synmscott Exp $
 *
 * NOTES
 *	  These routines allow the parser/planner/executor to perform
 *	  rapid lookups on the contents of the system catalogs.
 *
 *	  see catalog/syscache.h for a list of the cache id's
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"
#include "env/env.h"
#include "utils/builtins.h"
#include "access/heapam.h"
#include "catalog/catname.h"
#include "catalog/pg_aggregate.h"
#include "catalog/pg_amop.h"
#include "catalog/pg_extstore.h"
#include "catalog/pg_group.h"
#include "catalog/pg_index.h"
#include "catalog/pg_inherits.h"
#include "catalog/pg_language.h"
#include "catalog/pg_listener.h"
#include "catalog/pg_opclass.h"
#include "catalog/pg_operator.h"
#include "catalog/pg_proc.h"
#include "catalog/pg_rewrite.h"
#include "catalog/pg_shadow.h"
#include "catalog/pg_statistic.h"
#include "catalog/pg_type.h"
#include "utils/catcache.h"
#include "utils/temprel.h"
#include "miscadmin.h"

#include "utils/syscache.h"
#include "catalog/indexing.h"


typedef struct syscachemem {
	CatCache**		SysCache;   
        bool                        CacheInitialized;
} SysCacheGlobal;

static SectionId sys_cache_id = SECTIONID("SYCH");
/* Thread Local Storage for syscache globals  */
#ifdef TLS
TLS SysCacheGlobal*  syscache_global = NULL;
#else
#define syscache_global GetEnv()->syscache_global
#endif

static SysCacheGlobal* InitializeSysCacheGlobal(void);
static SysCacheGlobal* GetSysCacheGlobal(void);

/*---------------------------------------------------------------------------

	Adding system caches:

	Add your new cache to the list in include/utils/syscache.h.  Keep
	the list sorted alphabetically and adjust the cache numbers
	accordingly.

	Add your entry to the cacheinfo[] array below.	All cache lists are
	alphabetical, so add it in the proper place.  Specify the relation
	name, number of arguments, argument names, size of tuple, index lookup
	function, and index name.

	In include/catalog/indexing.h, add a define for the number of indexes
	in the relation, add a define for the index name, add an extern
	array to hold the index names, define the index lookup function
	prototype, and use DECLARE_UNIQUE_INDEX to define the index.  Cache
	lookups return only one row, so the index should be unique.

	In backend/catalog/indexing.c, initialize the relation array with
	the index names for the relation, fixed size of relation (or marking
	first non-fixed length field), and create the index lookup function.
	Pick one that takes similar arguments and use that one, but keep the
	function names in the same order as the cache list for clarity.

	Finally, any place your relation gets heap_insert() or
	heap_update calls, include code to do a CatalogIndexInsert() to update
	the system indexes.  The heap_* calls do not update indexes.

	bjm 1999/11/22

  ---------------------------------------------------------------------------
*/

static struct cachedesc cacheinfo[] = {
	{AggregateRelationName,		/* AGGNAME */
		2,
		{
			Anum_pg_aggregate_aggname,
			Anum_pg_aggregate_aggbasetype,
			0,
			0
		},
		offsetof(FormData_pg_aggregate, agginitval1),
		AggregateNameTypeIndex,
	AggregateNameTypeIndexScan},
	{AccessMethodRelationName,	/* AMNAME */
		1,
		{
			Anum_pg_am_amname,
			0,
			0,
			0
		},
		sizeof(FormData_pg_am),
		AmNameIndex,
	AmNameIndexScan},
	{AccessMethodOperatorRelationName,	/* AMOPOPID */
		3,
		{
			Anum_pg_amop_amopclaid,
			Anum_pg_amop_amopopr,
			Anum_pg_amop_amopid,
			0
		},
		sizeof(FormData_pg_amop),
		AccessMethodOpidIndex,
	AccessMethodOpidIndexScan},
	{AccessMethodOperatorRelationName,	/* AMOPSTRATEGY */
		3,
		{
			Anum_pg_amop_amopid,
			Anum_pg_amop_amopclaid,
			Anum_pg_amop_amopstrategy,
			0
		},
		sizeof(FormData_pg_amop),
		AccessMethodStrategyIndex,
                AccessMethodStrategyIndexScan},
	{AttributeRelationName,		/* ATTNAME */
		2,
		{
			Anum_pg_attribute_attrelid,
			Anum_pg_attribute_attname,
			0,
			0
		},
		ATTRIBUTE_TUPLE_SIZE,
		AttributeRelidNameIndex,
	AttributeRelidNameIndexScan},
	{AttributeRelationName,		/* ATTNUM */
		2,
		{
			Anum_pg_attribute_attrelid,
			Anum_pg_attribute_attnum,
			0,
			0
		},
		ATTRIBUTE_TUPLE_SIZE,
		AttributeRelidNumIndex,
                AttributeRelidNumIndexScan},
	{OperatorClassRelationName, /* CLADEFTYPE */
		1,
		{
			Anum_pg_opclass_opcdeftype,
			0,
			0,
			0
		},
		sizeof(FormData_pg_opclass),
		OpclassDeftypeIndex,
	OpclassDeftypeIndexScan},
	{OperatorClassRelationName, /* CLANAME */
		1,
		{
			Anum_pg_opclass_opcname,
			0,
			0,
			0
		},
		sizeof(FormData_pg_opclass),
		OpclassNameIndex,
	OpclassNameIndexScan},
	{ExtStoreRelationName, /* EXTSTORE */
		2,
		{
			Anum_pg_extstore_extrelid,
			Anum_pg_extstore_extattnum,
			0,
			0
		},
		sizeof(FormData_pg_extstore),
		ExtStoreAttnumIndex,
	ExtStoreAttnumIndexScan},
        {GroupRelationName,			/* GRONAME */
		1,
		{
			Anum_pg_group_groname,
			0,
			0,
			0
		},
		offsetof(FormData_pg_group, grolist),
		GroupNameIndex,
	GroupNameIndexScan},
	{GroupRelationName,			/* GROSYSID */
		1,
		{
			Anum_pg_group_grosysid,
			0,
			0,
			0
		},
		offsetof(FormData_pg_group, grolist),
		GroupSysidIndex,
	GroupSysidIndexScan},
	{IndexRelationName,			/* INDEXRELID */
		1,
		{
			Anum_pg_index_indexrelid,
			0,
			0,
			0
		},
		offsetof(FormData_pg_index, indpred),
		IndexRelidIndex,
	IndexRelidIndexScan},
	{InheritsRelationName,		/* INHRELID */
		2,
		{
			Anum_pg_inherits_inhrelid,
			Anum_pg_inherits_inhseqno,
			0,
			0
		},
		sizeof(FormData_pg_inherits),
		InheritsRelidSeqnoIndex,
	InheritsRelidSeqnoIndexScan},
	{LanguageRelationName,		/* LANGNAME */
		1,
		{
			Anum_pg_language_lanname,
			0,
			0,
			0
		},
		offsetof(FormData_pg_language, lancompiler),
		LanguageNameIndex,
	LanguageNameIndexScan},
	{LanguageRelationName,		/* LANGOID */
		1,
		{
			ObjectIdAttributeNumber,
			0,
			0,
			0
		},
		offsetof(FormData_pg_language, lancompiler),
		LanguageOidIndex,
	LanguageOidIndexScan},
	{ListenerRelationName,		/* LISTENREL */
		2,
		{
			Anum_pg_listener_relname,
			Anum_pg_listener_pid,
			0,
			0
		},
		sizeof(FormData_pg_listener),
		ListenerRelnamePidIndex,
	ListenerRelnamePidIndexScan},
	{OperatorRelationName,		/* OPERNAME */
		4,
		{
			Anum_pg_operator_oprname,
			Anum_pg_operator_oprleft,
			Anum_pg_operator_oprright,
			Anum_pg_operator_oprkind
		},
		sizeof(FormData_pg_operator),
		OperatorNameIndex,
                OperatorNameIndexScan},
	{OperatorRelationName,		/* OPEROID */
		1,
		{
			ObjectIdAttributeNumber,
			0,
			0,
			0
		},
		sizeof(FormData_pg_operator),
		OperatorOidIndex,
	OperatorOidIndexScan},
	{ProcedureRelationName,		/* PROCNAME */
		3,
		{
			Anum_pg_proc_proname,
			Anum_pg_proc_pronargs,
			Anum_pg_proc_proargtypes,
			0
		},
		offsetof(FormData_pg_proc, prosrc),
		ProcedureNameIndex,
                ProcedureNameIndexScan},
	{ProcedureRelationName,		/* PROCOID */
		1,
		{
			ObjectIdAttributeNumber,
			0,
			0,
			0
		},
		offsetof(FormData_pg_proc, prosrc),
		ProcedureOidIndex,
	ProcedureOidIndexScan},
	{RelationRelationName,		/* RELNAME */
		1,
		{
			Anum_pg_class_relname,
			0,
			0,
			0
		},
		CLASS_TUPLE_SIZE,
		ClassNameIndex,
	ClassNameIndexScan},
	{RelationRelationName,		/* RELOID */
		1,
		{
			ObjectIdAttributeNumber,
			0,
			0,
			0
		},
		CLASS_TUPLE_SIZE,
		ClassOidIndex,
	ClassOidIndexScan},
	{RewriteRelationName,		/* REWRITENAME */
		1,
		{
			Anum_pg_rewrite_rulename,
			0,
			0,
			0
		},
		offsetof(FormData_pg_rewrite, ev_qual),
		RewriteRulenameIndex,
	RewriteRulenameIndexScan},
	{RewriteRelationName,		/* RULEOID */
		1,
		{
			ObjectIdAttributeNumber,
			0,
			0,
			0
		},
		offsetof(FormData_pg_rewrite, ev_qual),
		RewriteOidIndex,
	RewriteOidIndexScan},
	{ShadowRelationName,		/* SHADOWNAME */
		1,
		{
			Anum_pg_shadow_usename,
			0,
			0,
			0
		},
		sizeof(FormData_pg_shadow),
		ShadowNameIndex,
	ShadowNameIndexScan},
	{ShadowRelationName,		/* SHADOWSYSID */
		1,
		{
			Anum_pg_shadow_usesysid,
			0,
			0,
			0
		},
		sizeof(FormData_pg_shadow),
		ShadowSysidIndex,
	ShadowSysidIndexScan},
	{StatisticRelationName,		/* STATRELID */
		2,
		{
			Anum_pg_statistic_starelid,
			Anum_pg_statistic_staattnum,
			0,
			0
		},
		offsetof(FormData_pg_statistic, stacommonval),
		StatisticRelidAttnumIndex,
                StatisticRelidAttnumIndexScan},
	{TypeRelationName,			/* TYPENAME */
		1,
		{
			Anum_pg_type_typname,
			0,
			0,
			0
		},
		offsetof(FormData_pg_type, typalign) +sizeof(char),
		TypeNameIndex,
	TypeNameIndexScan},
	{TypeRelationName,			/* TYPEOID */
		1,
		{
			ObjectIdAttributeNumber,
			0,
			0,
			0
		},
		offsetof(FormData_pg_type, typalign) +sizeof(char),
		TypeOidIndex,
	TypeOidIndexScan}
};

#ifdef GLOBALCACHE
static struct catcache *SysCache[lengthof(cacheinfo)];
static bool CacheInitialized = false;
#endif

static int32 SysCacheSize = lengthof(cacheinfo);


extern bool
IsCacheInitialized(void)
{
	return GetSysCacheGlobal()->CacheInitialized;
}


/*
 * zerocaches
 *
 *	  Make sure the SysCache structure is zero'd.
 */
void
zerocaches()
{
    SysCacheGlobal* sglobal = GetSysCacheGlobal();

	if ( sglobal->SysCache == NULL ) return;
	MemSet((char *) sglobal->SysCache, 0, SysCacheSize * sizeof(CatCache*));
}

/*
 * Note:
 *		This function was written because the initialized catalog caches
 *		are used to determine which caches may contain tuples which need
 *		to be invalidated in other backends.
 */
void
InitCatalogCache()
{
	int			cacheId;		/* XXX type */

#ifndef GLOBALCACHE
        SysCacheGlobal* sglobal = GetSysCacheGlobal();
        MemoryContext oldcxt = MemoryContextSwitchTo(MemoryContextGetTopContext());
        sglobal->SysCache = palloc(sizeof(CatCache*) * SysCacheSize);
        memset(sglobal->SysCache,0,sizeof(CatCache*) * SysCacheSize);
#endif
	if (IsBootstrapProcessingMode())
	{
		for (cacheId = 0; cacheId < SysCacheSize; cacheId += 1)
		{

			Assert(!PointerIsValid((Pointer) sglobal->SysCache[cacheId]));

			sglobal->SysCache[cacheId] = InitSysCache(cacheinfo[cacheId].name,
											 cacheinfo[cacheId].indname,
											 cacheId,
											 cacheinfo[cacheId].nkeys,
											 cacheinfo[cacheId].key,
										   cacheinfo[cacheId].iScanFunc);
			if (!PointerIsValid((char *) sglobal->SysCache[cacheId]))
			{
				elog(ERROR,
					 "InitCatalogCache: Can't init cache %s(%d)",
					 cacheinfo[cacheId].name,
					 cacheId);
			}

		}
	}
#ifndef GLOBALCACHE
	MemoryContextSwitchTo(oldcxt);
#endif
	GetSysCacheGlobal()->CacheInitialized = true;
}

/*
 * SearchSysCacheTupleCopy
 *
 *	This is like SearchSysCacheTuple, except it returns a copy of the tuple
 *	that the user is required to pfree().
 */
HeapTuple
SearchSysCacheTupleCopy(int cacheId,	/* cache selection code */
						Datum key1,
						Datum key2,
						Datum key3,
						Datum key4)
{
	HeapTuple	cachetup;

	cachetup = SearchSysCacheTuple(cacheId, key1, key2, key3, key4);
	if (PointerIsValid(cachetup))
		return heap_copytuple(cachetup);
	else
		return cachetup;		/* NULL */
}


/*
 * SearchSysCacheTuple
 *
 *	A layer on top of SearchSysCache that does the initialization and
 *	key-setting for you.
 *
 *	Returns the cache copy of the tuple if one is found, NULL if not.
 *	The tuple is the 'cache' copy.
 *
 *	XXX The tuple that is returned is NOT supposed to be pfree'd!
 */
HeapTuple
SearchSysCacheTuple(int cacheId,/* cache selection code */
					Datum key1,
					Datum key2,
					Datum key3,
					Datum key4)
{
	HeapTuple	tp;
    SysCacheGlobal* sglobal = GetSysCacheGlobal();

	if (cacheId < 0 || cacheId >= SysCacheSize)
	{
		elog(ERROR, "SearchSysCacheTuple: Bad cache id %d", cacheId);
		return (HeapTuple) NULL;
	}

	if (!PointerIsValid(sglobal->SysCache[cacheId]))
	{
		sglobal->SysCache[cacheId] = InitSysCache(cacheinfo[cacheId].name,
										 cacheinfo[cacheId].indname,
										 cacheId,
										 cacheinfo[cacheId].nkeys,
										 cacheinfo[cacheId].key,
										 cacheinfo[cacheId].iScanFunc);
		if (!PointerIsValid(sglobal->SysCache[cacheId]))
			elog(ERROR,
				 "InitCatalogCache: Can't init cache %s(%d)",
				 cacheinfo[cacheId].name,
				 cacheId);
	}

	/* temp table name remapping */
	if (cacheId == RELNAME)
	{
		char	   *nontemp_relname;

		if ((nontemp_relname =
			 get_temp_rel_by_username(DatumGetPointer(key1))) != NULL)
			key1 = PointerGetDatum(nontemp_relname);
	}

	tp = SearchSysCache(sglobal->SysCache[cacheId], key1, key2, key3, key4);
	if (!HeapTupleIsValid(tp))
	{
#ifdef CACHEDEBUG
		elog(DEBUG,
			 "SearchSysCacheTuple: Search %s(%d) %d %d %d %d failed",
			 cacheinfo[cacheId].name,
			 cacheId, key1, key2, key3, key4);
#endif
		return (HeapTuple) NULL;
	}
	return tp;
}


/*
 * SysCacheGetAttr
 *
 *		Given a tuple previously fetched by SearchSysCacheTuple() or
 *		SearchSysCacheTupleCopy(), extract a specific attribute.
 *
 * This is equivalent to using heap_getattr() on a tuple fetched
 * from a non-cached relation.	Usually, this is only used for attributes
 * that could be NULL or variable length; the fixed-size attributes in
 * a system table are accessed just by mapping the tuple onto the C struct
 * declarations from include/catalog/.
 *
 * As with heap_getattr(), if the attribute is of a pass-by-reference type
 * then a pointer into the tuple data area is returned --- the caller must
 * not modify or pfree the datum!
 */
Datum
SysCacheGetAttr(int cacheId, HeapTuple tup,
				AttrNumber attributeNumber,
				bool *isnull)
{

    SysCacheGlobal* sglobal = GetSysCacheGlobal();
	/*
	 * We just need to get the TupleDesc out of the cache entry, and then
	 * we can apply heap_getattr().  We expect that the cache control data
	 * is currently valid --- if the caller just fetched the tuple, then
	 * it should be.
	 */
	if (cacheId < 0 || cacheId >= SysCacheSize)
		elog(ERROR, "SysCacheGetAttr: Bad cache id %d", cacheId);
	if (!PointerIsValid(sglobal->SysCache[cacheId]) ||
		sglobal->SysCache[cacheId]->relationId == InvalidOid ||
		!PointerIsValid(sglobal->SysCache[cacheId]->cc_tupdesc))
		elog(ERROR, "SysCacheGetAttr: missing cache data for id %d", cacheId);

	return HeapGetAttr(tup, attributeNumber,
						sglobal->SysCache[cacheId]->cc_tupdesc,
						isnull);
}

SysCacheGlobal* GetSysCacheGlobal() {
    SysCacheGlobal* info = syscache_global;
    if ( info == NULL ) {
        info = InitializeSysCacheGlobal();
    }
    return info;
}

SysCacheGlobal* InitializeSysCacheGlobal() {
	SysCacheGlobal* global = AllocateEnvSpace(sys_cache_id,sizeof(SysCacheGlobal));
        
        memset(global,0x00,sizeof(SysCacheGlobal));
        global->CacheInitialized = false;
        
        syscache_global = global;
	return global;
}
