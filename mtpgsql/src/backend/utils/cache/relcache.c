/*-------------------------------------------------------------------------
 *
 * relcache.c
 *	  POSTGRES relation descriptor cache code
 *
 * Portions Copyright (c) 2000-2024, Myron Scott  <myron@weaverdb.org>
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *
 *
 *-------------------------------------------------------------------------
 */
/*
 * INTERFACE ROUTINES
 *		RelationInitialize				- initialize relcache
 *		RelationIdCacheGetRelation		- get a reldesc from the cache (id)
 *		RelationNameCacheGetRelation	- get a reldesc from the cache (name)
 *		RelationIdGetRelation			- get a reldesc by relation id
 *		RelationNameGetRelation			- get a reldesc by relation name
 *		RelationClose					- close an open relation
 *
 * NOTES
 *		This file is in the process of being cleaned up
 *		before I add system attribute indexing.  -cim 1/13/91
 *
 *		The following code contains many undocumented hacks.  Please be
 *		careful....
 *
 */

#include <errno.h>
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>


#include "postgres.h"
#include "env/env.h"
#include "utils/builtins.h"
#include "access/genam.h"
#include "access/heapam.h"
#include "access/istrat.h"
#include "catalog/catalog.h"
#include "catalog/catname.h"
#include "catalog/index.h"
#include "catalog/indexing.h"
#include "catalog/pg_attrdef.h"
#include "catalog/pg_log.h"
#include "catalog/pg_proc.h"
#include "catalog/pg_relcheck.h"
#include "catalog/pg_rewrite.h"
#include "catalog/pg_type.h"
#include "catalog/pg_variable.h"
#include "catalog/pg_database.h"
#include "commands/trigger.h"
#include "lib/hasht.h"
#include "miscadmin.h"
#include "storage/bufmgr.h"
#include "storage/smgr.h"
#include "utils/catcache.h"
#include "utils/relcache.h"
#include "utils/temprel.h"
#include "utils/mcxt.h"
#include "nodes/pg_list.h"
#include "env/freespace.h"
#include "env/dbwriter.h"

#ifdef GLOBALCACHE
extern MemoryContext CacheCxt
#endif

        static MemoryContext GlobalCacheMemory = NULL;

/* ----------------
 *		hardcoded tuple descriptors.  see lib/backend/catalog/pg_attribute.h
 * ----------------
 */
static FormData_pg_attribute Desc_pg_class[Natts_pg_class] = {Schema_pg_class};
/*
static FormData_pg_attribute Desc_pg_database[Natts_pg_database] = {Schema_pg_database};
*/
static FormData_pg_attribute Desc_pg_attribute[Natts_pg_attribute] = {Schema_pg_attribute};
static FormData_pg_attribute Desc_pg_proc[Natts_pg_proc] = {Schema_pg_proc};
static FormData_pg_attribute Desc_pg_type[Natts_pg_type] = {Schema_pg_type};
static FormData_pg_attribute Desc_pg_variable[Natts_pg_variable] = {Schema_pg_variable};
static FormData_pg_attribute Desc_pg_log[Natts_pg_log] = {Schema_pg_log};

/* pg_attnumind, pg_classnameind, pg_classoidind */
#define Num_indices_bootstrap	10

/*  these are used under the multi-threaded server to create index relation cache
    that is read in from file under normal postgres  */
typedef struct cachelist {
    char database[256];
    Relation icache[Num_indices_bootstrap];
} CacheList;

typedef struct rcache_global {
    CatCache* Caches;
    HTAB* RelationNameCache;
    HTAB* RelationIdCache;
    List* newlyCreatedRelns;
    MemoryContext rcache_cxt;
    bool criticalRelcacheBuild;
} RelationCacheGlobal;

static SectionId rel_cache_id = SECTIONID("RCGM");

/*  Thread Local Storage cache from RelationCacheGlobal  */
#ifdef TLS
TLS RelationCacheGlobal* relationcache_global = NULL;
#else 
#define relationcache_global GetEnv()->relationcache_global
#endif

static RelationCacheGlobal* InitializeRelationCacheGlobal(void);
static RelationCacheGlobal* GetRelationCacheGlobal(void);


static CacheList* masterlist;
static int listsize;
static pthread_mutex_t* igate;

static long m_init_irels(void);
static void RelationCacheInsert(Relation RELATION);
static Relation RelationNameCacheLookup(char* NAME);
static Relation RelationIdCacheLookup(Oid* ID);
static void RelationCacheDelete(Relation RELATION);

/* ----------------
 *		Hash tables that index the relation cache
 *
 *		Relations are cached two ways, by name and by id,
 *		thus there are two hash tables for referencing them.
 * ----------------
 */

/* ----------------
 *		RelationBuildDescInfo exists so code can be shared
 *		between RelationIdGetRelation() and RelationNameGetRelation()
 * ----------------
 */
typedef struct RelationBuildDescInfo {
    int infotype; /* lookup by id or by name */
#define INFO_RELID 1
#define INFO_RELNAME 2

    union {
        Oid info_id; /* relation object id */
        NameData info_name; /* relation name */
    } i;
} RelationBuildDescInfo;

/* -----------------
 *		macros to manipulate name cache and id cache
 * -----------------
 */
static void RelationCacheInsert(Relation RELATION) {
    RelationCacheGlobal* rglobal = GetRelationCacheGlobal();
    RelIdCacheEnt *idhentry;
    RelNameCacheEnt *namehentry;
    NameData name;

    char* relname = RelationGetPhysicalRelationName(RELATION);
    bool found = false;

    if (RELATION->buffer_cxt == NULL) {
        RELATION->buffer_cxt = GetBufferCxt();
        RELATION->snapshot_cxt = GetSnapshotHolder();
    }

    idhentry = (RelIdCacheEnt*) hash_search(rglobal->RelationIdCache, (char *) &RELATION->rd_id, HASH_ENTER, &found);
    if (idhentry == NULL)
        elog(FATAL, "can't insert into relation descriptor cache");
    if (found && !IsBootstrapProcessingMode()) {
        /* used to give notice -- now just keep quiet */
    }
    idhentry->reldesc = RELATION;

    namestrcpy(&name, relname);
    namehentry = (RelNameCacheEnt*) hash_search(rglobal->RelationNameCache, (char*) &name, HASH_ENTER, &found);
    if (namehentry == NULL)
        elog(FATAL, "can't insert into relation descriptor cache");
    if (found && !IsBootstrapProcessingMode()) {
        /* used to give notice -- now just keep quiet */
    }
    namehentry->reldesc = RELATION;

} /*  while(0)  */

static Relation RelationNameCacheLookup(char* NAME) {
    RelNameCacheEnt *hentry;
    bool found;
    RelationCacheGlobal* rglobal = GetRelationCacheGlobal();
    hentry = (RelNameCacheEnt*) hash_search(rglobal->RelationNameCache, (char *) NAME, HASH_FIND, &found);

    if (found)
        return hentry->reldesc;
    else
        return NULL;
}

static Relation RelationIdCacheLookup(Oid* ID) {
    RelationCacheGlobal* rglobal = GetRelationCacheGlobal();
    RelIdCacheEnt *hentry;
    bool found;
    hentry = (RelIdCacheEnt*) hash_search(rglobal->RelationIdCache, (char *) ID, HASH_FIND, &found);

    if (found)
        return hentry->reldesc;
    else
        return NULL;
}

static void RelationCacheDelete(Relation RELATION) {
    RelNameCacheEnt *namehentry;
    RelIdCacheEnt *idhentry;
    char *relname;
    bool found;
    RelationCacheGlobal* rglobal = GetRelationCacheGlobal();

    relname = RelationGetPhysicalRelationName(RELATION);
    namehentry = (RelNameCacheEnt*) hash_search(rglobal->RelationNameCache, relname, HASH_REMOVE, &found);
    if (namehentry == NULL)
        elog(FATAL, "can't delete from relation descriptor cache");
    if (!found)
        elog(NOTICE, "trying to delete a reldesc that does not exist.");

    idhentry = (RelIdCacheEnt*) hash_search(rglobal->RelationIdCache, (char *) &RELATION->rd_id, HASH_REMOVE, &found);
    if (idhentry == NULL)
        elog(FATAL, "can't delete from relation descriptor cache");
    if (!found)
        elog(NOTICE, "trying to delete a reldesc that does not exist.");
}

/* non-export function prototypes */

static void RelationShutdown(RelNameCacheEnt* relation, int dummy);
static void RelationClearRelation(Relation relation, bool rebuildIt);
static void RelationFlushRelation(RelNameCacheEnt *relationPtr,
        int skipLocalRelations);
static Relation RelationNameCacheGetRelation(const char *relationName, Oid databaseId);
static void RelationCacheAbortWalker(RelNameCacheEnt *relationPtr, int dummy);
static void RelationCacheCommitChecker(RelNameCacheEnt *relationPtr, int dummy);
static void init_irels(void);

static void formrdesc(char *relationName, u_int natts,
        FormData_pg_attribute *att, char relkind);

static HeapTuple ScanPgRelation(RelationBuildDescInfo buildinfo);
static HeapTuple scan_pg_rel_seq(RelationBuildDescInfo buildinfo);
static HeapTuple scan_pg_rel_ind(RelationBuildDescInfo buildinfo);
static Relation AllocateRelationDesc(Relation relation, u_int natts,
        Form_pg_class relp);
static void RelationBuildTupleDesc(RelationBuildDescInfo buildinfo,
        Relation relation, u_int natts);
static void build_tupdesc_seq(RelationBuildDescInfo buildinfo,
        Relation relation, u_int natts);
static void build_tupdesc_ind(RelationBuildDescInfo buildinfo,
        Relation relation, u_int natts);
static Relation RelationBuildDesc(RelationBuildDescInfo buildinfo,
        Relation oldrelation);
static Relation RelationCopyRelation(Relation target);
static void IndexedAccessMethodInitialize(Relation relation);
static void AttrDefaultFetch(Relation relation);
static void RelCheckFetch(Relation relation);

/* --------------------------------
 *		ScanPgRelation
 *
 *		this is used by RelationBuildDesc to find a pg_class
 *		tuple matching either a relation name or a relation id
 *		as specified in buildinfo.
 *
 *		NB: the returned tuple has been copied into palloc'd storage
 *		and must eventually be freed with heap_freetuple.
 * --------------------------------
 */
static HeapTuple
ScanPgRelation(RelationBuildDescInfo buildinfo) {

    /*
     * If this is bootstrap time (initdb), then we can't use the system
     * catalog indices, because they may not exist yet.  Otherwise, we
     * can, and do.
     */
    RelationCacheGlobal* rglobal = GetRelationCacheGlobal();

    if (IsIgnoringSystemIndexes() || !rglobal->criticalRelcacheBuild)
        return scan_pg_rel_seq(buildinfo);
    else
        return scan_pg_rel_ind(buildinfo);
}

static HeapTuple
scan_pg_rel_seq(RelationBuildDescInfo buildinfo) {
    HeapTuple pg_class_tuple;
    HeapTuple return_tuple;
    Relation pg_class_desc;
    HeapScanDesc pg_class_scan;
    ScanKeyData key;

    /* ----------------
     *	form a scan key
     * ----------------
     */
    switch (buildinfo.infotype) {
        case INFO_RELID:
            ScanKeyEntryInitialize(&key, 0,
                    ObjectIdAttributeNumber,
                    F_OIDEQ,
                    ObjectIdGetDatum(buildinfo.i.info_id));
            break;

        case INFO_RELNAME:
            ScanKeyEntryInitialize(&key, 0,
                    Anum_pg_class_relname,
                    F_NAMEEQ,
                    NameGetDatum(&buildinfo.i.info_name));
            break;

        default:
            elog(ERROR, "ScanPgRelation: bad buildinfo");
            return NULL;
    }

    /* ----------------
     *	open pg_class and fetch a tuple
     * ----------------
     */
    pg_class_desc = heap_openr(RelationRelationName, AccessShareLock);
    pg_class_scan = heap_beginscan(pg_class_desc, SnapshotNow, 1, &key);
    pg_class_tuple = heap_getnext(pg_class_scan);

    /* ----------------
     *	get set to return tuple
     * ----------------
     */
    if (!HeapTupleIsValid(pg_class_tuple))
        return_tuple = pg_class_tuple;
    else {
        /* ------------------
         *	a satanic bug used to live here: pg_class_tuple used to be
         *	returned here without having the corresponding buffer pinned.
         *	so when the buffer gets replaced, all hell breaks loose.
         *	this bug is discovered and killed by wei on 9/27/91.
         * -------------------
         */
        return_tuple = heap_copytuple(pg_class_tuple);
    }

    /* all done */
    heap_endscan(pg_class_scan);
    heap_close(pg_class_desc, AccessShareLock);

    return return_tuple;
}

static HeapTuple
scan_pg_rel_ind(RelationBuildDescInfo buildinfo) {
    Relation pg_class_desc;
    HeapTuple return_tuple;

    pg_class_desc = heap_openr(RelationRelationName, AccessShareLock);

    switch (buildinfo.infotype) {
        case INFO_RELID:
            return_tuple = ClassOidIndexScan(pg_class_desc, buildinfo.i.info_id);
            break;

        case INFO_RELNAME:
            return_tuple = ClassNameIndexScan(pg_class_desc, NameStr(buildinfo.i.info_name));
            break;

        default:
            elog(ERROR, "ScanPgRelation: bad buildinfo");
            return_tuple = NULL; /* keep compiler quiet */
    }

    heap_close(pg_class_desc, AccessShareLock);

    /* The xxxIndexScan routines will have returned a palloc'd tuple. */

    return return_tuple;
}

/* ----------------
 *		AllocateRelationDesc
 *
 *		This is used to allocate memory for a new relation descriptor
 *		and initialize the rd_rel field.
 *
 *		If 'relation' is NULL, allocate a new RelationData object.
 *		If not, reuse the given object (that path is taken only when
 *		we have to rebuild a relcache entry during RelationClearRelation).
 * ----------------
 */
static Relation
AllocateRelationDesc(Relation relation, u_int natts,
        Form_pg_class relp) {
    Form_pg_class relationForm;

    /* ----------------
     *	allocate space for the relation tuple form
     * ----------------
     */
    relationForm = (Form_pg_class) palloc(sizeof (FormData_pg_class));

    memcpy((char *) relationForm, (char *) relp, CLASS_TUPLE_SIZE);

    /* ----------------
     *	allocate space for new relation descriptor, if needed
     */
    if (relation == NULL)
        relation = (Relation) palloc(sizeof (RelationData));

    /* ----------------
     *	clear new reldesc
     * ----------------
     */
    MemSet((char *) relation, 0, sizeof (RelationData));

    /* make sure relation is marked as having no open file yet */
    relation->rd_smgr = NULL;
    relation->readtrigger = NULL;
    /* initialize attribute tuple form */
    relation->rd_att = CreateTemplateTupleDesc(natts);

    /* and initialize relation tuple form */
    relation->rd_rel = relationForm;

    relation->buffer_cxt = GetBufferCxt();
    relation->snapshot_cxt = GetSnapshotHolder();

    return relation;
}

/* --------------------------------
 *		RelationBuildTupleDesc
 *
 *		Form the relation's tuple descriptor from information in
 *		the pg_attribute, pg_attrdef & pg_relcheck system cataloges.
 * --------------------------------
 */
static void
RelationBuildTupleDesc(RelationBuildDescInfo buildinfo,
        Relation relation,
        u_int natts) {

    /*
     * If this is bootstrap time (initdb), then we can't use the system
     * catalog indices, because they may not exist yet.  Otherwise, we
     * can, and do.
     */
    RelationCacheGlobal* rglobal = GetRelationCacheGlobal();

    if (IsIgnoringSystemIndexes() || !rglobal->criticalRelcacheBuild)
        build_tupdesc_seq(buildinfo, relation, natts);
    else
        build_tupdesc_ind(buildinfo, relation, natts);
}

static void
SetConstrOfRelation(Relation relation, TupleConstr *constr, int ndef, AttrDefault *attrdef) {
    if (constr->has_not_null || ndef > 0 || relation->rd_rel->relchecks) {
        relation->rd_att->constr = constr;

        if (ndef > 0) /* DEFAULTs */ {
            if (ndef < relation->rd_rel->relnatts)
                constr->defval = (AttrDefault *)
                repalloc(attrdef, ndef * sizeof (AttrDefault));
            else
                constr->defval = attrdef;
            constr->num_defval = ndef;
            AttrDefaultFetch(relation);
        } else
            constr->num_defval = 0;

        if (relation->rd_rel->relchecks > 0) /* CHECKs */ {
            constr->num_check = relation->rd_rel->relchecks;
            constr->check = (ConstrCheck *) palloc(constr->num_check *
                    sizeof (ConstrCheck));
            MemSet(constr->check, 0, constr->num_check * sizeof (ConstrCheck));
            RelCheckFetch(relation);
        } else
            constr->num_check = 0;
    } else {
        pfree(constr);
        relation->rd_att->constr = NULL;
    }
}

static void
build_tupdesc_seq(RelationBuildDescInfo buildinfo,
        Relation relation,
        u_int natts) {
    HeapTuple pg_attribute_tuple;
    Relation pg_attribute_desc;
    HeapScanDesc pg_attribute_scan;
    Form_pg_attribute attp;
    ScanKeyData key;
    int need;
    TupleConstr *constr = (TupleConstr *) palloc(sizeof (TupleConstr));
    AttrDefault *attrdef = NULL;
    int ndef = 0;

    constr->has_not_null = false;
    /* ----------------
     *	form a scan key
     * ----------------
     */
    ScanKeyEntryInitialize(&key, 0,
            Anum_pg_attribute_attrelid,
            F_OIDEQ,
            ObjectIdGetDatum(RelationGetRelid(relation)));

    /* ----------------
     *	open pg_attribute and begin a scan
     * ----------------
     */
    pg_attribute_desc = heap_openr(AttributeRelationName, AccessShareLock);
    pg_attribute_scan = heap_beginscan(pg_attribute_desc, SnapshotNow, 1, &key);

    /* ----------------
     *	add attribute data to relation->rd_att
     * ----------------
     */
    need = natts;

    pg_attribute_tuple = heap_getnext(pg_attribute_scan);
    while (HeapTupleIsValid(pg_attribute_tuple) && need > 0) {
        attp = (Form_pg_attribute) GETSTRUCT(pg_attribute_tuple);

        if (attp->attnum > 0) {
            relation->rd_att->attrs[attp->attnum - 1] =
                    (Form_pg_attribute) palloc(ATTRIBUTE_TUPLE_SIZE);

            memcpy((char *) (relation->rd_att->attrs[attp->attnum - 1]),
                    (char *) attp,
                    ATTRIBUTE_TUPLE_SIZE);
            need--;
            /* Update if this attribute have a constraint */
            if (attp->attnotnull)
                constr->has_not_null = true;

            if (attp->attstorage == 'e')
                relation->rd_att->blobatt = attp->attnum;

            if (attp->atthasdef) {
                if (attrdef == NULL) {
                    attrdef = (AttrDefault *) palloc(relation->rd_rel->relnatts *
                            sizeof (AttrDefault));
                    MemSet(attrdef, 0,
                            relation->rd_rel->relnatts * sizeof (AttrDefault));
                }
                attrdef[ndef].adnum = attp->attnum;
                attrdef[ndef].adbin = NULL;
                ndef++;
            }

        }
        pg_attribute_tuple = heap_getnext(pg_attribute_scan);
    }

    if (need > 0)
        elog(ERROR, "catalog is missing %d attribute%s for relid %lu",
            need, (need == 1 ? "" : "s"), RelationGetRelid(relation));

    /* ----------------
     *	end the scan and close the attribute relation
     * ----------------
     */
    heap_endscan(pg_attribute_scan);
    heap_close(pg_attribute_desc, AccessShareLock);

    SetConstrOfRelation(relation, constr, ndef, attrdef);
}

static void
build_tupdesc_ind(RelationBuildDescInfo buildinfo,
        Relation relation,
        u_int natts) {
    Relation attrel;
    HeapTuple atttup;
    Form_pg_attribute attp;
    TupleConstr *constr = (TupleConstr *) palloc(sizeof (TupleConstr));
    AttrDefault *attrdef = NULL;
    int ndef = 0;
    int i;

    constr->has_not_null = false;

    attrel = heap_openr(AttributeRelationName, AccessShareLock);

    for (i = 1; i <= relation->rd_rel->relnatts; i++) {
#ifdef	_DROP_COLUMN_HACK__
        bool columnDropped = false;
#endif	 /* _DROP_COLUMN_HACK__ */

        atttup = (HeapTuple) AttributeRelidNumIndexScan(attrel,
                RelationGetRelid(relation), i);

        if (!HeapTupleIsValid(atttup)) {
#ifdef	_DROP_COLUMN_HACK__
            atttup = (HeapTuple) AttributeRelidNumIndexScan(attrel,
                    RelationGetRelid(relation), DROPPED_COLUMN_INDEX(i));
            if (!HeapTupleIsValid(atttup))
#endif	 /* _DROP_COLUMN_HACK__ */
                elog(ERROR, "cannot find attribute %d of relation %s", i,
                    RelationGetRelationName(relation));
#ifdef	_DROP_COLUMN_HACK__
            columnDropped = true;
#endif	 /* _DROP_COLUMN_HACK__ */
        }

        relation->rd_att->attrs[i - 1] = attp =
                (Form_pg_attribute) palloc(ATTRIBUTE_TUPLE_SIZE);

        memcpy((char *) attp,
                (char *) (Form_pg_attribute) GETSTRUCT(atttup),
                ATTRIBUTE_TUPLE_SIZE);

        /* don't forget to free the tuple returned from xxxIndexScan */
        heap_freetuple(atttup);

#ifdef	_DROP_COLUMN_HACK__
        if (columnDropped)
            continue;
#endif	 /* _DROP_COLUMN_HACK__ */

        /* Update if this attribute have a constraint */
        if (attp->attnotnull)
            constr->has_not_null = true;

        if (attp->attstorage == 'e')
            relation->rd_att->blobatt = attp->attnum;

        if (attp->atthasdef) {
            if (attrdef == NULL) {
                attrdef = (AttrDefault *) palloc(relation->rd_rel->relnatts *
                        sizeof (AttrDefault));
                MemSet(attrdef, 0,
                        relation->rd_rel->relnatts * sizeof (AttrDefault));
            }
            attrdef[ndef].adnum = i;
            attrdef[ndef].adbin = NULL;
            ndef++;
        }
    }

    heap_close(attrel, AccessShareLock);

    SetConstrOfRelation(relation, constr, ndef, attrdef);

}

/* --------------------------------
 *		RelationBuildRuleLock
 *
 *		Form the relation's rewrite rules from information in
 *		the pg_rewrite system catalog.
 * --------------------------------
 */
static void
RelationBuildRuleLock(Relation relation) {
    HeapTuple pg_rewrite_tuple;
    Relation pg_rewrite_desc;
    TupleDesc pg_rewrite_tupdesc;
    HeapScanDesc pg_rewrite_scan;
    ScanKeyData key;
    RuleLock *rulelock;
    int numlocks;
    RewriteRule **rules;
    int maxlocks;

    /* ----------------
     *	form an array to hold the rewrite rules (the array is extended if
     *	necessary)
     * ----------------
     */
    maxlocks = 4;
    rules = (RewriteRule **) palloc(sizeof (RewriteRule *) * maxlocks);
    numlocks = 0;

    /* ----------------
     *	form a scan key
     * ----------------
     */
    ScanKeyEntryInitialize(&key, 0,
            Anum_pg_rewrite_ev_class,
            F_OIDEQ,
            ObjectIdGetDatum(RelationGetRelid(relation)));

    /* ----------------
     *	open pg_rewrite and begin a scan
     * ----------------
     */
    pg_rewrite_desc = heap_openr(RewriteRelationName, AccessShareLock);
    pg_rewrite_scan = heap_beginscan(pg_rewrite_desc, SnapshotNow, 1, &key);
    pg_rewrite_tupdesc = RelationGetDescr(pg_rewrite_desc);

    while (HeapTupleIsValid(pg_rewrite_tuple = heap_getnext(pg_rewrite_scan))) {
        bool isnull;
        Datum ruleaction;
        Datum rule_evqual;
        char *ruleaction_str;
        char *rule_evqual_str;
        RewriteRule *rule;

        rule = (RewriteRule *) palloc(sizeof (RewriteRule));

        rule->ruleId = pg_rewrite_tuple->t_data->t_oid;

        rule->event = (int) HeapGetAttr(pg_rewrite_tuple,
                Anum_pg_rewrite_ev_type, pg_rewrite_tupdesc,
                &isnull) - 48;
        rule->attrno = (int) HeapGetAttr(pg_rewrite_tuple,
                Anum_pg_rewrite_ev_attr, pg_rewrite_tupdesc,
                &isnull);
        rule->isInstead = !!HeapGetAttr(pg_rewrite_tuple,
                Anum_pg_rewrite_is_instead, pg_rewrite_tupdesc,
                &isnull);

        ruleaction = HeapGetAttr(pg_rewrite_tuple,
                Anum_pg_rewrite_ev_action,
                pg_rewrite_tupdesc,
                &isnull);
        ruleaction_str = lztextout((lztext *) DatumGetPointer(ruleaction));
        rule->actions = (List *) stringToNode(ruleaction_str);
        pfree(ruleaction_str);

        rule_evqual = HeapGetAttr(pg_rewrite_tuple,
                Anum_pg_rewrite_ev_qual,
                pg_rewrite_tupdesc,
                &isnull);
        rule_evqual_str = lztextout((lztext *) DatumGetPointer(rule_evqual));
        rule->qual = (Node *) stringToNode(rule_evqual_str);
        pfree(rule_evqual_str);

        if (numlocks >= maxlocks) {
            maxlocks *= 2;
            rules = (RewriteRule **) repalloc(rules, sizeof (RewriteRule *) * maxlocks);
        }
        rules[numlocks++] = rule;
    }

    /* ----------------
     *	end the scan and close the attribute relation
     * ----------------
     */
    heap_endscan(pg_rewrite_scan);
    heap_close(pg_rewrite_desc, AccessShareLock);

    /* ----------------
     *	form a RuleLock and insert into relation
     * ----------------
     */
    rulelock = (RuleLock *) palloc(sizeof (RuleLock));
    rulelock->numLocks = numlocks;
    rulelock->rules = rules;

    relation->rd_rules = rulelock;
}

/* --------------------------------
 *		FreeRuleLock
 *
 *		Release the storage used for a set of rewrite rules.
 *
 *		Probably this should be in the rules code someplace...
 * --------------------------------
 */
static void
FreeRuleLock(RuleLock *rlock) {
    int i;

    if (rlock == NULL)
        return;
    for (i = 0; i < rlock->numLocks; i++) {
        RewriteRule *rule = rlock->rules[i];

#if 0							/* does freefuncs.c still work?  Not sure */
        freeObject(rule->actions);
        freeObject(rule->qual);
#endif
        pfree(rule);
    }
    pfree(rlock->rules);
    pfree(rlock);
}

/* --------------------------------
 *		equalRuleLocks
 *
 *		Determine whether two RuleLocks are equivalent
 *
 *		Probably this should be in the rules code someplace...
 * --------------------------------
 */
static bool
equalRuleLocks(RuleLock *rlock1, RuleLock *rlock2) {
    int i,
            j;

    if (rlock1 != NULL) {
        if (rlock2 == NULL)
            return false;
        if (rlock1->numLocks != rlock2->numLocks)
            return false;
        for (i = 0; i < rlock1->numLocks; i++) {
            RewriteRule *rule1 = rlock1->rules[i];
            RewriteRule *rule2 = NULL;

            /*
             * We can't assume that the rules are always read from
             * pg_rewrite in the same order; so use the rule OIDs to
             * identify the rules to compare.  (We assume here that the
             * same OID won't appear twice in either ruleset.)
             */
            for (j = 0; j < rlock2->numLocks; j++) {
                rule2 = rlock2->rules[j];
                if (rule1->ruleId == rule2->ruleId)
                    break;
            }
            if (j >= rlock2->numLocks)
                return false;
            if (rule1->event != rule2->event)
                return false;
            if (rule1->attrno != rule2->attrno)
                return false;
            if (rule1->isInstead != rule2->isInstead)
                return false;
            if (!equal(rule1->qual, rule2->qual))
                return false;
            if (!equal(rule1->actions, rule2->actions))
                return false;
        }
    } else if (rlock2 != NULL)
        return false;
    return true;
}

/* --------------------------------
 *		RelationBuildDesc
 *
 *		Build a relation descriptor --- either a new one, or by
 *		recycling the given old relation object.  The latter case
 *		supports rebuilding a relcache entry without invalidating
 *		pointers to it.
 *
 *		To build a relation descriptor, we have to allocate space,
 *		open the underlying unix file and initialize the following
 *		fields:
 *
 *	File				   rd_fd;		 open file descriptor
 *	BlockNumber					   rd_nblocks;	 number of blocks in rel
 *										 it will be set in ambeginscan()
 *	uint16				   rd_refcnt;	 reference count
 *	Form_pg_am			   rd_am;		 AM tuple
 *	Form_pg_class		   rd_rel;		 RELATION tuple
 *	Oid					   rd_id;		 relation's object id
 *	LockInfoData		   rd_lockInfo;  lock manager's info
 *	TupleDesc			   rd_att;		 tuple descriptor
 *
 *		Note: rd_ismem (rel is in-memory only) is currently unused
 *		by any part of the system.	someday this will indicate that
 *		the relation lives only in the main-memory buffer pool
 *		-cim 2/4/91
 * --------------------------------
 */
static Relation
RelationBuildDesc(RelationBuildDescInfo buildinfo,
        Relation oldrelation) {
    Relation relation;
    u_int natts;
    Oid relid;
    Oid relam;
    HeapTuple pg_class_tuple;
    Form_pg_class relp;

    /* ----------------
     *	find the tuple in pg_class corresponding to the given relation id
     * ----------------
     */
    pg_class_tuple = ScanPgRelation(buildinfo);

    /* ----------------
     *	if no such tuple exists, return NULL
     * ----------------
     */
    if (!HeapTupleIsValid(pg_class_tuple))
        return NULL;

    /* ----------------
     *	get information from the pg_class_tuple
     * ----------------
     */
    relid = pg_class_tuple->t_data->t_oid;
    relp = (Form_pg_class) GETSTRUCT(pg_class_tuple);
    natts = relp->relnatts;

    /* ----------------
     *	allocate storage for the relation descriptor,
     *	initialize relation->rd_rel and get the access method id.
     *	The storage is allocated in memory context CacheCxt.
     * ----------------
     */
    relation = AllocateRelationDesc(oldrelation, natts, relp);
    relam = relation->rd_rel->relam;

    /* ----------------
     *	initialize the relation's relation id (relation->rd_id)
     * ----------------
     */
    RelationGetRelid(relation) = relid;

    /* ----------------
     *	initialize relation->rd_refcnt
     * ----------------
     */
    RelationSetReferenceCount(relation, 1);

    /* ----------------
     *	 normal relations are not nailed into the cache
     * ----------------
     */
    relation->rd_isnailed = false;

    /* ----------------
     *	initialize the access method information (relation->rd_am)
     * ----------------
     */
    if (OidIsValid(relam))
        relation->rd_am = AccessMethodObjectIdGetForm(relam);

    /* ----------------
     *	initialize the tuple descriptor (relation->rd_att).
     * ----------------
     */
    RelationBuildTupleDesc(buildinfo, relation, natts);

    /* ----------------
     *	initialize the relation lock manager information
     * ----------------
     */
    RelationInitLockInfo(relation); /* see lmgr.c */

    relation->rd_smgr = NULL;
    relation->readtrigger = NULL;
    relation->buffer_cxt = GetBufferCxt();
    relation->snapshot_cxt = GetSnapshotHolder();
    /* ----------------
     *	insert newly created relation into proper relcaches,
     *	restore memory context and return the new reldesc.
     * ----------------
     *  Insert here to prevent infinte recursion caused 
     *  by inserting 
     */
    RelationCacheInsert(relation);

    /* ----------------
     *	initialize rules that affect this relation
     * ----------------
     */
    if (relp->relhasrules)
        RelationBuildRuleLock(relation);
    else
        relation->rd_rules = NULL;

    /* Triggers */
    if (relp->reltriggers > 0)
        RelationBuildTriggers(relation);
    else
        relation->trigdesc = NULL;

    /* ----------------
     *	initialize index strategy and support information for this relation
     * ----------------
     */
    if (OidIsValid(relam))
        IndexedAccessMethodInitialize(relation);
    /* -------------------
     *	free the memory allocated for pg_class_tuple
     *	and for lock data pointed to by pg_class_tuple
     * -------------------
     */
    heap_freetuple(pg_class_tuple);

    return relation;
}
/*  copy a relation from another memory context
        assumes you are already in your own memory context */

/*  b/c the original is in global memory, we can copy only the first level 
 b/c that is the only part that changes   */

static Relation
RelationCopyRelation(Relation target) {
    Relation copy = (Relation) palloc(sizeof (RelationData));
    memcpy(copy, target, sizeof (RelationData));
    copy->rd_smgr = NULL;
    copy->readtrigger = NULL;
    copy->buffer_cxt = GetBufferCxt();
    copy->snapshot_cxt = GetSnapshotHolder();
    return copy;
}

static void
IndexedAccessMethodInitialize(Relation relation) {
    IndexStrategy strategy;
    RegProcedure *support;
    int natts;
    Size stratSize;
    Size supportSize;
    uint16 relamstrategies;
    uint16 relamsupport;

    natts = relation->rd_rel->relnatts;
    relamstrategies = relation->rd_am->amstrategies;
    stratSize = AttributeNumberGetIndexStrategySize(natts, relamstrategies);
    strategy = (IndexStrategy) palloc(stratSize);
    relamsupport = relation->rd_am->amsupport;

    if (relamsupport > 0) {
        supportSize = natts * (relamsupport * sizeof (RegProcedure));
        support = (RegProcedure *) palloc(supportSize);
    } else
        support = (RegProcedure *) NULL;

    IndexSupportInitialize(strategy, support,
            relation->rd_att->attrs[0]->attrelid,
            relation->rd_rel->relam,
            relamstrategies, relamsupport, natts);

    RelationSetIndexSupport(relation, strategy, support);
}

/* --------------------------------
 *		formrdesc
 *
 *		This is a special version of RelationBuildDesc()
 *		used by RelationInitialize() in initializing the
 *		relcache.  The system relation descriptors built
 *		here are all nailed in the descriptor caches, for
 *		bootstrapping.
 * --------------------------------
 */
static void
formrdesc(char *relationName,
        u_int natts,
        FormData_pg_attribute *att, char relkind) {
    Relation relation;
    Size len;
    u_int i;

    /* ----------------
     *	allocate new relation desc
     * ----------------
     */
    len = sizeof (RelationData);
    relation = (Relation) palloc(len);
    MemSet((char *) relation, 0, len);

    /* ----------------
     *	don't open the unix file yet..
     * ----------------
     */
    relation->rd_smgr = NULL;
    relation->readtrigger = NULL;

    /* ----------------
     *	initialize reference count
     * ----------------
     */
    RelationSetReferenceCount(relation, 1);

    /* ----------------
     *	initialize relation tuple form
     * ----------------
     */
    relation->rd_rel = (Form_pg_class)
            palloc((Size) (sizeof (*relation->rd_rel)));
    MemSet(relation->rd_rel, 0, sizeof (FormData_pg_class));
    strcpy(RelationGetPhysicalRelationName(relation), relationName);

    /* ----------------
       initialize attribute tuple form
     */
    relation->rd_att = CreateTemplateTupleDesc(natts);

    /*
     * For debugging purposes, it's important to distinguish between
     * shared and non-shared relations, even at bootstrap time.  There's
     * code in the buffer manager that traces allocations that has to know
     * about this.
     */

    if (IsSystemRelationName(relationName)) {
        relation->rd_rel->relowner = 6; /* XXX use sym const */
        relation->rd_rel->relisshared = IsSharedSystemRelationName(relationName);
    } else {
        relation->rd_rel->relowner = 0;
        relation->rd_rel->relisshared = false;
    }

    relation->rd_rel->relpages = 1; /* XXX */
    relation->rd_rel->reltuples = 1; /* XXX */
    relation->rd_rel->relkind = relkind;
    relation->rd_rel->relnatts = (uint16) natts;
    relation->rd_isnailed = true;

    /* ----------------
     *	initialize tuple desc info
     * ----------------
     */
    for (i = 0; i < natts; i++) {
        relation->rd_att->attrs[i] = (Form_pg_attribute) palloc(ATTRIBUTE_TUPLE_SIZE);
        memcpy((char *) relation->rd_att->attrs[i],
                (char *) &att[i],
                ATTRIBUTE_TUPLE_SIZE);
    }

    /* ----------------
     *	initialize relation id
     * ----------------
     */
    RelationGetRelid(relation) = relation->rd_att->attrs[0]->attrelid;

    /* ----------------
     *	initialize the relation lock manager information
     * ----------------
     */
    RelationInitLockInfo(relation); /* see lmgr.c */

    relation->buffer_cxt = GetBufferCxt();
    relation->snapshot_cxt = GetSnapshotHolder();
    /* ----------------
     *	add new reldesc to relcache
     * ----------------
     */
    RelationCacheInsert(relation);

    /*
     * Determining this requires a scan on pg_class, but to do the scan
     * the rdesc for pg_class must already exist.  Therefore we must do
     * the check (and possible set) after cache insertion.
     */
    relation->rd_rel->relhasindex =
            CatalogHasIndex(relationName, RelationGetRelid(relation));
}


/* ----------------------------------------------------------------
 *				 Relation Descriptor Lookup Interface
 * ----------------------------------------------------------------
 */

/* --------------------------------
 *		RelationIdCacheGetRelation
 *
 *		Lookup a reldesc by OID.
 *		Only try to get the reldesc by looking up the cache
 *		do not go to the disk.
 *
 *		NB: relation ref count is incremented if successful.
 *		Caller should eventually decrement count.  (Usually,
 *		that happens by calling RelationClose().)
 * --------------------------------
 */
Relation
RelationIdCacheGetRelation(Oid relationId, Oid databaseId) {
    Relation rd;

    rd = RelationIdCacheLookup(&relationId);

    if (RelationIsValid(rd)) {
        if (rd->rd_smgr == NULL) {
            rd->rd_smgr = smgropen(DEFAULT_SMGR, GetDatabaseName(), RelationGetRelationName(rd),
                    rd->rd_lockInfo.lockRelId.dbId,
                    rd->rd_lockInfo.lockRelId.relId);
        }

        RelationIncrementReferenceCount(rd);

    }

    return rd;
}

/* --------------------------------
 *		RelationNameCacheGetRelation
 *
 *		As above, but lookup by name.
 * --------------------------------
 */
static Relation
RelationNameCacheGetRelation(const char *relationName, Oid databaseId) {
    Relation rd;
    NameData name;

    /*
     * make sure that the name key used for hash lookup is properly
     * null-padded  -- this is a string hash, not needed
     */

    namestrcpy(&name, relationName);
    rd = RelationNameCacheLookup((char*) NameStr(name));

    if (RelationIsValid(rd)) {
        if (rd->rd_smgr == NULL) {
            rd->rd_smgr = smgropen(DEFAULT_SMGR, GetDatabaseName(), RelationGetRelationName(rd),
                    rd->rd_lockInfo.lockRelId.dbId,
                    rd->rd_lockInfo.lockRelId.relId);
        }

        RelationIncrementReferenceCount(rd);

    }

    return rd;
}

/* --------------------------------
 *		RelationIdGetRelation
 *
 *		Lookup a reldesc by OID; make one if not already in cache.
 *
 *		NB: relation ref count is incremented, or set to 1 if new entry.
 *		Caller should eventually decrement count.  (Usually,
 *		that happens by calling RelationClose().)
 * --------------------------------
 */
Relation
RelationIdGetRelation(Oid relationId, Oid databaseId) {
    Relation rd;
    RelationBuildDescInfo buildinfo;
    MemoryContext oldcxt;
    RelationCacheGlobal* rglobal = GetRelationCacheGlobal();

    /* ----------------
     *	increment access statistics
     * ----------------
     */

#ifdef USESTATS         
    IncrHeapAccessStat(local_RelationIdGetRelation);
    IncrHeapAccessStat(global_RelationIdGetRelation);
#endif
    /* ----------------
     *	first try and get a reldesc from the cache
     * ----------------
     */
    rd = RelationIdCacheGetRelation(relationId, databaseId);
    if (!RelationIsValid(rd)) {
        /* ----------------
         *	no reldesc in the cache, so have RelationBuildDesc()
         *	build one and add it.
         * ----------------
         */
        buildinfo.infotype = INFO_RELID;
        buildinfo.i.info_id = relationId;

        oldcxt = MemoryContextSwitchTo(rglobal->rcache_cxt);
        rd = RelationBuildDesc(buildinfo, NULL);
        MemoryContextSwitchTo(oldcxt);
    }

    if (RelationIsValid(rd)) {
        if (rd->rd_smgr == NULL) {
            rd->rd_smgr = smgropen(DEFAULT_SMGR, GetDatabaseName(), RelationGetRelationName(rd),
                    rd->rd_lockInfo.lockRelId.dbId,
                    rd->rd_lockInfo.lockRelId.relId);
        }
    }


    return rd;
}

/* --------------------------------
 *		RelationNameGetRelation
 *
 *		As above, but lookup by name.
 * --------------------------------
 */
Relation
RelationNameGetRelation(const char *relationName, Oid databaseId) {
    char* temprelname;
    Relation rd;
    RelationBuildDescInfo buildinfo;
    MemoryContext oldcxt;
    RelationCacheGlobal* rglobal = GetRelationCacheGlobal();
    /* ----------------
     *	increment access statistics
     * ----------------
     */
#ifdef USESTATS
    IncrHeapAccessStat(local_RelationNameGetRelation);
    IncrHeapAccessStat(global_RelationNameGetRelation);
#endif
    /* ----------------
     *	if caller is looking for a temp relation, substitute its real name;
     *	we only index temp rels by their real names.
     * ----------------
     */
    temprelname = get_temp_rel_by_username(relationName);
    if (temprelname)
        relationName = temprelname;

    /* ----------------
     *	first try and get a reldesc from the cache
     * ----------------
     */
    rd = RelationNameCacheGetRelation(relationName, databaseId);
    if (!RelationIsValid(rd)) {
        /* ----------------
         *	no reldesc in the cache, so have RelationBuildDesc()
         *	build one and add it.
         * ----------------
         */
        buildinfo.infotype = INFO_RELNAME;
        namestrcpy(&buildinfo.i.info_name,relationName);

        oldcxt = MemoryContextSwitchTo(rglobal->rcache_cxt);
        rd = RelationBuildDesc(buildinfo, NULL);
        MemoryContextSwitchTo(oldcxt);
    }
    /*  make sure the file is open */
    if (RelationIsValid(rd)) {
        if (rd->rd_smgr == NULL) {
            rd->rd_smgr = smgropen(DEFAULT_SMGR, GetDatabaseName(), RelationGetRelationName(rd),
                    rd->rd_lockInfo.lockRelId.dbId,
                    rd->rd_lockInfo.lockRelId.relId);
        }
    } else {
//        elog(NOTICE, "Relation not found %s", relationName);
    }

    return rd;
}


/* ----------------------------------------------------------------
 *				cache invalidation support routines
 * ----------------------------------------------------------------
 */

/* --------------------------------
 * RelationClose - close an open relation
 *
 *	 Actually, we just decrement the refcount.
 * --------------------------------
 */
void
RelationClose(Relation relation) {
    /* Note: no locking manipulations needed */
    RelationDecrementReferenceCount(relation);
}

/*  close all the file descriptors we reserved in relcache
        when we are about to close down the connection
	
        MKS  12.23.2000
 */
static void
RelationShutdown(RelNameCacheEnt* ptr, int dummy) {
    if (ptr->reldesc->rd_smgr != NULL) {
        smgrclose(ptr->reldesc->rd_smgr);
        ptr->reldesc->rd_smgr = NULL;
        ptr->reldesc->readtrigger = NULL;
    }
}

/* --------------------------------
 * RelationClearRelation
 *
 *	 Physically blow away a relation cache entry, or reset it and rebuild
 *	 it from scratch (that is, from catalog entries).  The latter path is
 *	 usually used when we are notified of a change to an open relation
 *	 (one with refcount > 0).  However, this routine just does whichever
 *	 it's told to do; callers must determine which they want.
 *
 *	 If we detect a change in the relation's TupleDesc or trigger data
 *	 while rebuilding, we complain unless refcount is 0.
 * --------------------------------
 */
static void
RelationClearRelation(Relation relation, bool rebuildIt) {

    /*
     * Make sure smgr and lower levels close the relation's files, if they
     * weren't closed already.  We do this unconditionally; if the
     * relation is not deleted, the next smgr access should reopen the
     * files automatically.  This ensures that the low-level file access
     * state is updated after, say, a vacuum truncation.
     *
     * NOTE: this call is a no-op if the relation's smgr file is already
     * closed or unlinked.
     */
    if (relation->rd_smgr != NULL) {
        smgrclose(relation->rd_smgr);
        relation->rd_smgr = NULL;
        relation->readtrigger = NULL;
    }

    /*
     * Never, never ever blow away a nailed-in system relation, because
     * we'd be unable to recover.
     */
    if (relation->rd_isnailed) {
        /*  reopen the smgr  */
        relation->rd_smgr = smgropen(DEFAULT_SMGR, GetDatabaseName(), RelationGetRelationName(relation),
                relation->rd_lockInfo.lockRelId.dbId,
                relation->rd_lockInfo.lockRelId.relId);
        relation->rd_nblocks = 0;

        return;
    }
    /*
     * Remove relation from hash tables
     *
     * Note: we might be reinserting it momentarily, but we must not have it
     * visible in the hash tables until it's valid again, so don't try to
     * optimize this away...
     */
    RelationCacheDelete(relation);

    /*
     * Free all the subsidiary data structures of the relcache entry. We
     * cannot free rd_att if we are trying to rebuild the entry, however,
     * because pointers to it may be cached in various places. The trigger
     * manager might also have pointers into the trigdesc, and the rule
     * manager might have pointers into the rewrite rules. So to begin
     * with, we can only get rid of these fields:
     */

    if (relation->rd_am)
        pfree(relation->rd_am);
    if (relation->rd_rel)
        pfree(relation->rd_rel);
    if (relation->rd_istrat)
        pfree(relation->rd_istrat);
    if (relation->rd_support)
        pfree(relation->rd_support);
    if (relation->rd_indexlist) {
        freeList(relation->rd_indexlist);
    }




    /*
     * If we're really done with the relcache entry, blow it away. But if
     * someone is still using it, reconstruct the whole deal without
     * moving the physical RelationData record (so that the someone's
     * pointer is still valid).
     */


    if (!rebuildIt) {
        /* ok to zap remaining substructure */
        FreeTupleDesc(relation->rd_att);
        FreeRuleLock(relation->rd_rules);
        FreeTriggerDesc(relation->trigdesc);
        pfree(relation);
    } else {

        /*
         * When rebuilding an open relcache entry, must preserve ref count
         * and myxactonly flag.  Also attempt to preserve the tupledesc,
         * rewrite rules, and trigger substructures in place. Furthermore
         * we save/restore rd_nblocks (in case it is a local relation)
         * *and* call RelationGetNumberOfBlocks (in case it isn't).
         */
        uint16 old_refcnt = relation->rd_refcnt;
        bool old_myxactonly = relation->rd_myxactonly;
        TupleDesc old_att = relation->rd_att;
        RuleLock *old_rules = relation->rd_rules;
        TriggerDesc *old_trigdesc = relation->trigdesc;
        bool relDescChanged = false;
        RelationBuildDescInfo buildinfo;
        MemoryContext oldcxt = NULL;
        Relation temprel;
        RelationCacheGlobal* rglobal = GetRelationCacheGlobal();

        buildinfo.infotype = INFO_RELID;
        buildinfo.i.info_id = RelationGetRelid(relation);

        oldcxt = MemoryContextSwitchTo(rglobal->rcache_cxt);
        temprel = RelationBuildDesc(buildinfo, relation);
        MemoryContextSwitchTo(oldcxt);

        if (temprel != relation) {
            /* Should only get here if relation was deleted */
            FreeTupleDesc(old_att);
            FreeRuleLock(old_rules);
            FreeTriggerDesc(old_trigdesc);
            pfree(relation);
            elog(ERROR, "RelationClearRelation: relation %lu deleted while still in use",
                    buildinfo.i.info_id);
        }
        RelationSetReferenceCount(relation, old_refcnt);
        relation->rd_myxactonly = old_myxactonly;
        if (equalTupleDescs(old_att, relation->rd_att)) {
            FreeTupleDesc(relation->rd_att);
            relation->rd_att = old_att;
        } else {
            FreeTupleDesc(old_att);
            relDescChanged = true;
        }
        if (equalRuleLocks(old_rules, relation->rd_rules)) {
            FreeRuleLock(relation->rd_rules);
            relation->rd_rules = old_rules;
        } else {
            FreeRuleLock(old_rules);
            relDescChanged = true;
        }
        if (equalTriggerDescs(old_trigdesc, relation->trigdesc)) {
            FreeTriggerDesc(relation->trigdesc);
            relation->trigdesc = old_trigdesc;
        } else {
            FreeTriggerDesc(old_trigdesc);
            relDescChanged = true;
        }
        relation->rd_nblocks = 0;

        if (relDescChanged && old_refcnt > 0) {
            elog(NOTICE, "RelationClearRelation: relation %lu modified while in use %d",
                    buildinfo.i.info_id, old_refcnt);
        }

        /*  reopen the smgr  */
        relation->rd_smgr = smgropen(DEFAULT_SMGR, GetDatabaseName(), RelationGetRelationName(relation),
                relation->rd_lockInfo.lockRelId.dbId,
                relation->rd_lockInfo.lockRelId.relId);

    }

}

/* --------------------------------
 * RelationFlushRelation
 *
 *	 Rebuild the relation if it is open (refcount > 0), else blow it away.
 *	 If skipLocalRelations is TRUE, xact-local relations are ignored
 *	 (which is useful when processing SI cache reset, since xact-local
 *	 relations could not be targets of notifications from other backends).
 *
 *	 The peculiar calling convention (pointer to pointer to relation)
 *	 is needed so that we can use this routine as a hash table walker.
 * --------------------------------
 */
static void
RelationFlushRelation(RelNameCacheEnt *relationPtr,
        int skipLocalRelations) {
    Relation relation = relationPtr->reldesc;
    bool rebuildIt;

    if (relation->rd_myxactonly) {
        if (skipLocalRelations)
            return; /* don't touch local rels if so commanded */

        /*
         * Local rels should always be rebuilt, not flushed; the relcache
         * entry must live until RelationPurgeLocalRelation().
         */
        rebuildIt = true;
    } else {

        /*
         * Nonlocal rels can be dropped from the relcache if not open.
         */
        rebuildIt = !RelationHasReferenceCountZero(relation);
    }

    RelationClearRelation(relation, rebuildIt);
}

/* --------------------------------
 * RelationForgetRelation -
 *
 *		   RelationClearRelation + if the relation is myxactonly then
 *		   remove the relation descriptor from the newly created
 *		   relation list.
 * --------------------------------
 */
void
RelationForgetRelation(Oid rid, Oid did) {
    Relation relation;
    RelationCacheGlobal* rglobal = GetRelationCacheGlobal();

    relation = RelationIdCacheLookup(&rid);

    if (PointerIsValid(relation)) {
        if (relation->rd_myxactonly) {
            List *curr;
            List *prev = NIL;

            foreach(curr, rglobal->newlyCreatedRelns) {
                Relation reln = lfirst(curr);

                Assert(reln != NULL && reln->rd_myxactonly);
                if (RelationGetRelid(reln) == rid)
                    break;
                prev = curr;
            }
            if (curr == NIL)
                elog(FATAL, "Local relation %s not found in list",
                    RelationGetRelationName(relation));
            if (prev == NIL)
                rglobal->newlyCreatedRelns = lnext(rglobal->newlyCreatedRelns);
            else
                lnext(prev) = lnext(curr);
            pfree(curr);
        }

        /* Unconditionally destroy the relcache entry */
        RelationClearRelation(relation, false);
    }
}

/* --------------------------------
 *		RelationIdInvalidateRelationCache
 *
 *		This routine is invoked for SI cache flush messages.
 *
 *		We used to skip local relations, on the grounds that they could
 *		not be targets of cross-backend SI update messages; but it seems
 *		safer to process them, so that our *own* SI update messages will
 *		have the same effects during CommandCounterIncrement for both
 *		local and nonlocal relations.
 * --------------------------------
 */
void
RelationIdInvalidateRelationCache(Oid relationId, Oid databaseId) {
    RelNameCacheEnt entry;
    Relation relation;

    relation = RelationIdCacheLookup(&relationId);

    if (PointerIsValid(relation)) {
        entry.reldesc = relation;
        RelationFlushRelation(&entry, false);
    }
}

/*
 * RelationCacheInvalidate
 *	 Blow away cached relation descriptors that have zero reference counts,
 *	 and rebuild those with positive reference counts.
 *
 *	 This is currently used only to recover from SI message buffer overflow,
 *	 so we do not touch transaction-local relations; they cannot be targets
 *	 of cross-backend SI updates (and our own updates now go through a
 *	 separate linked list that isn't limited by the SI message buffer size).
 */
void
RelationCacheInvalidate(void) {
    RelationCacheGlobal* rglobal = GetRelationCacheGlobal();
    HashTableWalk(rglobal->RelationNameCache, (HashtFunc) RelationFlushRelation,
            (long) true);
    ResetSystemCache();

}

/*
 * RelationCacheClose walk the relation name cache to shutdown file descriptors
 *	on connection close
 *	 Shutdown case  MKS 12.23.2000
 */
void
RelationCacheShutdown(void) {
    RelationCacheGlobal* rglobal = GetRelationCacheGlobal();

    HashTableWalk(rglobal->RelationNameCache, (HashtFunc) RelationShutdown,
            (long) false);
    /*	ResetSystemCache();	*/
}

/*
 * RelationCacheAbort
 *
 *	Clean up the relcache at transaction abort.
 *
 *	What we need to do here is reset relcache entry ref counts to
 *	their normal not-in-a-transaction state.  A ref count may be
 *	too high because some routine was exited by elog() between
 *	incrementing and decrementing the count.
 *
 *	XXX Maybe we should do this at transaction commit, too, in case
 *	someone forgets to decrement a refcount in a non-error path?
 */
void
RelationCacheAbort(void) {
    RelationCacheGlobal* rglobal = GetRelationCacheGlobal();

    HashTableWalk(rglobal->RelationNameCache, (HashtFunc) RelationCacheAbortWalker, 0);
}

void
RelationCacheCommit(void) {
    if (IsBootstrapProcessingMode()) return;
    RelationCacheGlobal* rglobal = GetRelationCacheGlobal();

    HashTableWalk(rglobal->RelationNameCache, (HashtFunc) RelationCacheCommitChecker, 0);
}

void
RelationCacheWalk(HashtFunc func, int arg) {
    RelationCacheGlobal* rglobal = GetRelationCacheGlobal();

    HashTableWalk(rglobal->RelationNameCache, func, arg);
}

static void
RelationCacheCommitChecker(RelNameCacheEnt *relationPtr, int dummy) {
    Relation relation = relationPtr->reldesc;

    if (!relation->rd_isnailed && !RelationHasReferenceCountZero(relation)) {
        elog(DEBUG, "relation %s has refcount of %d at commit", RelationGetRelationName(relation), relation->rd_refcnt);
    }

    if ( relation->readtrigger ) {
        if ( relation->readtrigger->when == TRIGGER_COMMIT ) {
            relation->readtrigger->call(relation, relation->readtrigger->args);
        }
    }
    relation->readtrigger = NULL;
}

static void
RelationCacheAbortWalker(RelNameCacheEnt *relationPtr, int dummy) {
    Relation relation = relationPtr->reldesc;

    if (relation->rd_isnailed)
        RelationSetReferenceCount(relation, 1);
    else
        RelationSetReferenceCount(relation, 0);

    relation->readtrigger = NULL;
}

/* --------------------------------
 *		RelationRegisterRelation -
 *		   register the Relation descriptor of a newly created relation
 *		   with the relation descriptor Cache.
 * --------------------------------
 */
void
RelationRegisterRelation(Relation relation) {
    MemoryContext oldcxt;
    RelationCacheGlobal* rglobal = GetRelationCacheGlobal();

    relation->buffer_cxt = GetBufferCxt();
    relation->snapshot_cxt = GetSnapshotHolder();

    RelationInitLockInfo(relation);

    RelationCacheInsert(relation);

    /*
     * we've just created the relation. It is invisible to anyone else
     * before the transaction is committed. Setting rd_myxactonly allows
     * us to use the local buffer manager for select/insert/etc before the
     * end of transaction. (We also need to keep track of relations
     * created during a transaction and does the necessary clean up at the
     * end of the transaction.)				- ay 3/95
     */
    oldcxt = MemoryContextSwitchTo(MemoryContextGetEnv()->TopTransactionContext != NULL ? MemoryContextGetEnv()->TopTransactionContext :
        MemoryContextGetEnv()->QueryContext);

    relation->rd_myxactonly = TRUE;
    rglobal->newlyCreatedRelns = lcons(relation, rglobal->newlyCreatedRelns);

    MemoryContextSwitchTo(oldcxt);
}

/*
 * RelationPurgeLocalRelation -
 *	  find all the Relation descriptors marked rd_myxactonly and reset them.
 *	  This should be called at the end of a transaction (commit/abort) when
 *	  the "local" relations will become visible to others and the multi-user
 *	  buffer pool should be used.
 */
void
RelationPurgeLocalRelation(bool xactCommitted) {
    RelationCacheGlobal* rglobal = GetRelationCacheGlobal();

    if (rglobal->newlyCreatedRelns == NULL)
        return;

    while (rglobal->newlyCreatedRelns) {
        List *l = rglobal->newlyCreatedRelns;
        Relation reln = lfirst(l);

        Assert(reln != NULL && reln->rd_myxactonly);

        reln->rd_myxactonly = false; /* mark it not on list anymore */

        rglobal->newlyCreatedRelns = lnext(rglobal->newlyCreatedRelns);
        pfree(l);

        if (!xactCommitted) {

            /*
             * remove the file if we abort. This is so that files for
             * tables created inside a transaction block get removed.
             */
            if (reln->rd_isnoname) {
                if (!(reln->rd_unlinked)) {
                    if (reln->rd_smgr != NULL) smgrunlink(reln->rd_smgr);
                    reln->rd_smgr = NULL;
                    reln->rd_unlinked = TRUE;
                    reln->readtrigger = NULL;
                }
            } else {
                if (!(reln->rd_unlinked)) {
                    if (reln->rd_smgr != NULL) smgrunlink(reln->rd_smgr);
                    reln->rd_smgr = NULL;
                    reln->rd_unlinked = TRUE;
                    reln->readtrigger = NULL;
                }
            }
        }

        if (!IsBootstrapProcessingMode()) {
            RelationClearRelation(reln, false);
        }
    }
}

/* --------------------------------
 *		RelationInitialize
 *
 *		This initializes the relation descriptor cache.
 * --------------------------------
 */

#define INITRELCACHESIZE		400

void
RelationInitialize(void) {
    MemoryContext oldcxt;
    HASHCTL ctl;
    RelationCacheGlobal* rglobal = GetRelationCacheGlobal();

    /* ----------------
     *	switch to cache memory context
     * ----------------
     */

    oldcxt = MemoryContextSwitchTo(rglobal->rcache_cxt);

    /* ----------------
     *	create global caches
     * ----------------
     */
    MemSet(&ctl, 0, (int) sizeof (ctl));
    ctl.keysize = sizeof (NameData);
    ctl.entrysize = sizeof (RelNameCacheEnt);
    rglobal->RelationNameCache = hash_create("relation name cache", INITRELCACHESIZE, &ctl, HASH_ELEM);

    MemSet(&ctl, 0, (int) sizeof (ctl));
    ctl.keysize = sizeof (Oid);
    ctl.entrysize = sizeof (RelIdCacheEnt);
    ctl.hash = tag_hash;
    rglobal->RelationIdCache = hash_create("relation id cache", INITRELCACHESIZE, &ctl,
            HASH_ELEM | HASH_FUNCTION);
    /* ----------------
     *	initialize the cache with pre-made relation descriptors
     *	for some of the more important system relations.  These
     *	relations should always be in the cache.
     * ----------------
     */
    /*  
     *	DBWriter only wants to know about the LogRelation
     *	all other Relations are going to be added as fakies
     *	MKS  1.31.2001
     */
    if (!IsDBWriter()) {
        formrdesc(RelationRelationName, Natts_pg_class, Desc_pg_class, RELKIND_RELATION);
        formrdesc(AttributeRelationName, Natts_pg_attribute, Desc_pg_attribute, RELKIND_RELATION);
        formrdesc(ProcedureRelationName, Natts_pg_proc, Desc_pg_proc, RELKIND_RELATION);
        formrdesc(TypeRelationName, Natts_pg_type, Desc_pg_type, RELKIND_RELATION);
    }
    formrdesc(VariableRelationName, Natts_pg_variable, Desc_pg_variable, RELKIND_SPECIAL);
    formrdesc(LogRelationName, Natts_pg_log, Desc_pg_log, RELKIND_SPECIAL);

    /*
     * If this isn't initdb time, then we want to initialize some index
     * relation descriptors, as well.  The descriptors are for
     * pg_attnumind (to make building relation descriptors fast) and
     * possibly others, as they're added.
     */

    if (!IsBootstrapProcessingMode() && !IsDBWriter()) {
        init_irels();
    } else {

    }

    MemoryContextSwitchTo(oldcxt);
}

static void
AttrDefaultFetch(Relation relation) {
    AttrDefault *attrdef = relation->rd_att->constr->defval;
    int ndef = relation->rd_att->constr->num_defval;
    Relation adrel;
    Relation irel = (Relation) NULL;
    ScanKeyData skey;
    HeapTupleData tuple;
    HeapTuple htup;
    Form_pg_attrdef adform;
    IndexScanDesc sd = (IndexScanDesc) NULL;
    HeapScanDesc adscan = (HeapScanDesc) NULL;
    bool indexRes;
    struct varlena *val;
    bool isnull;
    int found;
    int i;
    bool hasindex;

    ScanKeyEntryInitialize(&skey,
            (bits16) 0x0,
            (AttrNumber) 1,
            (RegProcedure) F_OIDEQ,
            ObjectIdGetDatum(RelationGetRelid(relation)));

    adrel = heap_openr(AttrDefaultRelationName, AccessShareLock);
    hasindex = (adrel->rd_rel->relhasindex && !IsIgnoringSystemIndexes());
    if (hasindex) {
        irel = index_openr(AttrDefaultIndex);
        sd = index_beginscan(irel, false, 1, &skey);
    } else
        adscan = heap_beginscan(adrel, SnapshotNow, 1, &skey);
    tuple.t_datamcxt = NULL;
    tuple.t_datasrc = NULL;
    tuple.t_info = 0;
    tuple.t_data = NULL;

    for (found = 0;;) {
        Buffer buffer;

        if (hasindex) {
            indexRes = index_getnext(sd, ForwardScanDirection);
            if (!indexRes)
                break;

            tuple.t_self = sd->xs_ctup.t_self;
            heap_fetch(adrel, SnapshotNow, &tuple, &buffer);
            if (tuple.t_data == NULL)
                continue;
            htup = &tuple;
        } else {
            htup = heap_getnext(adscan);
            if (!HeapTupleIsValid(htup))
                break;
        }
        found++;
        adform = (Form_pg_attrdef) GETSTRUCT(htup);
        for (i = 0; i < ndef; i++) {
            if (adform->adnum != attrdef[i].adnum)
                continue;
            if (attrdef[i].adbin != NULL)
                elog(NOTICE, "AttrDefaultFetch: second record found for attr %s in rel %s",
                    NameStr(relation->rd_att->attrs[adform->adnum - 1]->attname),
                    RelationGetRelationName(relation));

            val = (struct varlena *) fastgetattr(htup,
                    Anum_pg_attrdef_adbin,
                    adrel->rd_att, &isnull);
            if (isnull)
                elog(NOTICE, "AttrDefaultFetch: adbin IS NULL for attr %s in rel %s",
                    NameStr(relation->rd_att->attrs[adform->adnum - 1]->attname),
                    RelationGetRelationName(relation));
            attrdef[i].adbin = textout(val);
            break;
        }
        if (hasindex)
            ReleaseBuffer(relation, buffer);

        if (i >= ndef)
            elog(NOTICE, "AttrDefaultFetch: unexpected record found for attr %d in rel %s",
                adform->adnum,
                RelationGetRelationName(relation));
    }

    if (found < ndef)
        elog(NOTICE, "AttrDefaultFetch: %d record not found for rel %s",
            ndef - found, RelationGetRelationName(relation));

    if (hasindex) {
        index_endscan(sd);
        index_close(irel);
    } else
        heap_endscan(adscan);
    heap_close(adrel, AccessShareLock);
}

static void
RelCheckFetch(Relation relation) {
    ConstrCheck *check = relation->rd_att->constr->check;
    int ncheck = relation->rd_att->constr->num_check;
    Relation rcrel;
    Relation irel = (Relation) NULL;
    ScanKeyData skey;
    HeapTupleData tuple;
    HeapTuple htup;
    IndexScanDesc sd = (IndexScanDesc) NULL;
    HeapScanDesc rcscan = (HeapScanDesc) NULL;
    bool indexRes;
    Name rcname;
    struct varlena *val;
    bool isnull;
    int found;
    bool hasindex;

    ScanKeyEntryInitialize(&skey,
            (bits16) 0x0,
            (AttrNumber) 1,
            (RegProcedure) F_OIDEQ,
            ObjectIdGetDatum(RelationGetRelid(relation)));

    rcrel = heap_openr(RelCheckRelationName, AccessShareLock);
    hasindex = (rcrel->rd_rel->relhasindex && !IsIgnoringSystemIndexes());
    if (hasindex) {
        irel = index_openr(RelCheckIndex);
        sd = index_beginscan(irel, false, 1, &skey);
    } else
        rcscan = heap_beginscan(rcrel, SnapshotNow, 1, &skey);
    tuple.t_datamcxt = NULL;
    tuple.t_datasrc = NULL;
    tuple.t_info = 0;
    tuple.t_data = NULL;

    for (found = 0;;) {
        Buffer buffer;

        if (hasindex) {
            indexRes = index_getnext(sd, ForwardScanDirection);
            if (!indexRes)
                break;

            tuple.t_self = sd->xs_ctup.t_self;
            heap_fetch(rcrel, SnapshotNow, &tuple, &buffer);
            if (tuple.t_data == NULL)
                continue;
            htup = &tuple;
        } else {
            htup = heap_getnext(rcscan);
            if (!HeapTupleIsValid(htup))
                break;
        }
        if (found == ncheck)
            elog(ERROR, "RelCheckFetch: unexpected record found for rel %s",
                RelationGetRelationName(relation));

        rcname = (Name) fastgetattr(htup,
                Anum_pg_relcheck_rcname,
                rcrel->rd_att, &isnull);
        if (isnull)
            elog(ERROR, "RelCheckFetch: rcname IS NULL for rel %s",
                RelationGetRelationName(relation));
        check[found].ccname = pstrdup(NameStr(*rcname));
        val = (struct varlena *) fastgetattr(htup,
                Anum_pg_relcheck_rcbin,
                rcrel->rd_att, &isnull);
        if (isnull)
            elog(ERROR, "RelCheckFetch: rcbin IS NULL for rel %s",
                RelationGetRelationName(relation));
        check[found].ccbin = textout(val);
        found++;
        if (hasindex)
            ReleaseBuffer(relation, buffer);
    }

    if (found < ncheck)
        elog(ERROR, "RelCheckFetch: %d record not found for rel %s",
            ncheck - found, RelationGetRelationName(relation));

    if (hasindex) {
        index_endscan(sd);
        index_close(irel);
    } else
        heap_endscan(rcscan);
    heap_close(rcrel, AccessShareLock);
}

/*
 *	init_irels(), write_irels() -- handle special-case initialization of
 *								   index relation descriptors.
 *
 *		In late 1992, we started regularly having databases with more than
 *		a thousand classes in them.  With this number of classes, it became
 *		critical to do indexed lookups on the system catalogs.
 *
 *		Bootstrapping these lookups is very hard.  We want to be able to
 *		use an index on pg_attribute, for example, but in order to do so,
 *		we must have read pg_attribute for the attributes in the index,
 *		which implies that we need to use the index.
 *
 *		In order to get around the problem, we do the following:
 *
 *		   +  When the database system is initialized (at initdb time), we
 *			  don't use indices on pg_attribute.  We do sequential scans.
 *
 *		   +  When the backend is started up in normal mode, we load an image
 *			  of the appropriate relation descriptors, in internal format,
 *			  from an initialization file in the data/base/... directory.
 *
 *		   +  If the initialization file isn't there, then we create the
 *			  relation descriptors using sequential scans and write 'em to
 *			  the initialization file for use by subsequent backends.
 *
 *		We could dispense with the initialization file and just build the
 *		critical reldescs the hard way on every backend startup, but that
 *		slows down backend startup noticeably if pg_class is large.
 *
 *		As of v6.5, vacuum.c deletes the initialization file at completion
 *		of a VACUUM, so that it will be rebuilt at the next backend startup.
 *		This ensures that vacuum-collected stats for the system indexes
 *		will eventually get used by the optimizer --- otherwise the relcache
 *		entries for these indexes will show zero sizes forever, since the
 *		relcache entries are pinned in memory and will never be reloaded
 *		from pg_class.
 */


void
InitIndexRelations(void) {
    init_irels();
}

static
long m_init_irels(void) {
    Relation ird;
    int relno;
    int i = 0;
    RelationBuildDescInfo bi;
    char* database = GetDatabaseName();
    MemoryContext context;
    RelationCacheGlobal* rglobal = GetRelationCacheGlobal();
    /*  look for irelcache in our list already created */
    for (i = 0; i < listsize; i++) {
        if (strcmp(masterlist[i].database, database) == 0) {
            context = MemoryContextSwitchTo(rglobal->rcache_cxt);
            for (relno = 0; relno < Num_indices_bootstrap; relno++) {
                ird = RelationCopyRelation(masterlist[i].icache[relno]);
                RelationInitLockInfo(ird);
                RelationCacheInsert(ird);
            }
            rglobal->criticalRelcacheBuild = true;
            MemoryContextSwitchTo(context);
            return 0;
        }
    }

    /*  create this in regular global memory b/c it is nailed
            and used by all backend threads  MKS   1.24.2001  */

    if (GlobalCacheMemory == NULL) {
        GlobalCacheMemory = AllocSetContextCreate((MemoryContext) NULL,
                "GlobalCacheMemoryContext",
                8 * 1024,
                8 * 1024,
                8 * 1024);
    }
    /*  if not found, build it here  */
    context = MemoryContextSwitchTo(GlobalCacheMemory);


    if (masterlist == NULL)
        masterlist = palloc(sizeof (CacheList) * (listsize + 1));
    else
        masterlist = repalloc(masterlist, sizeof (CacheList) * (listsize + 1));

    strncpy(masterlist[listsize].database, database, 255);

    bi.infotype = INFO_RELNAME;
    namestrcpy(&bi.i.info_name, AttributeRelidNumIndex);
    masterlist[listsize].icache[0] = RelationBuildDesc(bi, NULL);
    masterlist[listsize].icache[0]->rd_isnailed = true;

    namestrcpy(&bi.i.info_name, ClassNameIndex);
    masterlist[listsize].icache[1] = RelationBuildDesc(bi, NULL);
    masterlist[listsize].icache[1]->rd_isnailed = true;

    namestrcpy(&bi.i.info_name, ClassOidIndex);
    masterlist[listsize].icache[2] = RelationBuildDesc(bi, NULL);
    masterlist[listsize].icache[2]->rd_isnailed = true;

    namestrcpy(&bi.i.info_name, IndexRelidIndex);
    masterlist[listsize].icache[3] = RelationBuildDesc(bi, NULL);
    masterlist[listsize].icache[3]->rd_isnailed = true;

    namestrcpy(&bi.i.info_name, OpclassNameIndex);
    masterlist[listsize].icache[4] = RelationBuildDesc(bi, NULL);
    masterlist[listsize].icache[4]->rd_isnailed = true;

    namestrcpy(&bi.i.info_name, OperatorOidIndex);
    masterlist[listsize].icache[5] = RelationBuildDesc(bi, NULL);
    masterlist[listsize].icache[5]->rd_isnailed = true;

    namestrcpy(&bi.i.info_name, RewriteRulenameIndex);
    masterlist[listsize].icache[6] = RelationBuildDesc(bi, NULL);
    masterlist[listsize].icache[6]->rd_isnailed = true;

    namestrcpy(&bi.i.info_name, TriggerRelidIndex);
    masterlist[listsize].icache[7] = RelationBuildDesc(bi, NULL);
    masterlist[listsize].icache[7]->rd_isnailed = true;

    namestrcpy(&bi.i.info_name, AccessMethodStrategyIndex);
    masterlist[listsize].icache[8] = RelationBuildDesc(bi, NULL);
    masterlist[listsize].icache[8]->rd_isnailed = true;

    namestrcpy(&bi.i.info_name, AccessMethodOpidIndex);
    masterlist[listsize].icache[9] = RelationBuildDesc(bi, NULL);
    masterlist[listsize].icache[9]->rd_isnailed = true;

    /*  switch to original context */
    MemoryContextSwitchTo(context);
    /* now put them in my own context  */
    context = MemoryContextSwitchTo(rglobal->rcache_cxt);
    for (relno = 0; relno < Num_indices_bootstrap; relno++) {
        ird = RelationCopyRelation(masterlist[listsize].icache[relno]);
        RelationInitLockInfo(ird);
        RelationCacheInsert(ird);
    }
    MemoryContextSwitchTo(context);

    listsize++;
    rglobal->criticalRelcacheBuild = true;
    return 0;
}

static void
init_irels(void) {
    if (igate == NULL) {
        igate = os_malloc(sizeof (pthread_mutex_t));
        pthread_mutex_init(igate, NULL);
    }

    pthread_mutex_lock(igate);
    m_init_irels();
    pthread_mutex_unlock(igate);
    return;
}

void
RelationSetTrigger(Relation rel, BufferTrigger* read) {
    rel->readtrigger = read;
}

void
RelationClearTrigger(Relation rel) {
    rel->readtrigger = NULL;
}

void ReportTransactionStatus(int level, char* id) {
    TransactionId xid = (TransactionId) atoll(id);
    if (TransactionIdDidCommit(xid))
        elog(level, "reporting transaction %lld did commit\n", (uint64)xid);
    else if (TransactionIdDidAbort(xid))
        elog(level, "reporting transaction %lld did abort\n", (uint64)xid);
    else
        elog(level, "reporting transaction %lld unknown\n", (uint64)xid);
}

MemoryContext RelationGetCacheContext() {
    RelationCacheGlobal* rglobal = GetRelationCacheGlobal();
    return rglobal->rcache_cxt;
}

RelationCacheGlobal*
GetRelationCacheGlobal(void) {
    RelationCacheGlobal* rg = relationcache_global;
    if (rg == NULL) {
        rg = InitializeRelationCacheGlobal();
    }
    return rg;
}

RelationCacheGlobal*
InitializeRelationCacheGlobal(void) {
    RelationCacheGlobal* rglobal = AllocateEnvSpace(rel_cache_id, sizeof (RelationCacheGlobal));
    memset(rglobal, 0x00, sizeof (RelationCacheGlobal));
    rglobal->criticalRelcacheBuild = false;
    rglobal->rcache_cxt = AllocSetContextCreate(MemoryContextGetEnv()->CacheMemoryContext,
            "RelationMemoryContext",
            ALLOCSET_DEFAULT_MINSIZE,
            ALLOCSET_DEFAULT_INITSIZE,
            ALLOCSET_DEFAULT_MAXSIZE);

    relationcache_global = rglobal;

    return rglobal;
}

void PrintRelcacheMemory() {
    pthread_mutex_lock(igate);

    size_t total = MemoryContextStats(GlobalCacheMemory);
    user_log("Total global cache memory: %d", total);

    pthread_mutex_unlock(igate);

}
