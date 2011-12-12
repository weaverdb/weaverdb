/*-------------------------------------------------------------------------
 *
 * vacuumlazy.c
 *	  Concurrent ("lazy") vacuuming.
 *
 *
 * The major space usage for LAZY VACUUM is storage for the array of dead
 * tuple TIDs, with the next biggest need being storage for per-disk-page
 * free space info.  We want to ensure we can vacuum even the very largest
 * relations with finite memory space usage.  To do that, we set upper bounds
 * on the number of tuples and pages we will keep track of at once.
 *
 * We are willing to use at most VacuumMem memory space to keep track of
 * dead tuples.  We initially allocate an array of TIDs of that size.
 * If the array threatens to overflow, we suspend the heap scan phase
 * and perform a pass of index cleanup and page compaction, then resume
 * the heap scan with an empty TID array.
 *
 * We can limit the storage for page free space to MaxFSMPages entries,
 * since that's the most the free space map will be willing to remember
 * anyway.	If the relation has fewer than that many pages with free space,
 * life is easy: just build an array of per-page info.	If it has more,
 * we store the free space info as a heap ordered by amount of free space,
 * so that we can discard the pages with least free space to ensure we never
 * have more than MaxFSMPages entries in all.  The surviving page entries
 * are passed to the free space map at conclusion of the scan.
 *
 *
 * Portions Copyright (c) 1996-2001, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  $Header: /cvs/weaver/mtpgsql/src/backend/env/vacuumlazy.c,v 1.7 2007/05/23 15:39:24 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */

#include <sys/time.h>
#include <sys/resource.h>
#include <sys/types.h>

#include "postgres.h"
#include "env/env.h"

#include "env/freespace.h"
#include "env/poolsweep.h"
#include "env/dbwriter.h"
#include "env/connectionutil.h"

#include "access/genam.h"
#include "access/heapam.h"
#include "access/hio.h"
#include "access/xlog.h"
#include "commands/vacuum.h"
#include "miscadmin.h"
#include "storage/sinval.h"
#include "storage/lock.h"
#include "storage/smgr.h"
#include "catalog/catname.h"
#include "catalog/index.h"
#include "catalog/indexing.h"
#include "catalog/pg_extstore.h"
#include "utils/syscache.h"
#include "utils/inval.h"
#include "access/blobstorage.h"


/*
 * Space/time tradeoff parameters: do these need to be user-tunable?
 * 
 * A page with less than PAGE_SPACE_THRESHOLD free space will be forgotten
 * immediately, and not even passed to the free space map.	Removing the
 * uselessly small entries early saves cycles, and in particular reduces the
 * amount of time we spend holding the FSM lock when we finally call
 * MultiRecordFreeSpace.  Since the FSM will ignore pages below its own
 * runtime threshold anyway, there's no point in making this really small.
 * XXX Is it worth trying to measure average tuple size, and using that to
 * set the threshold?  Problem is we don't know average tuple size very
 * accurately for the first few pages...
 * 
 * To consider truncating the relation, we want there to be at least relsize /
 * REL_TRUNCATE_FRACTION potentially-freeable pages.
 */
#define PAGE_SPACE_THRESHOLD	((Size) (BLCKSZ / 32))

#define REL_TRUNCATE_FRACTION	10

/* MAX_TUPLES_PER_PAGE can be a conservative upper limit */
#define MAX_TUPLES_PER_PAGE		((int) (BLCKSZ / sizeof(HeapTupleHeaderData)))


typedef struct VacRUsage {
	struct timeval  tv;
	struct rusage   ru;
} VacRUsage;

typedef struct FragRepairInfo {
        FragMode        mode;
        Relation       *IRel;
	IndDesc        *Idesc;
	TupleDesc       tupdesc;
	Datum          *idatum;
	char           *inulls;
	int             ind_count;

        TransactionId   xmax_recent;
        BlockNumber     last_moved;
        long            num_moved;
        long            max_moved;
        long            extent;
        ItemPointer     blob_heads;
        int             num_blobs;
        int             max_blobs;
        ItemPointer     blob_tuples;
        int             num_blob_tuples;
        int             max_blob_tuples;
        bool            force;
	bool		blobs_seen;
} FragRepairInfo;

typedef struct LVRelStats {
	/* Overall statistics about rel */
	TransactionId   reapid;
	BlockNumber     rel_pages;
	TupleCount      rel_tuples;
	TupleCount      rel_live_tuples;
	TupleCount      rel_dead_tuples;
	TupleCount      rel_aborted_tuples;
	TupleCount      rel_vacuumed_tuples;
	TupleCount      rel_kept_tuples;
	TupleCount      rel_unused;
	TupleCount      rel_live_segment_tuples;
	TupleCount      rel_dead_segment_tuples;
	BlockNumber     nonempty_pages;	/* actually, last nonempty page + 1 */
	/* List of TIDs of tuples we intend to delete */
	/* NB: this list is ordered by TID address */
	TupleCount      num_dead_tuples;	/* current # of entries */
	TupleCount      num_aborted_tuples;	/* current # of entries */
	TupleCount      num_recently_dead_tuples;	/* current # of entries */
	TupleCount      max_dead_tuples;	/* # slots allocated in array */
	ItemPointer     dead_tuples;	/* array of ItemPointerData */
	ItemPointer     recently_dead_tuples;	/* array of ItemPointerData */
/*      ItemPointer     forward_pointers;  */	/* array of ItemPointerData */
	/* Array or heap of per-page info about free space */
	/* We use a simple array until it fills up, then convert to heap */
	bool            fs_is_heap;	/* are we using heap organization? */
	long            num_free_pages;	/* current # of entries */
	long            max_free_pages;	/* # slots allocated in arrays */
	/* we are going to use these for space usage decisions  */
	Size            max_size;
	Size            min_size;
	Size            ave_size;
	BlockNumber    *free_pages;	/* array or heap of block numbers */
	Size           *free_spaceavail;	/* array or heap of available
						 * space */
	int            *free_pointers;	/* a count of unused pointers per
					 * page  */
        long            empty_pages;
        long            changed_pages;
	long            total_free;
        long            total_bytes;
        long            total_seg_bytes;
        bool            reindexing;
	bool            scanonly;
	bool            force_trim;
	bool		freespace_scan;
	Snapshot        index_confirm;
}               LVRelStats;


/* non-export function prototypes */
static TupleCount 
lazy_scan_heap(Relation onerel, LVRelStats * vacrelstats,
	       Relation * Irel, int nindexes);
static void     lazy_vacuum_heap(Relation onerel, LVRelStats * vacrelstats);
static TupleCount lazy_scan_index(Relation indrel);
static TupleCount lazy_vacuum_index(Relation indrel, LVRelStats * vacrelstats);
static int 
lazy_vacuum_page(Relation onerel, BlockNumber blkno, Buffer buffer,
		 int tupindex, LVRelStats * vacrelstats);
static void     lazy_truncate_heap(Relation onerel, LVRelStats * vacrelstats);
static BlockNumber 
count_nondeletable_pages(Relation onerel,
			 LVRelStats * vacrelstats);
static void     lazy_space_alloc(LVRelStats * vacrelstats, BlockNumber relblocks);
static void 
lazy_record_dead_tuple(LVRelStats * vacrelstats,
		       ItemPointer itemptr);
static void 
lazy_record_free_space(LVRelStats * vacrelstats,
		       BlockNumber page, Size avail, int unused_pointers);
static void     lazy_record_sizes(LVRelStats * vacrelstats, Size min, Size max, Size ave);
static int      vac_cmp_itemptr(const void *left, const void *right);
static void
                vac_mkindesc(Relation onerel, int nindices, Relation * Irel, IndDesc ** Idesc);

static void
                lazy_vacuum_rel(Relation onerel, bool scanonly, bool force_trim);
static void
                lazy_index_freespace(Relation onerel,bool alter);

static void
                vac_close_indexes(int nindexes, Relation * Irel);
static void
                vac_open_indexes(Relation relation, int *nindexes, Relation ** Irel);

static List    *
                insert_ordered_oid(List * list, Oid datum);
static List    *
                RelationGetIndexList(Relation relation);
static void
vac_update_relstats(Oid relid, BlockNumber num_pages, TupleCount num_tuples,
		    bool hasindex);

static bool
repair_page_fragmentation(Relation onerel, Buffer buffer, FragRepairInfo* repair_info);
static bool
repair_insert_index_for_entry(Relation onerel,HeapTuple newtup, FragRepairInfo* repair_info);

static BlockNumber
lazy_repair_fragmentation(Relation onerel, FragRepairInfo* repair_info);
static BlockNumber
lazy_respan_blobs(Relation onerel, bool exclude_self, FragRepairInfo* repair_info);
static int
lazy_record_recently_dead(LVRelStats * vacrelstats, ItemPointer item);
static void
lazy_update_index_stats(Relation irel, TupleCount i_tups);

static const char * vac_show_rusage(VacRUsage * ru0, char *buf);
static void vac_init_rusage(VacRUsage * ru0);

static VRelList vc_getrels(void);

static void     vacuum_log(Relation rel,char* pattern,...);


void
lazy_open_vacuum_rel(Oid relid, bool force_trim, bool scanonly)
{
	Relation        rel = RelationIdGetRelation(relid, DEFAULTDBOID);
	if (RelationIsValid(rel)) {
/*  only relations are relevant to vacuum, anything else, just let freespace know not to 
    record any information or trigger any more scans 
*/
            if (rel->rd_rel->relkind == RELKIND_RELATION) {
                LockRelation(rel, ShareUpdateExclusiveLock);
                lazy_vacuum_rel(rel, scanonly, force_trim);
            } else if (rel->rd_rel->relkind == RELKIND_INDEX) {
                LockRelation(rel, ShareUpdateExclusiveLock);
                lazy_index_freespace(rel,true);
            } else {

                RegisterFreespace(rel, 0, NULL, NULL,
                      NULL, 0, 0, 0, 0, 0, true);
            }
            RelationClose(rel);
	}
}


void
lazy_respan_blobs_rel(Oid relid, bool force, bool exclude_self)
{
        int             maxtuples = 128;
        Relation        rel = RelationIdGetRelation(relid, DEFAULTDBOID);
        char*           maxtupleprop = GetProperty("frag_maxmove");

        if ( maxtupleprop != NULL ) {
            maxtuples = atoi(maxtupleprop);
        }

	if ( maxtuples <= 0 ) {
		maxtuples = 128;
	}
	if ( maxtuples > 1024 ) {
		maxtuples = 1024;
	}

        maxtuples *= 1024;
        
        if (RelationIsValid(rel)) {
/*  only relations are relevant to vacuum, anything else, just let freespace know not to
    record any information or trigger any more scans */
            if (rel->rd_rel->relkind == RELKIND_RELATION) {
                BlockNumber  blk = 0;
                FragRepairInfo       repair_info;

                repair_info.xmax_recent = GetCheckpointId();
                repair_info.force = force;
                repair_info.extent = GetNextExtentFactor(rel);
                repair_info.last_moved = 0;
                repair_info.num_moved = 0;
                repair_info.max_moved = 0;
                repair_info.mode = NORMAL;
                repair_info.blobs_seen = false;
                repair_info.max_blobs = 1024 * 1024;
                repair_info.num_blobs = 0;
                repair_info.blob_heads = palloc(sizeof(ItemPointerData) * repair_info.max_blobs);
                repair_info.max_blob_tuples = 1024 * 1024;
                repair_info.num_blob_tuples = 0;
                repair_info.blob_tuples = palloc(sizeof(ItemPointerData) * repair_info.max_blob_tuples);
                
                vac_open_indexes(rel, &repair_info.ind_count, &repair_info.IRel);

                if (repair_info.IRel != NULL) {
                    vac_mkindesc(rel, repair_info.ind_count, repair_info.IRel, &repair_info.Idesc);
                    repair_info.tupdesc = RelationGetDescr(rel);
                    repair_info.idatum = (Datum *) palloc(INDEX_MAX_KEYS * sizeof(*repair_info.idatum));
                    repair_info.inulls = (char *) palloc(INDEX_MAX_KEYS * sizeof(*repair_info.idatum));
                }
        
                LockRelation(rel, ShareUpdateExclusiveLock);

                blk = lazy_respan_blobs(rel, exclude_self, &repair_info);
                
                if (repair_info.IRel != NULL) {
                    pfree(repair_info.Idesc);
                    pfree(repair_info.idatum);
                    pfree(repair_info.inulls);
                    vac_close_indexes(repair_info.ind_count, repair_info.IRel);
                }                
            } else {
                    RegisterFreespace(rel, 0, NULL, NULL,
                                      NULL, 0, 0, 0, 0, 0, true);
            }
            RelationClose(rel);
        }
}

void
lazy_fragmentation_scan_rel(Oid relid,bool force, FragMode blobs, int max)
{
        Relation        rel = RelationIdGetRelation(relid, DEFAULTDBOID);
        char*           defmax = GetProperty("frag_maxmove");
        long            max_count = 128;
        
        if ( max == 0 ) {
            if ( defmax != NULL ) {
                max_count = atoi(defmax);
            }
        } else {
            max_count = max;
        }
	
	if ( max_count > 1024 ) {
		max_count = 1024;
	}
	if ( max_count < 0 ) {
		max_count = 1;
	}
        
        max_count *= 1024;
        
        if (RelationIsValid(rel)) {
/*  only relations are relevant to vacuum, anything else, just let freespace know not to
    record any information or trigger any more scans */
            if (rel->rd_rel->relkind == RELKIND_RELATION) {
                BlockNumber  blk = 0;
                FragRepairInfo       repair_info;

                repair_info.xmax_recent = GetCheckpointId();
                repair_info.force = force;
                repair_info.extent = GetNextExtentFactor(rel);
                repair_info.last_moved = 0;
                repair_info.num_moved = 0;
                repair_info.max_moved = max_count;
                repair_info.mode = blobs;
                repair_info.blobs_seen = false;
                repair_info.max_blobs = max_count;
                repair_info.num_blobs = 0;
                repair_info.blob_heads = palloc(sizeof(ItemPointerData) * repair_info.max_blobs);
                repair_info.max_blob_tuples = max_count;
                repair_info.num_blob_tuples = 0;
                repair_info.blob_tuples = palloc(sizeof(ItemPointerData) * repair_info.max_blob_tuples);
                
                vac_open_indexes(rel, &repair_info.ind_count, &repair_info.IRel);

                if (repair_info.IRel != NULL) {
                    vac_mkindesc(rel, repair_info.ind_count, repair_info.IRel, &repair_info.Idesc);
                    repair_info.tupdesc = RelationGetDescr(rel);
                    repair_info.idatum = (Datum *) palloc(INDEX_MAX_KEYS * sizeof(*repair_info.idatum));
                    repair_info.inulls = (char *) palloc(INDEX_MAX_KEYS * sizeof(*repair_info.idatum));
                }
        
                LockRelation(rel, ShareUpdateExclusiveLock);

                blk = lazy_repair_fragmentation(rel, &repair_info);

                if (repair_info.IRel != NULL) {
                    pfree(repair_info.Idesc);
                    pfree(repair_info.idatum);
                    pfree(repair_info.inulls);
                    vac_close_indexes(repair_info.ind_count, repair_info.IRel);
                }                
            } else {
                    RegisterFreespace(rel, 0, NULL, NULL,
                                      NULL, 0, 0, 0, 0, 0, true);
            }
            RelationClose(rel);
        }
}

void
lazy_freespace_scan_rel(Oid relid)
{
        Relation        rel = RelationIdGetRelation(relid, DEFAULTDBOID);
        
        if (RelationIsValid(rel)) {
/*  only relations are relevant to vacuum, anything else, just let freespace know not to
    record any information or trigger any more scans
*/
        if (rel->rd_rel->relkind == RELKIND_RELATION) {
                LVRelStats     *vacrelstats;
                TupleCount      vac_count;
       /*  only looking for freespace so don't worry about blocking 
     *  most op's  */
                LockRelation(rel, AccessShareLock);

                vacrelstats = (LVRelStats *) palloc(sizeof(LVRelStats));
                MemSet(vacrelstats, 0, sizeof(LVRelStats));
                
                vacrelstats->reapid = GetCheckpointId();
                vacrelstats->scanonly = true;
                vacrelstats->force_trim = false;
                vacrelstats->freespace_scan = true;

                vacuum_log(rel,"Checkpoint Id: %d",vacrelstats->reapid);

                /* Do the scan, don't care about indexes */
                vac_count = lazy_scan_heap(rel, vacrelstats, NULL, 0);
                
                RegisterFreespace(rel, vacrelstats->num_free_pages,
                      vacrelstats->free_pages, vacrelstats->free_spaceavail, vacrelstats->free_pointers,
                      vacrelstats->min_size, vacrelstats->max_size, vacrelstats->ave_size,
                      vacrelstats->rel_live_tuples,
                      vacrelstats->rel_dead_tuples + vacrelstats->rel_kept_tuples, false);
                      
                pfree(vacrelstats);
        } else if (rel->rd_rel->relkind == RELKIND_INDEX) {
                lazy_index_freespace(rel, false);
        } else {
                RegisterFreespace(rel, 0, NULL, NULL,
                                  NULL, 0, 0, 0, 0, 0, true);
        }
        RelationClose(rel);
    }
}


static void
lazy_index_freespace(Relation onerel, bool alter)
{
        BlockNumber*    free_pages;
        BlockNumber     cpage;
        BlockNumber     size;
        int             num_free = 0;
        int             max_free = 4096;
        ProcessingMode  mode = GetProcessingMode();
        
        Assert(onerel->rd_rel->relkind == RELKIND_INDEX);

	free_pages = palloc(sizeof(BlockNumber) * max_free);
	MemSet(free_pages, 0, sizeof(BlockNumber) * max_free);
        
        if ( !alter ) SetProcessingMode(ReadOnlyProcessing);
        size = RelationGetNumberOfBlocks(onerel);
        for(cpage=1;cpage<size;cpage++) {
            if ( cpage == index_recoverpage(onerel,cpage) ) {
                free_pages[num_free++] = cpage;
                if ( num_free >= max_free) break;
            }
        }
        
        if ( !alter ) SetProcessingMode(mode);
	RegisterFreespace(onerel, num_free,free_pages, 0, 0,
                      0, 0, 0,
                      0,0, true);

/*  don't do this for now, not optimized properly */

	pfree(free_pages);

}

/*
 * lazy_vacuum_rel() -- perform LAZY VACUUM for one heap relation
 * 
 * This routine vacuums a single heap, cleans out its indexes, and updates its
 * num_pages and num_tuples statistics.
 * 
 * At entry, we have already established a transaction and opened and locked the
 * relation.
 */
static void
lazy_vacuum_rel(Relation onerel, bool scanonly, bool force_trim)
{
	LVRelStats     *vacrelstats;
	Relation       *Irel;
	int             nindexes = 0;
	bool            hasindex = false;
	TupleCount      vac_count;
	double          ratio = 0.0;
	double          total_dead = 0.0;
	long            random = 0;

	vacrelstats = (LVRelStats *) palloc(sizeof(LVRelStats));
	MemSet(vacrelstats, 0, sizeof(LVRelStats));
	vacrelstats->reapid = GetCheckpointId();
	vacrelstats->scanonly = scanonly;
	vacrelstats->force_trim = force_trim;
    vacrelstats->freespace_scan = false;
    
    vacuum_log(onerel,"Checkpoint Id: %d",vacrelstats->reapid);
/* Open all indexes of the relation */
    vac_open_indexes(onerel, &nindexes, &Irel);
    hasindex = (nindexes > 0);

/* Do the vacuuming */
    vac_count = lazy_scan_heap(onerel, vacrelstats, Irel, nindexes);

/* Done with indexes */
    vac_close_indexes(nindexes, Irel);
	/*
	 * Optionally truncate the relation.
	 * 
	 * Don't even think about it unless we have a shot at releasing a goodly
	 * number of pages.  Otherwise, the time taken isn't worth it.
	 */
    if ( !scanonly ) {
        if (force_trim) {
            LockRelation(onerel, AccessExclusiveLock);
            lazy_truncate_heap(onerel, vacrelstats);
        } else {
            uint32 trunc_frac = GetNextExtentFactor(onerel);

            uint32 possibly_freeable = vacrelstats->rel_pages - vacrelstats->nonempty_pages;
            vacuum_log(onerel,"Truncation: total pages %d, possibly freeable %d, next extent %d",vacrelstats->rel_pages,possibly_freeable,trunc_frac);

            if (possibly_freeable > 20 && (possibly_freeable) >  trunc_frac) {

                 vacuum_log(onerel,"Attempting Truncation");
                /*
                 * hold the relation lock until the end so we can
                 * update freespace before someone else tries to
                 * write.
                 */
                if (NoWaitLockRelation(onerel, AccessExclusiveLock)) {
                        lazy_truncate_heap(onerel, vacrelstats);
                } else {
                        double          ratio = ((double) possibly_freeable / (double) vacrelstats->rel_pages);
                        vacuum_log(onerel,"failed to lock for space reduction ratio: %.2f",ratio);
                }
            }
        }
    }

	/* Update shared free space map with final free space info */
	/* Update statistics in pg_class */
        vac_update_relstats(RelationGetRelid(onerel), vacrelstats->rel_pages,
        vacrelstats->rel_live_tuples, hasindex);

	RegisterFreespace(onerel, vacrelstats->num_free_pages,
                      vacrelstats->free_pages, vacrelstats->free_spaceavail, vacrelstats->free_pointers,
                      vacrelstats->min_size, vacrelstats->max_size, vacrelstats->ave_size,
                      vacrelstats->rel_live_tuples,
                      vacrelstats->rel_dead_tuples + vacrelstats->rel_kept_tuples, true);

	if (!scanonly) {
            ratio = ((double) vacrelstats->total_free) / ((double) (vacrelstats->rel_pages * MaxTupleSize));
            random = prandom();
            
            vacuum_log(onerel,"defrag chance stats -- ratio:%.2f random:%d threshold:%.0f",ratio, random, (MAX_RANDOM_VALUE * ratio));

            if (random < (MAX_RANDOM_VALUE * 0.20)) {
                    AddAnalyzeRequest(NameStr(onerel->rd_rel->relname), GetDatabaseName(), onerel->rd_id, GetDatabaseId());
            }
        }
/*  don't do this for now, not optimized properly */

	pfree(vacrelstats);

}

static bool
lazy_scan_heap_page(Relation onerel, Buffer buf, LVRelStats * vacrelstats) {
        Page            page = BufferGetPage(buf);
        OffsetNumber    offnum;
        bool            pgchanged, hastup;
        TupleCount      page_unused = 0;
        TupleCount      nunused = 0;
        TupleCount      num_tuples = 0;
        TupleCount      tups_live = 0;
        TupleCount      tups_dead = 0;
        TupleCount      nkeep = 0;
        BlockNumber     blkno = BufferGetBlockNumber(buf);
	Size            max, min;
	Size		freespace;
	int		num_dead = vacrelstats->num_dead_tuples;

        OffsetNumber maxoff = PageGetMaxOffsetNumber(page);
        	
        max = 0;
	min = ~0;
        pgchanged = false;
        hastup = false;

        for (offnum = FirstOffsetNumber;
             offnum <= maxoff;
             offnum = OffsetNumberNext(offnum)) {
                ItemId              itemid;
                uint16              sv_infomask;
                HeapTupleData       tuple;
                bool                tupgone = false;

                itemid = PageGetItemId(page, offnum);

                if (!ItemIdIsUsed(itemid)) {
                        page_unused += 1;
                        nunused += 1;
                        continue;
                }

                tuple.t_datamcxt = NULL;
                tuple.t_datasrc = NULL;
                tuple.t_info = 0;
                tuple.t_data = (HeapTupleHeader) PageGetItem(page, itemid);
                tuple.t_len = ItemIdGetLength(itemid);
                ItemPointerSet(&tuple.t_self,blkno,offnum);

                ItemPointerSet(&(tuple.t_self), blkno, offnum);

                tupgone = false;
                sv_infomask = tuple.t_data->t_infomask;

                switch (HeapTupleSatisfiesVacuum(tuple.t_data, vacrelstats->reapid)) {
                    case HEAPTUPLE_DEAD:
                        tupgone = true;	/* we can delete the tuple */
                        break;
                    case HEAPTUPLE_STILLBORN:
                        vacrelstats->num_aborted_tuples++;
                        tupgone = true;
                        break;
                    case HEAPTUPLE_LIVE:
                        tups_live++;
                        break;
                    case HEAPTUPLE_RECENTLY_DEAD:
                        /*
                         * If tuple is recently deleted then we must
                         * not remove it from relation.
                         */
                        lazy_record_recently_dead(vacrelstats, &(tuple.t_self));
                        nkeep += 1;
                        break;
                    case HEAPTUPLE_INSERT_IN_PROGRESS:
                        /*
                         * This is an expected case during concurrent
                         * vacuum
                         */
                        break;
                    case HEAPTUPLE_DELETE_IN_PROGRESS:
                        /*
                         * This is an expected case during concurrent
                         * vacuum
                         */
                        break;
                    default:
                        LockBuffer((onerel), buf, BUFFER_LOCK_UNLOCK);
                        elog(ERROR, "Unexpected HeapTupleSatisfiesVacuum result");
                        break;
                }

                if (sv_infomask & HEAP_BLOB_SEGMENT) {
                    if (tupgone)
                            vacrelstats->rel_dead_segment_tuples += 1;
                    else
                            vacrelstats->rel_live_segment_tuples += 1;
                    vacrelstats->total_seg_bytes += tuple.t_len;
                } else {
                    Size            t_size = (sv_infomask & HEAP_HASBLOB) ?
                    sizeof_tuple_blob(onerel, &tuple) :
                    tuple.t_len;
                    if (t_size < min)
                            min = t_size;
                    if (t_size > max)
                            max = t_size;
                    vacrelstats->total_bytes += tuple.t_len;
                }

                /*
                 * check for hint-bit update by
                 * HeapTupleSatisfiesVacuum
                 */
                if (sv_infomask != tuple.t_data->t_infomask)
                        pgchanged = true;

                num_tuples += 1;
                if (tupgone) {
                    if (!vacrelstats->scanonly) {
                        lazy_record_dead_tuple(vacrelstats, &(tuple.t_self));
                    }
                    tups_dead += 1;
                } else {
                    hastup = true;
                }
        }		/* scan along page */
        /*
         * Remember the location of the last page with nonremovable
         * tuples
         */
        if (hastup) {
                vacrelstats->nonempty_pages = blkno + 1;
        } else {
          /*  this is a fix for previous mis-deeds when 
           *  PageRepairFragmentation didn't clear space if there 
           *  were not tuples in it, nothing should be accessing,
           *  there are no tuples on it.
           */
            LockBuffer((onerel), buf, BUFFER_LOCK_UNLOCK);
            LockBuffer((onerel), buf, BUFFER_LOCK_REF_EXCLUSIVE);
            freespace = PageGetFreeSpace(page);
            PageCompactPage(page);
            nunused = PageRepairFragmentation(page);
            if ( freespace != PageGetFreeSpace(page) ) pgchanged = true;
        } 

        if (num_dead == vacrelstats->num_dead_tuples) {
            lazy_record_free_space(vacrelstats, blkno, PageGetFreeSpace(page), nunused);                
        }

        if( min < vacrelstats->min_size ) vacrelstats->min_size = min;
        if ( max > vacrelstats->max_size )  vacrelstats->max_size = max;
        /* save stats for use later */
	vacrelstats->rel_tuples += num_tuples;
	vacrelstats->rel_live_tuples += tups_live;
	vacrelstats->rel_dead_tuples += tups_dead;
	vacrelstats->rel_kept_tuples += nkeep;
	vacrelstats->rel_unused += nunused;
        
        return pgchanged;
}

/*
 * lazy_scan_heap() -- scan an open heap relation
 * 
 * This routine sets commit status bits, builds lists of dead tuples and pages
 * with free space, and calculates statistics on the number of live tuples in
 * the heap.  When done, or when we run low on space for dead-tuple TIDs,
 * invoke vacuuming of indexes and heap.
 */
static          TupleCount
lazy_scan_heap(Relation onerel, LVRelStats * vacrelstats,
	       Relation * Irel, int nindexes)
{
	BlockNumber     nblocks, blkno;
	char           *relname;
	TupleCount      tups_vacuumed, tups_aborted, tups_live_segment, tups_dead_segment;
	int             i;

	VacRUsage       ru0;
	char            rubuf[255];

	vac_init_rusage(&ru0);

	relname = RelationGetRelationName(onerel);

	tups_dead_segment = tups_live_segment = tups_aborted = tups_vacuumed = 0;

	nblocks = RelationGetNumberOfBlocks(onerel);
	vacrelstats->nonempty_pages = 0;
	vacrelstats->empty_pages = 0;
	vacrelstats->total_free = 0;
        vacrelstats->max_size = 0;
        vacrelstats->min_size = ~0;
	vacrelstats->num_dead_tuples = 0;

	lazy_space_alloc(vacrelstats, nblocks);

	for (blkno = 0; blkno < nblocks; blkno++) {
		Buffer          buf;
		Page            page;
                bool            changed = false;

                if (IsShutdownProcessingMode()) {
                        elog(ERROR, "shutting down");
                }
                
		if ( !vacrelstats->freespace_scan && !vacrelstats->force_trim && (blkno % 5 == 0) ) {
                        UnlockRelation(onerel,ShareUpdateExclusiveLock);
			if ( !NoWaitLockRelation(onerel,ShareUpdateExclusiveLock) ) {
			 	vacuum_log(onerel,"stopping scan for index build");	
                                break;
			}
		}
                /*
		 * If we are close to overrunning the available space for
		 * dead-tuple TIDs, pause and do a cycle of vacuuming before
		 * we tackle this page.
		 */
		if ((vacrelstats->max_dead_tuples - vacrelstats->num_dead_tuples) < MAX_TUPLES_PER_PAGE &&
		    vacrelstats->num_dead_tuples > 0) {
			/* Remove index entries */
			for (i = 0; i < nindexes; i++) {
                            TupleCount i_tup = lazy_vacuum_index(Irel[i], vacrelstats);
			}
			/*
			 * flush the dirty buffers to make sure that the
			 * index entries are gone before the heap entries
			 * come out.
			 */
			FlushAllDirtyBuffers(true);
			/* Remove tuples from heap */
			lazy_vacuum_heap(onerel, vacrelstats);

			/* Forget the now-vacuumed tuples, and press on */
			tups_vacuumed += vacrelstats->num_dead_tuples;
			tups_aborted += vacrelstats->num_aborted_tuples;
			vacrelstats->num_dead_tuples = 0;
			vacrelstats->num_aborted_tuples = 0;
			tups_live_segment += vacrelstats->rel_live_segment_tuples;
			tups_dead_segment += vacrelstats->rel_dead_segment_tuples;
			vacrelstats->rel_live_segment_tuples = 0;
			vacrelstats->rel_dead_segment_tuples = 0;
		}
                buf = ReadBuffer(onerel, blkno);
		if (!BufferIsValid(buf))
			elog(ERROR, "bad buffer read in garbage collection");

		/* In this phase we only need shared access to the buffer */
		/*
		 * hmm... I think we need exclusive so we make sure that no
		 * additions are made to the page as we mark tuples gone and
		 * get ready to blast them  MKS 1/9/2002
		 */
		LockBuffer((onerel), buf, BUFFER_LOCK_SHARE);

		page = BufferGetPage(buf);

		if (PageIsNew(page)) {
			/* Not sure we still need to handle this case, but... */

			LockBuffer((onerel), buf, BUFFER_LOCK_UNLOCK);
			LockBuffer((onerel), buf, BUFFER_LOCK_EXCLUSIVE);
			vacuum_log(onerel, "Uninitialized page %u - fixing", blkno);
			PageInit(page, BufferGetPageSize(buf), 0);
			lazy_record_free_space(vacrelstats, blkno,
						 PageGetFreeSpace(page), 0);
			LockBuffer((onerel), buf, BUFFER_LOCK_UNLOCK);
			WriteBuffer(onerel,buf);
			continue;
		}
		if (!BufferHasError(buf) && PageIsEmpty(page)) {
			vacrelstats->empty_pages++;
			lazy_record_free_space(vacrelstats, blkno,
					       PageGetFreeSpace(page), 0);
			LockBuffer((onerel), buf, BUFFER_LOCK_UNLOCK);
			ReleaseBuffer(onerel, buf);
			continue;
		}

                if ( !vacrelstats->freespace_scan ) {
                    changed = lazy_scan_heap_page(onerel,buf,vacrelstats);
                } else {
                     TupleCount unused = 0;
                     OffsetNumber offnum,maxoff;
                     Size       len = 0;
                     
                     Assert(vacrelstats->scanonly);
                     maxoff = PageGetMaxOffsetNumber(page);
                     for (offnum = FirstOffsetNumber;
                         offnum <= maxoff;
                         offnum = OffsetNumberNext(offnum)) {
                            ItemId itemid = PageGetItemId(page, offnum);
                            if (!ItemIdIsUsed(itemid)) {
                                unused++;
                            } else {
                                len = ItemIdGetLength(itemid);
                                if ( len > vacrelstats->max_size ) vacrelstats->max_size = len;
                                if ( len < vacrelstats->min_size ) vacrelstats->min_size = len;
                                vacrelstats->rel_tuples++;
                                vacrelstats->total_bytes += len;
                            }
                     }
                     
                    lazy_record_free_space(vacrelstats, blkno, PageGetFreeSpace(page), unused);                
                }
                /*
                 * If we remembered any tuples for deletion, then the page
                 * will be visited again by lazy_vacuum_heap, which will
                 * compute and record its post-compaction free space.  If
                 * not, then we're done with this page, so remember its free
                 * space as-is.
                 */

                
		LockBuffer((onerel), buf, BUFFER_LOCK_UNLOCK);
                if ( changed ) {
                    WriteBuffer(onerel,buf);
                    vacrelstats->changed_pages += 1;
                } else {
                    ReleaseBuffer(onerel, buf);
                }

		if ( vacrelstats->freespace_scan && vacrelstats->num_free_pages >= vacrelstats->rel_pages * 0.10 ) {
                    break;
		}
	}


	/* If any tuples need to be deleted, perform final vacuum cycle */
	/* XXX put a threshold on min number of tuples here? */
	if ( vacrelstats->num_dead_tuples > 0 ) {
            /* Remove index entries */
            for (i = 0; i < nindexes; i++) {
                    TupleCount      i_tup = lazy_vacuum_index(Irel[i], vacrelstats);
                    if (i_tup == vacrelstats->num_dead_tuples) {
                            lazy_update_index_stats(Irel[i], i_tup);
                    }
            }

            /*
             * flush the dirty buffers to make sure that the index
             * entries are gone before the heap entries come out.
             */
            FlushAllDirtyBuffers(true);

            /* Remove tuples from heap */
            lazy_vacuum_heap(onerel, vacrelstats);
            /*( vacuum stats  */
            tups_vacuumed += vacrelstats->num_dead_tuples;
            vacrelstats->num_dead_tuples = 0;
	} else {
            /* Scan indexes just to update pg_class statistics about them */
            if ( !vacrelstats->freespace_scan ) {
                for (i = 0; i < nindexes; i++) {
                        TupleCount      i_tups = lazy_scan_index(Irel[i]);
                        lazy_update_index_stats(Irel[i], i_tups);
                }
            }
	}
        /* common stats */
        tups_aborted += vacrelstats->num_aborted_tuples;
        tups_live_segment += vacrelstats->rel_live_segment_tuples;
        tups_dead_segment += vacrelstats->rel_dead_segment_tuples;
        
                        
        vacrelstats->rel_aborted_tuples = tups_aborted;
	vacrelstats->rel_vacuumed_tuples = tups_vacuumed;
	vacrelstats->rel_live_segment_tuples = tups_live_segment;
	vacrelstats->rel_dead_segment_tuples = tups_dead_segment;

	if (vacrelstats->max_size < vacrelstats->min_size)
		vacrelstats->min_size = vacrelstats->max_size;
	if (onerel->rd_rel->relkind == RELKIND_RELATION && vacrelstats->total_bytes > 0) {
            vacrelstats->ave_size = 
                    ((vacrelstats->total_bytes + vacrelstats->total_seg_bytes) 
                        / 
                    (vacrelstats->rel_tuples - vacrelstats->rel_live_segment_tuples - vacrelstats->rel_dead_segment_tuples));
	}
	vacuum_log(onerel,"Pages %ld: Changed %ld, Empty %ld; Tup %ld: Live %ld, Dead %ld, Abort %ld, Vac %ld, Keep %ld, UnUsed %ld, Segments: Live %ld, Dead %ld.",
	     nblocks, vacrelstats->changed_pages, vacrelstats->empty_pages,
	     vacrelstats->rel_tuples, vacrelstats->rel_live_tuples,
	     vacrelstats->rel_dead_tuples, tups_aborted, tups_vacuumed, vacrelstats->rel_kept_tuples, 
                vacrelstats->rel_unused, tups_live_segment, tups_dead_segment);
	vacuum_log(onerel,"Tuple Sizes Min:%u Max:%u Ave:%u", vacrelstats->min_size, vacrelstats->max_size, vacrelstats->ave_size);
	vacuum_log(onerel,"Total Space Usage %ld free / %ld total",
	     vacrelstats->total_free, nblocks * MaxTupleSize);
	vacuum_log(onerel,"%s", vac_show_rusage(&ru0, rubuf));
	return (tups_vacuumed);
}


/*
 * lazy_vacuum_heap() -- second pass over the heap
 * 
 * This routine marks dead tuples as unused and compacts out free space on their
 * pages.  Pages not having dead tuples recorded from lazy_scan_heap are not
 * visited at all.
 * 
 * Note: the reason for doing this as a second pass is we cannot remove the
 * tuples until we've removed their index entries, and we want to process
 * index entry removal in batches as large as possible.
 */
static void
lazy_vacuum_heap(Relation onerel, LVRelStats * vacrelstats)
{
	long            tupindex;
	long            npages;
	char            rubuf[255];
	VacRUsage       ru0;

	vac_init_rusage(&ru0);
	npages = 0;
	tupindex = 0;

	while (tupindex < vacrelstats->num_dead_tuples) {
		BlockNumber     tblk;
		Buffer          buf;
		Page            page;
		long            unused_p, newmax;
		Size		freespace;


		if ( !vacrelstats->force_trim ) {
                    UnlockRelation(onerel,ShareUpdateExclusiveLock);
                    if ( !NoWaitLockRelation(onerel,ShareUpdateExclusiveLock) ) {
                        vacuum_log(onerel,"aborting vacuum for index build");
                        break;
                    }
                }

		tblk = ItemPointerGetBlockNumber(&vacrelstats->dead_tuples[tupindex]);
		buf = ReadBuffer(onerel, tblk);

		if (!BufferIsValid(buf))
			elog(ERROR, "bad buffer read in garbage collection");

		LockBuffer((onerel), buf, BUFFER_LOCK_REF_EXCLUSIVE);
		tupindex = lazy_vacuum_page(onerel, tblk, buf, tupindex, vacrelstats);
		/*
		 * Now that we've compacted the page, record its available
		 * space
		 */
		page = BufferGetPage(buf);
 /*  ok, vacuum_page marks the line pointer as
  *  unused, PageRepairFragmentation deallocates the 
  *  pointer.  Only if end pointers are deallocated
  *  will PageCompactPage reap pointers at the end.
  *  this should protect against any index pointers 
  *  pointing to a line pointer that disappears 
  *  due to PageCompactPage
  */
/*      cant use this until we have a foolproof way of ensuring
 *      corruption does not occur due to index pointers 
 *      leading to the compacted linepointers 
 * 
 *      update 3/4/08.  should be able to compact pages
 *      now that indexes are scanned at recovery time for 
 *      bad pointers
 */
                newmax = PageCompactPage(page);
                unused_p = PageRepairFragmentation(page);
		
		freespace = PageGetFreeSpace(page);
		
		LockBuffer((onerel), buf, BUFFER_LOCK_UNLOCK);

		if ( WriteBuffer(onerel,buf) ) {
                	lazy_record_free_space(vacrelstats, tblk,
				       freespace, unused_p);
                }

		npages++;
	}
        
	/*
	 * UnlockRelation(onerel,AccessExclusiveLock);
	 */
	vacuum_log(onerel,"Removed %d tuples in %ld pages.",
	     tupindex, npages);
	vacuum_log(onerel,"%s", vac_show_rusage(&ru0, rubuf));
}

/*
 * lazy_vacuum_page() -- free dead tuples on a page and repair its
 * fragmentation.
 * 
 * Caller is expected to handle reading, locking, and writing the buffer.
 * 
 * tupindex is the index in vacrelstats->dead_tuples of the first dead tuple for
 * this page.  We assume the rest follow sequentially. The return value is
 * the first tupindex after the tuples of this page.
 */
static int
lazy_vacuum_page(Relation onerel, BlockNumber blkno, Buffer buffer,
		 int tupindex, LVRelStats * vacrelstats)
{
	OffsetNumber    unbuf[BLCKSZ / sizeof(OffsetNumber)];
	OffsetNumber   *unused = unbuf;
	long            uncnt;
	Page            page = BufferGetPage(buffer);
	ItemId          itemid;

	for (; tupindex < vacrelstats->num_dead_tuples; tupindex++) {
		BlockNumber     tblk;
		OffsetNumber    toff;
		ItemPointer     target = &vacrelstats->dead_tuples[tupindex];

		tblk = ItemPointerGetBlockNumber(target);
		if (tblk != blkno)
			break;	/* past end of tuples for this block */
		toff = ItemPointerGetOffsetNumber(target);
		itemid = PageGetItemId(page, toff);
		itemid->lp_flags &= ~LP_USED;
	}

	return tupindex;
}

/*
 * lazy_scan_index() -- scan one index relation to update pg_class statistic.
 * 
 * We use this when we have no deletions to do.
 */
static          TupleCount
lazy_scan_index(Relation indrel)
{
	TupleCount      nitups, other, recently_dead_tups, live, dead;
	long            nipages = 0;
	long            notemptypages = 0;
	IndexScanDesc   iscan;
	VacRUsage       ru0;

	char            rubuf[255];
	BlockNumber     cpage = 0;

	vac_init_rusage(&ru0);

	iscan = index_beginscan(indrel, false, 0, (ScanKey) NULL);

	dead = live = nitups = other = recently_dead_tups = 0;

	while (index_getnext(iscan, ForwardScanDirection)) {
		nitups++;
		if (cpage != ItemPointerGetBlockNumber(&iscan->currentItemData)) {
			notemptypages++;
			cpage = ItemPointerGetBlockNumber(&iscan->currentItemData);
		}
		if (IsShutdownProcessingMode())
			elog(ERROR, "shutting down");
	}

	index_endscan(iscan);
        
	nipages = RelationGetNumberOfBlocks(indrel);

	if (nipages > 50 && ((double) (nipages - notemptypages) / (double) nipages) > 0.75) {
		vacuum_log(indrel,"Index: adding reindex request index pages: %d used pages: %d number of tuples: %d", nipages, notemptypages, nitups);
		AddReindexRequest(NameStr(indrel->rd_rel->relname), GetDatabaseName(),
				  indrel->rd_id, GetDatabaseId());
	} else {
            lazy_index_freespace(indrel, true);
        }
/*
	vac_update_relstats(RelationGetRelid(indrel), nipages, nitups, false);
*/
	vacuum_log(indrel,"Index: Pages %ld; Empty: %ld; Tuples %ld.", nipages, (nipages - notemptypages), nitups);
	vacuum_log(indrel,"%s", vac_show_rusage(&ru0, rubuf));

	return nitups;

}

static void
lazy_update_index_stats(Relation irel, TupleCount i_tups)
{
	BlockNumber     nipages = RelationGetNumberOfBlocks(irel);
	vac_update_relstats(RelationGetRelid(irel), nipages, i_tups, false);
}

/*
 * lazy_vacuum_index() -- vacuum one index relation.
 * 
 * Delete all the index entries pointing to tuples listed in
 * vacrelstats->dead_tuples.
 * 
 * Finally, we arrange to update the index relation's statistics in pg_class.
 */
static          TupleCount
lazy_vacuum_index(Relation indrel, LVRelStats * vacrelstats)
{
	TupleCount      nitupsremoved = 0;

	VacRUsage       ru0;
	char            rubuf[255];

	vac_init_rusage(&ru0);
        
        LockRelation(indrel,RowExclusiveLock);
        
	nitupsremoved = (TupleCount) index_bulkdelete(indrel, vacrelstats->num_dead_tuples, vacrelstats->dead_tuples);

	/* if deleted tuples do not equal it is time to reindex  */
	if (nitupsremoved != (vacrelstats->num_dead_tuples - vacrelstats->rel_dead_segment_tuples) ) {
		vacuum_log(indrel,"Index: Deleted %ld Heap: Dead %ld Aborted %ld Segments %ld.",
                     nitupsremoved,
		     vacrelstats->num_dead_tuples,
		     vacrelstats->num_aborted_tuples,
		     vacrelstats->rel_dead_segment_tuples);
                /*
		AddReindexRequest(NameStr(indrel->rd_rel->relname), GetDatabaseName(),
				  indrel->rd_id, GetDatabaseId());
                */
	}
	vacuum_log(indrel,"Index: Deleted %ld.", nitupsremoved);

	vacuum_log(indrel,"%s", vac_show_rusage(&ru0, rubuf));
	return nitupsremoved;
}

static bool
repair_insert_index_for_entry(Relation onerel, HeapTuple newtup, FragRepairInfo* repair_info) {
	InsertIndexResult   iresult;
        IndDesc             *idcur;
        
        if (repair_info->IRel != (Relation *) NULL) {
            long i;
            for (i = 0, idcur = repair_info->Idesc; i < repair_info->ind_count; i++, idcur++) {
                    FormIndexDatum(idcur->natts,
                                   (AttrNumber *) & (idcur->tform->indkey[0]),
                                   newtup,
                                   repair_info->tupdesc,
                                   repair_info->idatum,
                                   repair_info->inulls,
                                   idcur->finfoP);
                    iresult = index_insert(repair_info->IRel[i],
                                           repair_info->idatum,
                                           repair_info->inulls,
                                        &newtup->t_self,
                                           onerel,false);
                    if (iresult)
                            pfree(iresult);
            }
    }   
     return TRUE;
}

static bool
repair_page_fragmentation(Relation onerel, Buffer buffer, FragRepairInfo* repair_info) 
{
  /*   EXCLUSIVE PAGE LOCK MUST BE HELD BEFORE CALL  */
        OffsetNumber        offnum, maxoff;
        Page                page = BufferGetPage(buffer);
        BlockNumber         current = BufferGetBlockNumber(buffer);
        bool                page_altered = false;

        if ( PageIsEmpty(page) ) return false;
		        
        maxoff = PageGetMaxOffsetNumber(page);
        
        for (offnum = FirstOffsetNumber;
                offnum <= maxoff;
                offnum = OffsetNumberNext(offnum)) {

                HeapTupleData   tuple;
                HeapTuple       newtup = NULL;
                ItemId		lp;
                bool            has_blob = FALSE;
                bool            handled = FALSE;
                BlockNumber     last_block = InvalidBlockNumber;
                HTSV_Result     state;
                uint16          flags;

                lp = PageGetItemId(page, offnum);
                if ( !ItemIdIsUsed(lp) ) {
                     continue;
                } else {
                     tuple.t_datamcxt = NULL;
                     tuple.t_datasrc = NULL;
                     tuple.t_info = 0;
                     ItemPointerSet(&tuple.t_self,current,offnum);
                     tuple.t_data = (HeapTupleHeader) PageGetItem(page, lp);
                     tuple.t_len = ItemIdGetLength(lp);
                     flags = tuple.t_data->t_infomask;
                }
                
		state = HeapTupleSatisfiesVacuum(tuple.t_data,repair_info->xmax_recent);
                if ( tuple.t_data->t_infomask != flags ) page_altered = TRUE;
                if ( state != HEAPTUPLE_LIVE ) {
                    continue;
                }
                
		if (!repair_info->force && !(tuple.t_data->t_infomask & HEAP_FRAG_SCANNED) ) {
                   tuple.t_data->t_infomask |= HEAP_FRAG_SCANNED;
                   page_altered = true;
                   continue;
                }
                
                if (tuple.t_data->t_infomask & HEAP_BLOB_SEGMENT) {
                    if ( tuple.t_data->t_infomask & HEAP_BLOBHEAD ) {
                        if ( repair_info->num_blobs < repair_info->max_blobs ) {
/*
                            vacuum_log(onerel,"adding %d/%d",
                                ItemPointerGetBlockNumber(&tuple.t_self),
                                ItemPointerGetOffsetNumber(&tuple.t_self));
*/
                            ItemPointerCopy(&tuple.t_self,&repair_info->blob_heads[repair_info->num_blobs]);
                            repair_info->num_blobs+=1;
                        }
                    }
                    continue;
                }
                                 
                if ( repair_info->mode == RELINKING && HeapTupleHasBlob(&tuple) ) {
                    repair_info->blobs_seen = true;
                    if ( repair_info->num_blob_tuples < repair_info->max_blob_tuples ) {
                        ItemPointerCopy(&tuple.t_self,&repair_info->blob_tuples[repair_info->num_blob_tuples]);
                        repair_info->num_blob_tuples+=1;
                    }
                    continue;
                } 
                /* copy tuple */
                newtup = heap_copytuple(&tuple);
                /*
                 * Mark new tuple as moved_in by vacuum and store
                 * xmin in t_cmin and store current XID in xmin
                 */
                if (!(newtup->t_data->t_infomask & HEAP_MOVED_IN)) {
                    newtup->t_data->progress.t_vtran = newtup->t_data->t_xmin;
                }
                newtup->t_data->t_xmin = GetCurrentTransactionId();
                newtup->t_data->t_xmax = InvalidTransactionId;
                newtup->t_data->t_infomask &= ~(HEAP_XACT_MASK);
                newtup->t_data->t_infomask |= (HEAP_MOVED_IN | HEAP_XMAX_INVALID);
                /*
                 * Mark old tuple as moved_off by vacuum and store
                 * vacuum XID in t_cmin !!!
                 */
                tuple.t_data->t_xmax = GetCurrentTransactionId();
                tuple.t_data->t_infomask &=
                        ~(HEAP_XMAX_COMMITTED | HEAP_XMAX_INVALID | HEAP_MARKED_FOR_UPDATE);
                tuple.t_data->t_infomask |= (HEAP_MOVED_OUT);

                page_altered = true;
                /* add tuple to the page */
                RelationInvalidateHeapTuple(onerel, &tuple);                
                    
                LockBuffer(onerel,buffer,BUFFER_LOCK_UNLOCK); 
                                
                last_block = GetFreespace(onerel,newtup->t_len,0);
                while ( !handled && last_block < current ) {                    
                    Buffer try_buffer = ReadBuffer(onerel,last_block);
                    if ( !BufferIsValid(try_buffer) ) {
                        elog(ERROR,"bad buffer read repairing fragmentation");
                    }
                    
                    LockBuffer(onerel,try_buffer,BUFFER_LOCK_EXCLUSIVE);
                    if ( PageGetFreeSpace(BufferGetPage(try_buffer)) >= newtup->t_len ) {
                        RelationPutHeapTuple(onerel,try_buffer,newtup);
                        handled = TRUE;
                    }
                    LockBuffer(onerel,try_buffer,BUFFER_LOCK_UNLOCK);
                    
                    if ( handled ) {
                       WriteBuffer(onerel,try_buffer);
                    } else {
                       ReleaseBuffer(onerel,try_buffer);
                       last_block = GetFreespace(onerel,newtup->t_len,last_block);
                    }
                }
                
                if ( last_block > repair_info->last_moved ) {
                     repair_info->last_moved = last_block;
                }
                
                if ( handled ) {
                    repair_info->num_moved++;
                    /* insert index' tuples if needed */
                    repair_insert_index_for_entry(onerel, newtup, repair_info);

                    LockBuffer(onerel,buffer,BUFFER_LOCK_EXCLUSIVE); 

                    tuple.t_data->t_ctid = newtup->t_self;
                
                } else {
                    LockBuffer(onerel,buffer,BUFFER_LOCK_EXCLUSIVE); 
                    Assert(tuple.t_data->t_xmax == GetCurrentTransactionId());
                    
                    tuple.t_data->t_xmax = InvalidTransactionId;
                    tuple.t_data->t_infomask &= ~(HEAP_MOVED_OUT);
                    tuple.t_data->t_infomask |= (HEAP_XMAX_INVALID);
                     
                    offnum = maxoff;
                }
                heap_freetuple(newtup);   
        }		/* walk along page */ 
        return page_altered;
}

static BlockNumber
lazy_respan_blobs(Relation onerel, bool exclude_self, FragRepairInfo* repair_info)
{
	BlockNumber     nblocks, marker;

	VacRUsage       ru0;
	char            rubuf[255];
        int             added = 0;
        MemoryContext page_cxt;
                
        vacuum_log(onerel,"start respan blobs %s",RelationGetRelationName(onerel));
        
        vac_init_rusage(&ru0);

	nblocks = RelationGetNumberOfBlocks(onerel);
        
        page_cxt = AllocSetContextCreate(MemoryContextGetCurrentContext(),
                                         "RespanPageContext",
                                        ALLOCSET_DEFAULT_MINSIZE,
                                        ALLOCSET_DEFAULT_INITSIZE,
                                        ALLOCSET_DEFAULT_MAXSIZE);   
        
        MemoryContextSwitchTo(page_cxt);
	for (marker = 0;
	     marker < nblocks;
	     marker++) {
                bool page_altered = false;
                Buffer buf = InvalidBuffer;
                Page page = NULL;
                OffsetNumber    maxoff,offnum;
                HTSV_Result state;

                
 		if (!repair_info->force && (marker % 5) == 0) {
		/*
		 * now check to see if anyone is doing updates or changes to
		 * the system.  If so exit out of this loop
		 */
			if (NoWaitLockRelation(onerel, ShareLock)) {
				UnlockRelation(onerel, ShareLock);
			} else {
				vacuum_log(onerel,"exiting defrag of due to concurrent access");
                                break;
			}
		}               
                buf = ReadBuffer(onerel, marker);
                if ( !BufferIsValid(buf) ) {
                    elog(ERROR,"bad read under respanning");
                }
                LockBuffer((onerel), buf, BUFFER_LOCK_EXCLUSIVE);
                page = BufferGetPage(buf);
                maxoff = PageGetMaxOffsetNumber(page);
        
                for (offnum = FirstOffsetNumber;
                        offnum <= maxoff;
                        offnum = OffsetNumberNext(offnum)) {

                        HeapTupleData   tuple;
                        ItemId		lp = PageGetItemId(page, offnum);
                        
                        if ( !ItemIdIsUsed(lp) ) {
                             continue;
                        } else {
                             tuple.t_datamcxt = NULL;
                             tuple.t_datasrc = NULL;
                             tuple.t_info = 0;
                             ItemPointerSet(&tuple.t_self,marker,offnum);
                             tuple.t_data = (HeapTupleHeader) PageGetItem(page, lp);
                             tuple.t_len = ItemIdGetLength(lp);
                             
                             if ( HeapTupleHasBlob(&tuple) && !(tuple.t_data->t_infomask & HEAP_BLOB_SEGMENT) ) {
                                 state = HeapTupleSatisfiesVacuum(tuple.t_data,repair_info->xmax_recent);
                                 if ( state == HEAPTUPLE_LIVE ) {
                                    HeapTuple newtup = NULL;
                                    
                                    tuple.t_data->t_xmax = GetCurrentTransactionId();
                                    tuple.t_data->t_infomask &= ~(HEAP_XMAX_COMMITTED | HEAP_XMAX_INVALID);
                                    tuple.t_data->t_infomask |= HEAP_MARKED_FOR_UPDATE;
                                    LockBuffer((onerel), buf, BUFFER_LOCK_UNLOCK);
                                    newtup = vacuum_respan_tuple_blob(onerel,&tuple,exclude_self);
                                    if ( newtup != NULL ) {
                                        if (!(newtup->t_data->t_infomask & HEAP_MOVED_IN)) {
                                            newtup->t_data->progress.t_vtran = newtup->t_data->t_xmin;
                                        }
                                        newtup->t_data->t_xmin = GetCurrentTransactionId();
                                        newtup->t_data->t_xmax = InvalidTransactionId;
                                        newtup->t_data->t_infomask &= ~(HEAP_XACT_MASK);
                                        newtup->t_data->t_infomask |= (HEAP_MOVED_IN | HEAP_XMAX_INVALID);
                                        RelationPutHeapTupleAtFreespace(onerel,newtup,0);
                                        repair_insert_index_for_entry(onerel, newtup, repair_info);
                                        delete_tuple_blob(onerel,&tuple,newtup);
                                        added++;
                                    }
                                    LockBuffer((onerel), buf, BUFFER_LOCK_EXCLUSIVE);
                                    if ( newtup != NULL ) {
                                        tuple.t_data->t_infomask &= ~(HEAP_MARKED_FOR_UPDATE);
                                        tuple.t_data->t_infomask |= (HEAP_MOVED_OUT);
                                        heap_freetuple(newtup);
                                    }
                                    page_altered = true;
                                 }
                             }
                       }
                }        
                LockBuffer((onerel), buf, BUFFER_LOCK_UNLOCK);
		if ( page_altered ) WriteBuffer(onerel,buf);
                else ReleaseBuffer(onerel, buf);

                MemoryContextResetAndDeleteChildren(page_cxt);
                
		if (IsShutdownProcessingMode()) {
			elog(ERROR, "shutting down");
		}

	}			/* walk along relation */

        MemoryContextSwitchTo(page_cxt->parent);
        MemoryContextDelete(page_cxt);
        
	vacuum_log(onerel, "Rel: Pages: %ld; Tuple(s) respanned: %ld.", marker, added);
	vacuum_log(onerel, "%s", vac_show_rusage(&ru0, rubuf));
	/*
	 * FlushRelationBuffers(onerel,nblocks);
	 */

        return added;
}

static BlockNumber
lazy_repair_fragmentation(Relation onerel, FragRepairInfo* repair_info) {
	Buffer          buf;
	BlockNumber     nblocks, marker;

	VacRUsage       ru0;
	char            rubuf[255];
        
        MemoryContext page_cxt = AllocSetContextCreate(MemoryContextGetCurrentContext(),
                                         "FragPageContext",
                                        ALLOCSET_DEFAULT_MINSIZE,
                                        ALLOCSET_DEFAULT_INITSIZE,
                                        ALLOCSET_DEFAULT_MAXSIZE);
        vacuum_log(onerel,"Checkpoint Id: %d",repair_info->xmax_recent);
        
        vac_init_rusage(&ru0);
	/*
	 * Scan pages backwards from the last nonempty page, trying to move
	 * tuples down to lower pages.	Quit when we reach a page that we
	 * have moved any tuples onto.  Note that if a page is still in the
	 * fraged_pages list (list of candidate move-target pages) when we
	 * reach it, we will remove it from the list.  This ensures we never
	 * move a tuple up to a higher page number.
	 * 
	 * NB: this code depends on the vacuum_pages and fraged_pages lists
	 * being in order, and on fraged_pages being a subset of
	 * vacuum_pages.
	 */
	nblocks = RelationGetNumberOfBlocks(onerel);
        repair_info->last_moved = 0;
        
        MemoryContextSwitchTo(page_cxt);
	for (marker = nblocks;
	     marker > (repair_info->last_moved + repair_info->extent);
	     marker--) {
                bool page_altered = false;
		BlockNumber blkno = marker - 1;
                
                if ( repair_info->num_moved > repair_info->max_moved ) {
                    break;
                }
                
		if (!repair_info->force && (marker % 5) == 0) {
		/*
		 * now check to see if anyone is doing updates or changes to
		 * the system.  If so exit out of this loop
		 */
			if (NoWaitLockRelation(onerel, ShareLock)) {
				UnlockRelation(onerel, ShareLock);
			} else {
				vacuum_log(onerel,"exiting defrag of due to concurrent access");
                                break;
			}
		}
                
		buf = ReadBuffer(onerel, blkno);
                if ( !BufferIsValid(buf) ) {
                    elog(ERROR,"bad buffer read under repair fragmentation");
                }
                LockBuffer((onerel), buf, BUFFER_LOCK_EXCLUSIVE);
                page_altered = repair_page_fragmentation(onerel, buf, repair_info);
                LockBuffer((onerel), buf, BUFFER_LOCK_UNLOCK);
				
                if (page_altered)
			WriteBuffer(onerel,buf);
		else
			ReleaseBuffer(onerel, buf);
                
                MemoryContextResetAndDeleteChildren(page_cxt);
                
		if (IsShutdownProcessingMode()) {
			elog(ERROR, "shutting down");
		}

	}			/* walk along relation */

        MemoryContextSwitchTo(page_cxt->parent);
        MemoryContextDelete(page_cxt);
        
	vacuum_log(onerel, "Rel: Pages: %ld --> %ld; Tuple(s) moved: %ld.", nblocks, repair_info->last_moved, repair_info->num_moved);
	vacuum_log(onerel, "%s", vac_show_rusage(&ru0, rubuf));

        return marker;
}

/*
 * lazy_truncate_heap - try to truncate off any empty pages at the end
 */
static void
lazy_truncate_heap(Relation onerel, LVRelStats * vacrelstats)
{
	BlockNumber     old_rel_pages = vacrelstats->rel_pages;
	BlockNumber     new_rel_pages;
	BlockNumber    *pages;
	Size           *spaceavail;
	int            *pointers;
	int             n;
	int             i, j;
        long            factor = GetNextExtentFactor(onerel);

	VacRUsage       ru0;
	char            rubuf[255];

	vac_init_rusage(&ru0);
	/*
	 * Now that we have exclusive lock, look to see if the rel has grown
	 * whilst we were vacuuming with non-exclusive lock.  If so, give up;
	 * the newly added pages presumably contain non-deletable tuples.
	 */
	new_rel_pages = RelationGetNumberOfBlocks(onerel);
	if (new_rel_pages != old_rel_pages) {
		/*
		 * might as well use the latest news when we update pg_class
		 * stats
		 */
		vacrelstats->rel_pages = new_rel_pages;
		return;
	}
	/*
	 * Scan backwards from the end to verify that the end pages actually
	 * contain nothing we need to keep.  This is *necessary*, not
	 * optional, because other backends could have added tuples to these
	 * pages whilst we were vacuuming.
	 */
	new_rel_pages = count_nondeletable_pages(onerel, vacrelstats);

	if (new_rel_pages >= old_rel_pages) {
            vacuum_log(onerel,"Truncation: no freeable pages. exiting");
		return;
	}

        if ( factor >= (old_rel_pages - new_rel_pages) ) {
      /*  don't truncate pages that would just be recreated on the next allocation  */
            vacuum_log(onerel,"Truncation: extent factor (%d) exceeds freeable pages (%d). exiting",
                    factor,(old_rel_pages - new_rel_pages));
                return;
        }
	/*
	 * Okay to truncate.
	 * 
	 * First, flush any shared buffers for the blocks we intend to delete.
	 * FlushRelationBuffers is a bit more than we need for this, since it
	 * will also write out dirty buffers for blocks we aren't deleting,
	 * but it's the closest thing in bufmgr's API.
	 */
        /*
	i = FlushRelationBuffers(onerel, new_rel_pages);
	if (i < 0)
		elog(ERROR, "VACUUM (lazy_truncate_heap): FlushRelationBuffers returned %d",i);
        */
	/*
	 * Do the physical truncation.
	 */
        SetTransactionCommitType(TRANSACTION_SYNCED_COMMIT);
        FlushAllDirtyBuffers(true);
        ForgetFreespace(onerel,false);
	InvalidateRelationBuffers(onerel);
	TruncateHeapRelation(onerel, new_rel_pages);
	onerel->rd_nblocks = new_rel_pages;
	vacrelstats->rel_pages = onerel->rd_nblocks;	/* save new number of blocks */
	/*
	 * Drop free-space info for removed blocks; these must not get
	 * entered into the FSM!
	 */
	pages = vacrelstats->free_pages;
	spaceavail = vacrelstats->free_spaceavail;
	pointers = vacrelstats->free_pointers;
	n = vacrelstats->num_free_pages;
	j = 0;
	for (i = 0; i < n; i++) {
            if (pages[i] < new_rel_pages) {
                    pages[j] = pages[i];
                    spaceavail[j] = spaceavail[i];
                    pointers[j] = pointers[i];
                    j++;
            } else {
                    vacrelstats->total_free -= (spaceavail[i] > MaxTupleSize) ? MaxTupleSize : spaceavail[i];
            }
	}
	vacrelstats->num_free_pages = j;
	/*
	 * We keep the exclusive lock until commit (perhaps not necessary)?
	 */

	vacuum_log(onerel, "Truncated %u --> %u pages.",
	     old_rel_pages, onerel->rd_nblocks);
	vacuum_log(onerel, "%s", vac_show_rusage(&ru0, rubuf));
}

/*
 * Rescan end pages to verify that they are (still) empty of needed tuples.
 * 
 * Returns number of nondeletable pages (last nonempty page + 1).
 */
static          BlockNumber
count_nondeletable_pages(Relation onerel, LVRelStats * vacrelstats)
{
	BlockNumber     blkno;
	HeapTupleData   tuple;

	/* Strange coding of loop control is needed because blkno is unsigned */
	blkno = vacrelstats->rel_pages;
	while (blkno > vacrelstats->nonempty_pages) {
		Buffer          buf;
		Page            page;
		OffsetNumber    offnum, maxoff;
		bool            pgchanged, hastup;

		blkno--;

		buf = ReadBuffer(onerel, blkno);
		if (!BufferIsValid(buf))
			elog(ERROR, "bad buffer read in garbage collection");
		/* In this phase we only need shared access to the buffer */
		LockBuffer((onerel), buf, BUFFER_LOCK_SHARE);

		page = BufferGetPage(buf);

		if (PageIsNew(page) || PageIsEmpty(page)) {
                    hastup = false;
                    pgchanged = false;
			/* PageIsNew robably shouldn't happen... */
                } else {
                    pgchanged = false;
                    hastup = false;
                    maxoff = PageGetMaxOffsetNumber(page);
                    for (offnum = FirstOffsetNumber;
                         offnum <= maxoff;
                         offnum = OffsetNumberNext(offnum)) {
                            ItemId          itemid;
                            uint16          sv_infomask;

                            itemid = PageGetItemId(page, offnum);

                            if (ItemIdIsUsed(itemid)) {
                                    hastup = true;
                                    break;
                            }
                    }		/* scan along page */
                }
                
		LockBuffer((onerel), buf, BUFFER_LOCK_UNLOCK);
		ReleaseBuffer(onerel, buf);

		/* Done scanning if we found a tuple here */
		if (hastup)
			return blkno + 1;
	}

	/*
	 * If we fall out of the loop, all the previously-thought-to-be-empty
	 * pages really are; we need not bother to look at the last
	 * known-nonempty page.
	 */
	return vacrelstats->nonempty_pages;
}

/*
 * lazy_space_alloc - space allocation decisions for lazy vacuum
 * 
 * See the comments at the head of this file for rationale.
 */
static void
lazy_space_alloc(LVRelStats * vacrelstats, BlockNumber relblocks)
{
	long            maxtuples = 128;
	long            maxpages = 128;
        char*           maxtupleprop = GetProperty("freetuples");
        char*           maxpageprop = GetProperty("freepages");
        
        if ( maxtupleprop != NULL ) {
            maxtuples = atoi(maxtupleprop);
            if ( maxtuples < 0 ) maxtuples = 1;
        }
        if ( maxpageprop != NULL ) {
            maxpages = atoi(maxpageprop);
            if ( maxpages < 0 ) maxpages = 1;
        }

	maxtuples *= (1024);

        vacrelstats->rel_pages = relblocks;
	vacrelstats->num_dead_tuples = 0;
	vacrelstats->num_recently_dead_tuples = 0;
	vacrelstats->max_dead_tuples = maxtuples;
	vacrelstats->dead_tuples = (ItemPointer)
		palloc(maxtuples * sizeof(ItemPointerData));
	vacrelstats->recently_dead_tuples = (ItemPointer)
		palloc(maxtuples * sizeof(ItemPointerData));
        
	maxpages *= (1024);

	vacrelstats->max_size = 0;
	vacrelstats->min_size = ~0;

	vacrelstats->fs_is_heap = false;
	vacrelstats->num_free_pages = 0;
	vacrelstats->max_free_pages = maxpages;
	vacrelstats->free_pages = (BlockNumber *)
		palloc(maxpages * sizeof(BlockNumber));
	vacrelstats->free_spaceavail = (Size *)
		palloc(maxpages * sizeof(Size));
	vacrelstats->free_pointers = (int *)
		palloc(maxpages * sizeof(int));

}

/*
 * lazy_record_dead_tuple - remember one deletable tuple
 */
static void
lazy_record_dead_tuple(LVRelStats * vacrelstats,
		       ItemPointer itemptr)
{
	/*
	 * The array shouldn't overflow under normal behavior, but perhaps it
	 * could if we are given a really small VacuumMem. In that case, just
	 * forget the last few tuples.
	 */
	if (vacrelstats->num_dead_tuples < vacrelstats->max_dead_tuples) {
		vacrelstats->dead_tuples[vacrelstats->num_dead_tuples] = *itemptr;
		vacrelstats->num_dead_tuples++;
	}
}

static int
lazy_record_recently_dead(LVRelStats * vacrelstats,
			  ItemPointer itemptr)
{
	/*
	 * The array shouldn't overflow under normal behavior, but perhaps it
	 * could if we are given a really small VacuumMem. In that case, just
	 * forget the last few tuples.
	 */
	if (vacrelstats->num_recently_dead_tuples < vacrelstats->max_dead_tuples) {
		vacrelstats->recently_dead_tuples[vacrelstats->num_recently_dead_tuples++] = *itemptr;
	}
	return 0;
}


static void
lazy_record_sizes(LVRelStats * vacrelstats, Size min, Size max, Size average)
{
	vacrelstats->min_size = min;
	vacrelstats->max_size = max;
	vacrelstats->ave_size = average;
}

/*
 * lazy_record_free_space - remember free space on one page
 */
static void
lazy_record_free_space(LVRelStats * vacrelstats,
		       BlockNumber page,
		       Size avail, int unused_pointers)
{
	BlockNumber    *pages;
	Size           *spaceavail;
	int             n;
	int            *pointers;

	/* Ignore pages with little free space */
	int min_size = ( vacrelstats->min_size < BLCKSZ /32 ) ? vacrelstats->min_size : BLCKSZ /32;

	vacrelstats->total_free += (avail > MaxTupleSize) ? MaxTupleSize : avail;

	if (avail < min_size)
		return;

	/* Copy pointers to local variables for notational simplicity */
	pages = vacrelstats->free_pages;
	spaceavail = vacrelstats->free_spaceavail;
	pointers = vacrelstats->free_pointers;
	n = vacrelstats->max_free_pages;

	/* If we haven't filled the array yet, just keep adding entries */
	if (vacrelstats->num_free_pages < n) {
            pages[vacrelstats->num_free_pages] = page;
            spaceavail[vacrelstats->num_free_pages] = avail;
            pointers[vacrelstats->num_free_pages] = unused_pointers;
            vacrelstats->num_free_pages++;
            return;
	}
	/*----------
	 * The rest of this routine works with "heap" organization of the
	 * free space arrays, wherein we maintain the heap property
	 *			spaceavail[(j-1) div 2] <= spaceavail[j]  for 0 < j < n.
	 * In particular, the zero'th element always has the smallest available
	 * space and can be discarded to make room for a new page with more space.
	 * See Knuth's discussion of heap-based priority queues, sec 5.2.3;
	 * but note he uses 1-origin array subscripts, not 0-origin.
	 *----------
	 */

	/* If we haven't yet converted the array to heap organization, do it */
	if (!vacrelstats->fs_is_heap) {
		/*
		 * Scan backwards through the array, "sift-up" each value
		 * into its correct position.  We can start the scan at n/2-1
		 * since each entry above that position has no children to
		 * worry about.
		 */
		int             l = n / 2;

		while (--l >= 0) {
			BlockNumber     R = pages[l];
			Size            K = spaceavail[l];
			int             P = pointers[l];
			int             i;	/* i is where the "hole" is */

			i = l;
			for (;;) {
				int             j = 2 * i + 1;

				if (j >= n)
					break;
				if (j + 1 < n && spaceavail[j] > spaceavail[j + 1])
					j++;
				if (K <= spaceavail[j])
					break;
				pages[i] = pages[j];
				spaceavail[i] = spaceavail[j];
				pointers[i] = pointers[j];
				i = j;
			}
			pages[i] = R;
			spaceavail[i] = K;
			pointers[i] = P;
		}

		vacrelstats->fs_is_heap = true;
	}
	/* If new page has more than zero'th entry, insert it into heap */
	if (avail > spaceavail[0]) {
		/*
		 * Notionally, we replace the zero'th entry with the new
		 * data, and then sift-up to maintain the heap property.
		 * Physically, the new data doesn't get stored into the
		 * arrays until we find the right location for it.
		 */
		int             i = 0;	/* i is where the "hole" is */

		for (;;) {
			int             j = 2 * i + 1;

			if (j >= n)
				break;
			if (j + 1 < n && spaceavail[j] > spaceavail[j + 1])
				j++;
			if (avail <= spaceavail[j])
				break;
			pages[i] = pages[j];
			spaceavail[i] = spaceavail[j];
			pointers[i] = pointers[j];
			i = j;
		}
                pages[i] = page;
                spaceavail[i] = avail;
                pointers[i] = unused_pointers;
	}
}
/*
 * Comparator routines for use with qsort() and bsearch().
 */
static int
vac_cmp_itemptr(const void *left, const void *right)
{
	BlockNumber     lblk, rblk;
	OffsetNumber    loff, roff;

	lblk = ItemPointerGetBlockNumber((ItemPointer) left);
	rblk = ItemPointerGetBlockNumber((ItemPointer) right);

	if (lblk < rblk)
		return -1;
	if (lblk > rblk)
		return 1;

	loff = ItemPointerGetOffsetNumber((ItemPointer) left);
	roff = ItemPointerGetOffsetNumber((ItemPointer) right);

	if (loff < roff)
		return -1;
	if (loff > roff)
		return 1;

	return 0;
}


static void
vac_open_indexes(Relation relation, int *nindexes, Relation ** Irel)
{
	List           *indexoidlist, *indexoidscan;
	int             i;
	indexoidlist = RelationGetIndexList(relation);
	*nindexes = length(indexoidlist);

	if (*nindexes > 0)
		*Irel = (Relation *) palloc(*nindexes * sizeof(Relation));
	else
		*Irel = NULL;
	i = 0;
	foreach(indexoidscan, indexoidlist) {
		Oid             indexoid = lfirsti(indexoidscan);

		(*Irel)[i] = index_open(indexoid);
                /*  make sure that a fresh block count is acquired, see freespace.c:RelationGetNumberofBlocks */
                (*Irel)[i]->rd_nblocks = 0;
		/*
		 * be extra careful this should not be needed with heap lock
		 * but who knows
		 */
		LockRelation((*Irel)[i], ShareUpdateExclusiveLock);
		i++;
	}
	freeList(indexoidlist);
}




static void
vac_close_indexes(int nindexes, Relation * Irel)
{
	if (Irel == (Relation *) NULL)
		return;
	while (nindexes--) {
		UnlockRelation(Irel[nindexes], ShareUpdateExclusiveLock);
		index_close(Irel[nindexes]);
	}
	pfree(Irel);
}

static List    *
RelationGetIndexList(Relation relation)
{
	Relation        indrel;
	Relation        irel = (Relation) NULL;
	ScanKeyData     skey;
	IndexScanDesc   sd = (IndexScanDesc) NULL;
	HeapScanDesc    hscan = (HeapScanDesc) NULL;
	bool            hasindex;
	List           *result;
	MemoryContext   oldcxt;

	/* Quick exit if we already computed the list. */
	if (relation->rd_indexfound)
		return listCopy(relation->rd_indexlist);

	/* Prepare to scan pg_index for entries having indrelid = this rel. */
	indrel = heap_openr(IndexRelationName, AccessShareLock);
	/*
	 * hasindex = (indrel->rd_rel->relhasindex &&
	 * !IsIgnoringSystemIndexes());
	 */
	hasindex = false;
	if (hasindex) {
		irel = index_openr(IndexRelidIndex);
		ScanKeyEntryInitialize(&skey,
				       (bits16) 0x0,
				       (AttrNumber) 1,
				       (RegProcedure) F_OIDEQ,
			      ObjectIdGetDatum(RelationGetRelid(relation)));
		sd = index_beginscan(irel, false, 1, &skey);
	} else {
		ScanKeyEntryInitialize(&skey, (bits16) 0x0, (AttrNumber) Anum_pg_index_indrelid,
				       (RegProcedure) F_OIDEQ,
			      ObjectIdGetDatum(RelationGetRelid(relation)));
		hscan = heap_beginscan(indrel, SnapshotNow, 1, &skey);
	}

	/*
	 * We build the list we intend to return (in the caller's context)
	 * while doing the scan. After successfully completing the scan, we
	 * copy that list into the relcache entry. This avoids cache-context
	 * memory leakage if we get some sort of error partway through.
	 */
	result = NIL;
	for (;;) {
		HeapTupleData   tuple;
		HeapTuple       htup;
		Buffer          buffer;
		Form_pg_index   index;

		if (hasindex) {
			if (index_getnext(sd, ForwardScanDirection))
				break;

			tuple.t_self = sd->xs_ctup.t_self;
			heap_fetch(indrel, SnapshotNow, &tuple, &buffer);
			if (tuple.t_data == NULL)
				continue;
			htup = &tuple;
		} else {
			htup = heap_getnext(hscan);
			if (!HeapTupleIsValid(htup))
				break;
		}

		index = (Form_pg_index) GETSTRUCT(htup);
		result = insert_ordered_oid(result, index->indexrelid);

		if (hasindex)
			ReleaseBuffer(indrel, buffer);

	}
	if (hasindex) {
		index_endscan(sd);
		index_close(irel);
	} else {
		heap_endscan(hscan);
	}
	heap_close(indrel, AccessShareLock);

	oldcxt = MemoryContextSwitchTo(RelationGetCacheContext());
	relation->rd_indexlist = listCopy(result);
	relation->rd_indexfound = true;
	MemoryContextSwitchTo(oldcxt);
	return result;
}

static List    *
insert_ordered_oid(List * list, Oid datum)
{
	List           *l;
	/* Does the datum belong at the front? */
	if (list == NIL || datum < (Oid) lfirsti(list))
		return lconsi(datum, list);
	/* No, so find the entry it belongs after */
	l = list;
	for (;;) {
		List           *n = lnext(l);
		if (n == NIL || datum < (Oid) lfirsti(n))
			break;	/* it belongs before n */
		l = n;
	}
	/* Insert datum into list after item l */
	lnext(l) = lconsi(datum, lnext(l));
	return list;
}


static void
vac_update_relstats(Oid relid, BlockNumber num_pages, TupleCount num_tuples,
		    bool hasindex)
{
	Relation        rd;
	HeapTupleData   rtup;
	HeapTuple       ctup;
	Form_pg_class   pgcform;
	Buffer          buffer;

	/*
	 * update number of tuples and number of pages in pg_class
	 */
	rd = heap_openr(RelationRelationName, RowExclusiveLock);

	ctup = SearchSysCacheTupleCopy(RELOID,
				       ObjectIdGetDatum(relid),
				       0, 0, 0);
	if (!HeapTupleIsValid(ctup))
		elog(ERROR, "pg_class entry for relid %u vanished during vacuuming",
		     relid);

	/* get the buffer cache tuple */
	rtup.t_self = ctup->t_self;
	heap_freetuple(ctup);
	heap_fetch(rd, SnapshotNow, &rtup, &buffer);

	/* overwrite the existing statistics in the tuple */
	pgcform = (Form_pg_class) GETSTRUCT(&rtup);
	pgcform->relpages = (long) num_pages;
	pgcform->reltuples = num_tuples;
	pgcform->relhasindex = hasindex;

	/*
	 * If we have discovered that there are no indexes, then there's no
	 * primary key either.	This could be done more thoroughly...
	 */
	if (!hasindex)
		pgcform->relhaspkey = false;

	/* invalidate the tuple in the cache and write the buffer */
	/*
	 * This info is not vital so let the poolsweep reset all caches at the
	 * time of pause  */
	RelationInvalidateHeapTuple(rd, &rtup);
	WriteBuffer(rd, buffer);

	heap_close(rd, RowExclusiveLock);
}





/*
 * Initialize usage snapshot.
 */
void
vac_init_rusage(VacRUsage * ru0)
{
	struct timezone tz;
        memset(ru0,0x00,sizeof(VacRUsage));
	getrusage(RUSAGE_SELF, &ru0->ru);
	gettimeofday(&ru0->tv, &tz);
}

/*
 * Compute elapsed time since ru0 usage snapshot, and format into a
 * displayable string.  Result is in a static string, which is tacky, but no
 * one ever claimed that the Postgres backend is threadable...
 */
const char     *
vac_show_rusage(VacRUsage * ru0, char *buf)
{
	VacRUsage       ru1;

	vac_init_rusage(&ru1);

	if (ru1.tv.tv_usec < ru0->tv.tv_usec) {
		ru1.tv.tv_sec--;
		ru1.tv.tv_usec += 1000000;
	}

	sprintf(buf,
		"time elapsed %ld.%.2ld sec.",
		(ru1.tv.tv_sec - ru0->tv.tv_sec),
		((ru1.tv.tv_usec - ru0->tv.tv_usec) / 10000L));

	return buf;
}

static void
vac_mkindesc(Relation onerel, int nindices, Relation * Irel, IndDesc ** Idesc)
{
	IndDesc        *idcur;
	HeapTuple       cachetuple;
	AttrNumber     *attnumP;
	int             natts;
	int             i;

	*Idesc = (IndDesc *) palloc(nindices * sizeof(IndDesc));

	for (i = 0, idcur = *Idesc; i < nindices; i++, idcur++) {
		cachetuple = SearchSysCacheTupleCopy(INDEXRELID,
				ObjectIdGetDatum(RelationGetRelid(Irel[i])),
						     0, 0, 0);
		Assert(cachetuple);

		/*
		 * we never free the copy we make, because Idesc needs it for
		 * later
		 */
		idcur->tform = (Form_pg_index) GETSTRUCT(cachetuple);
		for (attnumP = &(idcur->tform->indkey[0]), natts = 0;
		     natts < INDEX_MAX_KEYS && *attnumP != InvalidAttrNumber;
		     attnumP++, natts++);
		if (idcur->tform->indproc != InvalidOid) {
			idcur->finfoP = &(idcur->finfo);
			FIgetnArgs(idcur->finfoP) = natts;
			natts = 1;
			FIgetProcOid(idcur->finfoP) = idcur->tform->indproc;
			*(FIgetname(idcur->finfoP)) = '\0';
		} else
			idcur->finfoP = (FuncIndexInfo *) NULL;

		idcur->natts = natts;
	}

}				/* vc_mkindesc */


static          VRelList
vc_getrels()
{
	Relation        rel;
	TupleDesc       tupdesc;
	HeapScanDesc    scan;
	HeapTuple       tuple;
	MemoryContext   old;
	VRelList        vrl, cur;
	Datum           d;
	char           *rname;
	char            rkind;
	bool            n;
	bool            found = false;
	ScanKeyData     key;

	ScanKeyEntryInitialize(&key, 0x0, Anum_pg_class_relkind,
			       F_CHAREQ, CharGetDatum('r'));

	vrl = cur = (VRelList) NULL;

	rel = heap_openr(RelationRelationName, AccessShareLock);
	tupdesc = RelationGetDescr(rel);

	scan = heap_beginscan(rel, SnapshotNow, 1, &key);

	while (HeapTupleIsValid(tuple = heap_getnext(scan))) {
		found = true;

		d = HeapGetAttr(tuple, Anum_pg_class_relname, tupdesc, &n);
		rname = (char *) d;

		d = HeapGetAttr(tuple, Anum_pg_class_relkind, tupdesc, &n);

		rkind = DatumGetChar(d);

		if (rkind != RELKIND_RELATION) {
			if (rkind == RELKIND_INDEX)
				vacuum_log(rel, "vacuum: ignoring index");
			else if (rkind == RELKIND_SPECIAL)
				vacuum_log(rel, "vacuum: ignoring special");
			continue;
		}
		/* get a relation list entry for this guy */
		old = MemoryContextSwitchTo(MemoryContextGetEnv()->QueryContext);
		if (vrl == (VRelList) NULL)
			vrl = cur = (VRelList) palloc(sizeof(VRelListData));
		else {
			cur->vrl_next = (VRelList) palloc(sizeof(VRelListData));
			cur = cur->vrl_next;
		}
		MemoryContextSwitchTo(old);

		cur->vrl_relid = tuple->t_data->t_oid;
		cur->vrl_next = (VRelList) NULL;
	}
	if (found == false)
		vacuum_log(rel,"Vacuum: table not found");

	heap_endscan(scan);
	heap_close(rel, AccessShareLock);


	return vrl;
}

void
lazy_vacuum_database(bool verbose)
{
	VRelList        vrl, cur;

	Oid             dbid = GetDatabaseId();

	TransactionId   xunder = GetCheckpointId();

	/* get list of relations */
	vrl = vc_getrels();

	/* vacuum each heap relation */
	for (cur = vrl; cur != (VRelList) NULL; cur = cur->vrl_next) {

		CommitTransaction();
		StartTransaction();

		if (IsShutdownProcessingMode()) {
			elog(ERROR, "system is shutting down");
		}
		lazy_open_vacuum_rel(cur->vrl_relid, true, true);
		DropVacuumRequests(cur->vrl_relid, dbid);
	}

	SetTransactionLowWaterMark(xunder);
}

void  vacuum_log(Relation rel, char* pattern, ...) {
    char            msg[256];
    va_list         args;

    va_start(args, pattern);
    vsprintf(msg,pattern,args);
#ifdef SUNOS
    DTRACE_PROBE3(mtpg,vacuum__msg,msg,RelationGetRelid(rel),GetDatabaseId());  
#endif
#ifdef DEBUGLOGS
    elog(DEBUG,"vacuum:%d/%d %s",RelationGetRelid(rel),GetDatabaseId(),msg); 
#endif
    va_end(args);
}


