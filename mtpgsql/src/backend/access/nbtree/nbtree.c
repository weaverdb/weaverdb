/*-------------------------------------------------------------------------
 *
 * nbtree.c
 *	  Implementation of Lehman and Yao's btree management algorithm for
 *	  Postgres.
 *
 * NOTES
 *	  This file contains only the public interface routines.
 *
 *
 * Portions Copyright (c) 1996-2002, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * IDENTIFICATION
 *	  $Header: /cvs/weaver/mtpgsql/src/backend/access/nbtree/nbtree.c,v 1.3 2007/02/27 19:19:04 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include "env/env.h"
#include "access/genam.h"
#include "access/heapam.h"
#include "access/nbtree.h"
#include "catalog/index.h"
#include "catalog/pg_index.h"
#include "executor/executor.h"
#include "miscadmin.h"
#include "storage/sinval.h"
#include "utils/tqual.h"
#include "env/poolsweep.h"
#include "env/delegatedscan.h"
#include "env/freespace.h"
#include "utils/syscache.h"

/* Working state for btbuild and its callback */
typedef struct {
    bool usefast;
    bool isUnique;
    bool hasDead;
    Relation heapRel;
    BTSpool * spool;
    BTSpool * dead_spool;
    TupleCount indtuples;
} BTBuildState;


#define BuildingBtree 	GetIndexGlobals()->BuildingBtree

static BlockNumber
_bt_check_pagelinks(Relation rel, BlockNumber target);

static int
cmp_itemptr(const void *left, const void *right);
static void _bt_restscan(IndexScanDesc scan);
static void btbuildCallback(Relation index,
        HeapTuple htup,
        Datum *attdata,
        char *nulls,
        bool tupleIsAlive,
        void *state);

/*
 * AtEOXact_nbtree() --- clean up nbtree subsystem at xact abort or commit.
 */
void
AtEOXact_nbtree(void) {
    /*
     * Note: these actions should only be necessary during xact abort; but
     * they can't hurt during a commit.
     */

    /* If we were building a btree, we ain't anymore. */
    BuildingBtree = false;
}

/*
 *	btbuild() -- build a new btree index.
 *
 *		We use a global variable to record the fact that we're creating
 *		a new index.  This is used to avoid high-concurrency locking,
 *		since the index won't be visible until this transaction commits
 *		and since building is guaranteed to be single-threaded.
 */
Datum
btbuild(Relation heap, Relation index, int natts,
        AttrNumber *attnum, IndexStrategy istrat, uint16 pcount,
        Datum *params, FuncIndexInfo *finfo, PredInfo *predInfo) {
    HeapScanDesc hscan;
    HeapTuple htup;

    IndexTuple itup;
    TupleDesc htupdesc,
            itupdesc;
    Datum * attdata = NULL;
    char * nulls = NULL;
    InsertIndexResult res = 0;
    int i;
    BTItem btitem;
    TupleCount reltuples = 0;

    HTSV_Result isAlive = HEAPTUPLE_LIVE;
    TransactionId cxid = InvalidTransactionId;


    MemoryContext parent = MemoryContextGetCurrentContext();
    MemoryContext build_context = AllocSetContextCreate(parent,
            "NbtBuildContext",
            ALLOCSET_DEFAULT_MINSIZE,
            ALLOCSET_DEFAULT_INITSIZE,
            ALLOCSET_DEFAULT_MAXSIZE);
    MemoryContext scan_cxt = SubSetContextCreate(build_context, "BuildScanContext");

    MemoryContextSwitchTo(build_context);

    cxid = GetCurrentTransactionId();

#ifndef OMIT_PARTIAL_INDEX
    ExprContext * econtext = (ExprContext *) NULL;
    TupleTable tupleTable = (TupleTable) NULL;
    TupleTableSlot * slot = (TupleTableSlot *) NULL;
#endif

    Node *pred = predInfo->pred,
            *oldPred = predInfo->oldPred;

    Env* env = GetEnv();
    BTBuildState buildstate;

    /* set flag to disable locking */
    BuildingBtree = true;

    /* get tuple descriptors for heap and index relations */
    htupdesc = RelationGetDescr(heap);
    itupdesc = RelationGetDescr(index);

    /* get space for data items that'll appear in the index tuple */
    attdata = (Datum *) palloc(natts * sizeof (Datum));
    nulls = (char *) palloc(natts * sizeof (char));
    /*
     * bootstrap processing does something strange, so don't use
     * sort/build for initial catalog indices.	at some point i need to
     * look harder at this.  (there is some kind of incremental processing
     * going on there.) -- pma 08/29/95
     */
    buildstate.usefast = (GetIndexGlobals()->FastIndexBuild && IsNormalProcessingMode());
    buildstate.isUnique = IndexIsUniqueNoCache(RelationGetRelid(index));
    buildstate.hasDead = false;
    buildstate.heapRel = heap;
    buildstate.spool = NULL;
    buildstate.dead_spool = NULL;
    buildstate.indtuples = 0;

#ifdef BTREE_BUILD_STATS
    if (log_btree_build_stats)
        ResetUsage();
#endif   /* BTREE_BUILD_STATS */

    /*
     * We expect to be called exactly once for any index relation. If
     * that's not the case, big trouble's what we have.
     */
    if (RelationGetNumberOfBlocks(index) != 0)
        elog(ERROR, "%s already contains data",
            RelationGetRelationName(index));

    /* initialize the btree index metadata page */
    _bt_metapinit(index);

    if (buildstate.usefast) {
        buildstate.spool = _bt_spoolinit(index, buildstate.isUnique);
        if (buildstate.isUnique) buildstate.dead_spool = _bt_spoolinit(index, false);
    }


    hscan = heap_beginscan(heap, SnapshotAny, 0, (ScanKey) NULL);

    while (HeapTupleIsValid(htup = heap_getnext(hscan))) {
        HeapTupleData tp;

        ItemId lp;
        PageHeader dp;
        OffsetNumber offnum;
        int len = 0;

        reltuples++;

        CheckForCancel();

        MemoryContextResetAndDeleteChildren(scan_cxt);
        MemoryContextSwitchTo(scan_cxt);
        /*
         * If oldPred != NULL, this is an EXTEND INDEX command, so skip
         * this tuple if it was already in the existing partial index
         */
        if (oldPred != NULL) {
#ifndef OMIT_PARTIAL_INDEX
            ExecStoreTuple(htup, slot, false);
            if (ExecQual((List *) oldPred, econtext, false)) {
                buildstate.indtuples++;
                continue;
            }
#endif	 /* OMIT_PARTIAL_INDEX */
        }

        /*
         * Skip this tuple if it doesn't satisfy the partial-index
         * predicate
         */
        if (pred != NULL) {
#ifndef OMIT_PARTIAL_INDEX
            /* SetSlotContents(slot, htup); */
            ExecStoreTuple(htup, slot, false);
            if (!ExecQual((List *) pred, econtext, false))
                continue;
#endif	 /* OMIT_PARTIAL_INDEX */
        }

        buildstate.indtuples++;

        /*
         * For the current heap tuple, extract all the attributes we use
         * in this index, and note which are null.
         */

        for (i = 1; i <= natts; i++) {
            int attoff;
            bool attnull;

            /*
             * Offsets are from the start of the tuple, and are
             * zero-based; indices are one-based.  The next call returns i
             * - 1.  That's data hiding for you.
             */

            attoff = AttrNumberGetAttrOffset(i);
            attdata[attoff] = GetIndexValue(htup,
                    htupdesc,
                    attoff,
                    attnum,
                    finfo,
                    &attnull);
            nulls[attoff] = (attnull ? 'n' : ' ');
        }

        /* form an index tuple and point it at the heap tuple */
        itup = index_formtuple(itupdesc, attdata, nulls);
        /*
         * If the single index key is null, we don't insert it into the
         * index.  Btrees support scans on <, <=, =, >=, and >. Relational
         * algebra says that A op B (where op is one of the operators
         * above) returns null if either A or B is null.  This means that
         * no qualification used in an index scan could ever return true
         * on a null attribute.  It also means that indices can't be used
         * by ISNULL or NOTNULL scans, but that's an artifact of the
         * strategy map architecture chosen in 1986, not of the way nulls
         * are handled here.
         */

        /*
         * New comments: NULLs handling. While we can't do NULL
         * comparison, we can follow simple rule for ordering items on
         * btree pages - NULLs greater NOT_NULLs and NULL = NULL is TRUE.
         * Sure, it's just rule for placing/finding items and no more -
         * keytest'll return FALSE for a = 5 for items having 'a' isNULL.
         * Look at _bt_skeycmp, _bt_compare and _bt_itemcmp for how it
         * works.				 - vadim 03/23/97
         *
         * if (itup->t_info & INDEX_NULL_MASK) { pfree(itup); continue; }
         */

        itup->t_tid = htup->t_self;
        btitem = _bt_formitem(itup);

        /*
         * if we are doing bottom-up btree build, we insert the index into
         * a spool file for subsequent processing.	otherwise, we insert
         * into the btree.
         */
        /* switch back to build context 
                so that any memory created during 
                spooling is persistent until 
                build is done.
         */
        MemoryContextSwitchTo(build_context);
        if (buildstate.usefast) {
            if (buildstate.isUnique) {
                switch (HeapTupleSatisfiesVacuum(htup->t_data, cxid)) {
                    case HEAPTUPLE_STILLBORN:
                    case HEAPTUPLE_RECENTLY_DEAD:
                    case HEAPTUPLE_DEAD:
                        _bt_spool(btitem, buildstate.dead_spool);
                        buildstate.hasDead = true;
                        break;
                    case HEAPTUPLE_LIVE:
                        _bt_spool(btitem, buildstate.spool);
                        break;
                    case HEAPTUPLE_INSERT_IN_PROGRESS:
                        _bt_spool(btitem, buildstate.dead_spool);
                        buildstate.hasDead = true;
                        break;
                    case HEAPTUPLE_DELETE_IN_PROGRESS:
                        _bt_spool(btitem, buildstate.spool);
                        break;
                    default:
                        elog(ERROR, "heap is in inconsistent state %d", isAlive);
                        break;
                }
            } else {
                _bt_spool(btitem, buildstate.spool);
            }
        } else {
            res = _bt_doinsert(index, btitem, buildstate.isUnique, heap);
        }

        pfree(btitem);
        pfree(itup);
        if (res)
            pfree(res);

    }
    /* okay, all heap tuples are indexed */

    heap_endscan(hscan);

    /*
     * if we are doing bottom-up btree build, finish the build by (1)
     * completing the sort of the spool file, (2) inserting the sorted
     * tuples into btree pages and (3) building the upper levels.
     */
    if (buildstate.usefast) {
        BTSpool* builders = buildstate.spool;
        if (buildstate.dead_spool) {
            if (buildstate.hasDead) {
                _bt_spoolmerge(buildstate.dead_spool, buildstate.spool);
                builders = buildstate.dead_spool;
                _bt_spooldestroy(buildstate.spool);
                buildstate.spool = NULL;
            } else {
                _bt_spooldestroy(buildstate.dead_spool);
                buildstate.dead_spool = NULL;
            }
        }
        _bt_leafbuild(builders);
        _bt_spooldestroy(builders);
    }

#ifdef BTREE_BUILD_STATS
    if (log_btree_build_stats) {
        ShowUsage("BTREE BUILD STATS");
        ResetUsage();
    }
#endif   /* BTREE_BUILD_STATS */

    /* all done */
    pfree(nulls);
    pfree(attdata);
    BuildingBtree = false;

    /*
     * Since we just counted the tuples in the heap, we update its stats
     * in pg_class to guarantee that the planner takes advantage of the
     * index we just created.  But, only update statistics during normal
     * index definitions, not for indices on system catalogs created
     * during bootstrap processing.  We must close the relations before
     * updating statistics to guarantee that the relcache entries are
     * flushed when we increment the command counter in UpdateStats(). But
     * we do not release any locks on the relations; those will be held
     * until end of transaction.
     */
    if (IsNormalProcessingMode()) {
        Oid hrelid = RelationGetRelid(heap);
        Oid irelid = RelationGetRelid(index);

        heap_close(heap, NoLock);
        index_close(index);
        UpdateStats(hrelid, reltuples);
        UpdateStats(irelid, buildstate.indtuples);
    }
    MemoryContextSwitchTo(parent);
    MemoryContextDelete(build_context);
    /*	
            elog(DEBUG,"b-tree rebuild %s heap tuples:%ld index tuples:%ld",RelationGetRelationName(index),reltuples,buildstate.indtuples);
     **/
}

/*
 * Per-tuple callback from IndexBuildHeapScan
 */
static void
btbuildCallback(Relation index,
        HeapTuple htup,
        Datum *attdata,
        char *nulls,
        bool tupleIsAlive,
        void *state) {
    BTBuildState *buildstate = (BTBuildState *) state;
    IndexTuple itup;
    BTItem btitem;
    InsertIndexResult res;

    /* form an index tuple and point it at the heap tuple */
    itup = index_formtuple(RelationGetDescr(index), attdata, nulls);
    itup->t_tid = htup->t_self;

    btitem = _bt_formitem(itup);

    /*
     * if we are doing bottom-up btree build, we insert the index into a
     * spool file for subsequent processing.  otherwise, we insert into
     * the btree.
     */
    if (buildstate->usefast) {
        _bt_spool(btitem, buildstate->spool);
    } else {
        res = _bt_doinsert(index, btitem,
                buildstate->isUnique, buildstate->heapRel);
        if (res)
            pfree(res);
    }

    buildstate->indtuples += 1;

    pfree(btitem);
    pfree(itup);
}

/*
 *	btinsert() -- insert an index tuple into a btree.
 *
 *		Descend the tree recursively, find the appropriate location for our
 *		new tuple, put it there, set its unique OID as appropriate, and
 *		return an InsertIndexResult to the caller.
 */
Datum
btinsert(Relation rel, Datum *datum, char *nulls, ItemPointer ht_ctid, Relation heapRel, bool is_put) {
    InsertIndexResult res;
    BTItem btitem;
    IndexTuple itup;
    IndexProp atts = IndexProperties(RelationGetRelid(rel));

    /* generate an index tuple */
    itup = index_formtuple(RelationGetDescr(rel), datum, nulls);
    itup->t_tid = *ht_ctid;
    btitem = _bt_formitem(itup);

    if (!is_put && IndexPropIsDeferred(atts)) {
        res = _bt_queueinsert(rel, btitem, IndexPropIsUnique(atts), heapRel);
    } else {
        res = _bt_doinsert(rel, btitem, IndexPropIsUnique(atts), heapRel);
    }

    pfree(btitem);
    pfree(itup);

    return PointerGetDatum(res);
}

/*
 *	btgettuple() -- Get the next tuple in the scan.
 */
bool
btgettuple(IndexScanDesc scan, ScanDirection dir) {
    BTScanOpaque so = (BTScanOpaque) scan->opaque;
    Page page;
    OffsetNumber offnum;
    bool res;
    Relation rel = scan->relation;

    /*
     * If we've already initialized this scan, we can just advance it in
     * the appropriate direction.  If we haven't done so yet, we call a
     * routine to get the first item in the scan.
     */
    if (ItemPointerIsValid(&(scan->currentItemData))) {
        /*
         * Restore scan position using heap TID returned by previous call
         * to btgettuple(). _bt_restscan() re-grabs the read lock on the
         * buffer, too.
         */
        _bt_restscan(scan);

        /*
         * Now continue the scan.
         */
        res = _bt_next(scan, dir);
    } else
        res = _bt_first(scan, dir);


    /*
     * Save heap TID to use it in _bt_restscan.  Then release the read
     * lock on the buffer so that we aren't blocking other backends.
     *
     * NOTE: we do keep the pin on the buffer!	This is essential to ensure
     * that someone else doesn't delete the index entry we are stopped on.
     */
    if (res) {
        ((BTScanOpaque) scan->opaque)->curHeapIptr = scan->xs_ctup.t_self;
        LockBuffer((rel), ((BTScanOpaque) scan->opaque)->btso_curbuf,
                BUFFER_LOCK_UNLOCK);
    }

    return res;
}

/*
 *	btbeginscan() -- start a scan on a btree index
 */
Datum
btbeginscan(Relation rel, bool fromEnd, uint16 keysz, ScanKey key) {
    IndexScanDesc scan;

    /* get the scan */
    scan = RelationGetIndexScan(rel, false, keysz, key);

    return PointerGetDatum(scan);
}

/*
 *	btrescan() -- rescan an index relation
 */
Datum
btrescan(IndexScanDesc scan, bool fromEnd, ScanKey scankey) {
    ItemPointer iptr;
    BTScanOpaque so;

    so = (BTScanOpaque) scan->opaque;

    if (so == NULL) /* if called from btbeginscan */ {
        so = (BTScanOpaque) palloc(sizeof (BTScanOpaqueData));
        so->btso_curbuf = so->btso_mrkbuf = InvalidBuffer;
        ItemPointerSetInvalid(&(so->curHeapIptr));
        ItemPointerSetInvalid(&(so->mrkHeapIptr));
        if (scan->numberOfKeys > 0)
            so->keyData = (ScanKey) palloc(scan->numberOfKeys * sizeof (ScanKeyData));
        else
            so->keyData = (ScanKey) NULL;
        scan->opaque = so;
    }

    /* we aren't holding any read locks, but gotta drop the pins */
    if (ItemPointerIsValid(iptr = &(scan->currentItemData))) {
        ReleaseBuffer(scan->relation, so->btso_curbuf);
        so->btso_curbuf = InvalidBuffer;
        ItemPointerSetInvalid(&(so->curHeapIptr));
        ItemPointerSetInvalid(iptr);
    }

    if (ItemPointerIsValid(iptr = &(scan->currentMarkData))) {
        ReleaseBuffer(scan->relation, so->btso_mrkbuf);
        so->btso_mrkbuf = InvalidBuffer;
        ItemPointerSetInvalid(&(so->mrkHeapIptr));
        ItemPointerSetInvalid(iptr);
    }

    /*
     * Reset the scan keys. Note that keys ordering stuff moved to
     * _bt_first.	   - vadim 05/05/97
     */
    so->numberOfKeys = scan->numberOfKeys;
    if (scan->numberOfKeys > 0) {
        memmove(scan->keyData,
                scankey,
                scan->numberOfKeys * sizeof (ScanKeyData));
        memmove(so->keyData,
                scankey,
                so->numberOfKeys * sizeof (ScanKeyData));
    }
}

void
btmovescan(IndexScanDesc scan, Datum v) {
    ItemPointer iptr;
    BTScanOpaque so;

    so = (BTScanOpaque) scan->opaque;

    /* we aren't holding any read locks, but gotta drop the pin */
    if (ItemPointerIsValid(iptr = &(scan->currentItemData))) {
        ReleaseBuffer(scan->relation, so->btso_curbuf);
        so->btso_curbuf = InvalidBuffer;
        ItemPointerSetInvalid(iptr);
    }

    so->keyData[0].sk_argument = v;
}

/*
 *	btendscan() -- close down a scan
 */
Datum
btendscan(IndexScanDesc scan) {
    BTScanOpaque so;

    so = (BTScanOpaque) scan->opaque;

    /* we aren't holding any read locks, but gotta drop the pins */
    if (BufferIsValid(so->btso_curbuf))
        ReleaseBuffer(scan->relation, so->btso_curbuf);
    so->btso_curbuf = InvalidBuffer;
    ItemPointerSetInvalid(&scan->currentItemData);

    if (BufferIsValid(so->btso_mrkbuf))
        ReleaseBuffer(scan->relation, so->btso_mrkbuf);
    so->btso_mrkbuf = InvalidBuffer;
    ItemPointerSetInvalid(&scan->currentMarkData);

    if (so->keyData != (ScanKey) NULL)
        pfree(so->keyData);

    pfree(so);

}

/*
 *	btmarkpos() -- save current scan position
 */
Datum
btmarkpos(IndexScanDesc scan) {
    ItemPointer iptr;
    BTScanOpaque so;

    so = (BTScanOpaque) scan->opaque;

    /* we aren't holding any read locks, but gotta drop the pin */
    if (ItemPointerIsValid(iptr = &(scan->currentMarkData))) {
        ReleaseBuffer(scan->relation, so->btso_mrkbuf);
        so->btso_mrkbuf = InvalidBuffer;
        ItemPointerSetInvalid(iptr);
    }

    /* bump pin on current buffer for assignment to mark buffer */
    if (ItemPointerIsValid(&(scan->currentItemData))) {
        so->btso_mrkbuf = ReadBuffer(scan->relation, BufferGetBlockNumber(so->btso_curbuf));
        if (!BufferIsValid(so->btso_mrkbuf))
            elog(ERROR, "bad buffer read while marking btree %s position", RelationGetRelationName(scan->relation));
        scan->currentMarkData = scan->currentItemData;
        so->mrkHeapIptr = so->curHeapIptr;
    }
}

/*
 *	btrestrpos() -- restore scan to last saved position
 */
Datum
btrestrpos(IndexScanDesc scan) {
    ItemPointer iptr;
    BTScanOpaque so;

    so = (BTScanOpaque) scan->opaque;

    /* we aren't holding any read locks, but gotta drop the pin */
    if (ItemPointerIsValid(iptr = &(scan->currentItemData))) {
        ReleaseBuffer(scan->relation, so->btso_curbuf);
        so->btso_curbuf = InvalidBuffer;
        ItemPointerSetInvalid(iptr);
    }

    /* bump pin on marked buffer */
    if (ItemPointerIsValid(&(scan->currentMarkData))) {
        so->btso_curbuf = ReadBuffer(scan->relation, BufferGetBlockNumber(so->btso_mrkbuf));
        if (!BufferIsValid(so->btso_curbuf))
            elog(ERROR, "bad buffer read while restoring btree %s position", RelationGetRelationName(scan->relation));
        scan->currentItemData = scan->currentMarkData;
        so->curHeapIptr = so->mrkHeapIptr;
    }
}

Datum
btdelete(Relation rel, ItemPointer tid) {
#ifdef NOTUSED
    /* adjust any active scans that will be affected by this deletion */
    _bt_adjscans(rel, tid);

    /* delete the data from the page */
    _bt_pagedel(rel, tid);
#endif
    return NULL;
}

Datum
btrecoverpage(Relation rel, BlockNumber block) {
    Buffer buffer;
    Page page;
    BTPageOpaque opaque;
    bool changed = false;
    BlockNumber  relsize = RelationGetNumberOfBlocks(rel);
    /*  notthing to check on meta */
    if (block == BTREE_METAPAGE) return LongGetDatum(InvalidBlockNumber);
        
    buffer = _bt_getbuf(rel, block, BT_WRITE);
    page = BufferGetPage(buffer);
    opaque = (BTPageOpaque) PageGetSpecialPointer(page);
    
    if ( PageIsNew(page) ) {
        LockBuffer(rel,buffer,BUFFER_LOCK_CRITICAL);
        _bt_pageinit(page,BufferGetPageSize(buffer));
        opaque->btpo_flags |= BTP_REAPED;
        _bt_wrtbuf(rel, buffer);
        return LongGetDatum(block);
    }
    
    if (P_ISSPLIT(opaque)) {
        _bt_relbuf(rel,buffer);
        return LongGetDatum(InvalidBlockNumber);
    }
    
    if ( P_ISREAPED(opaque) || 
            (
                !P_ISROOT(opaque) 
                && BTreeInvalidParent(opaque)
                && P_RIGHTMOST(opaque) 
                && P_LEFTMOST(opaque)
                && _bt_empty(page)
            )
    ) {
        if ( !P_ISREAPED(opaque) ) {
                opaque->btpo_flags |= BTP_REAPED;
                _bt_wrtbuf(rel,buffer);
        } else {
                _bt_relbuf(rel,buffer);
        }
        return LongGetDatum(block);
    }

    if ( !_bt_empty(page) ) {
        if (P_ISLEAF(opaque) && IsInitProcessingMode()) {
            HeapTuple heap = SearchSysCacheTuple(INDEXRELID, ObjectIdGetDatum(rel->rd_id), NULL, NULL, NULL);
            Oid heapid = SysCacheGetAttr(INDEXRELID, heap, Anum_pg_index_indrelid, NULL);
            Relation heaprel = RelationIdGetRelation(heapid, DEFAULTDBOID);
            OffsetNumber current;

            for (current = P_FIRSTDATAKEY(opaque); current <= PageGetMaxOffsetNumber(page); current = OffsetNumberNext(current)) {
                bool deleteit = false;

                BTItem item = (BTItem) PageGetItem(page, PageGetItemId(page, current));
                ItemPointer pointer = &item->bti_itup.t_tid;
                Buffer heapbuffer = ReadBuffer(heaprel, ItemPointerGetBlockNumber(pointer));

                if (!BufferIsValid(heapbuffer)) {
                    deleteit = true;
                } else {
                    LockBuffer(heaprel, heapbuffer, BUFFER_LOCK_SHARE);
                    Page heapPage = BufferGetPage(heapbuffer);

                    if (ItemPointerGetOffsetNumber(pointer) <= PageGetMaxOffsetNumber(heapPage)) {
                        ItemId heapitem = PageGetItemId(heapPage, ItemPointerGetOffsetNumber(pointer));
                        if (!ItemIdIsUsed(heapitem)) {
                            deleteit = true;
                        }
                    } else {
                        deleteit = true;
                    }
                    LockBuffer(heaprel, heapbuffer, BUFFER_LOCK_UNLOCK);
                    ReleaseBuffer(heaprel, heapbuffer);
                }

                if (deleteit) {
                    LockBuffer(rel,buffer,BUFFER_LOCK_CRITICAL);
                    PageIndexTupleDelete(page, current);
                    elog(NOTICE, "nbtree: Removing btree leaf page index tuple block: %d offset: %d", block, current);
                    current = OffsetNumberPrev(current);
                    changed = true;
                    LockBuffer(rel,buffer,BUFFER_LOCK_NOTCRITICAL);
                } else {
                    /*
                                    elog(NOTICE,"Validated btree index tuple block: %d offset: %d",block,current);
                     */
                }
            }
            RelationClose(heaprel);
        } else if ( !P_ISLEAF(opaque) && !IsReadOnlyProcessingMode() ) {
            OffsetNumber current;

            for (current = P_FIRSTDATAKEY(opaque); current <= PageGetMaxOffsetNumber(page); current = OffsetNumberNext(current)) {
                bool deleteit = false;

                BTItem item = (BTItem) PageGetItem(page, PageGetItemId(page, current));
                ItemPointer pointer = &item->bti_itup.t_tid;
                BlockNumber lblock = ItemPointerGetBlockNumber(pointer);
                Buffer leafbuffer = InvalidBuffer;
                
                if ( lblock != block && lblock < relsize ) leafbuffer = _bt_getbuf(rel, lblock, BT_READYWRITE);

                if (!BufferIsValid(leafbuffer)) {
                    deleteit = true;
                } else {
                    Page leafpage = BufferGetPage(leafbuffer);
                    BTPageOpaque lopaque = (BTPageOpaque) PageGetSpecialPointer(leafpage);
                    if (lopaque->btpo_parent == InvalidBlockNumber ) {
                        deleteit = true;
                        _bt_relbuf(rel, leafbuffer);
                    } else if ( _bt_empty(leafpage) ) {
                        if ( !P_RIGHTMOST(lopaque) ) {
                            deleteit = true;
                            lopaque->btpo_parent = InvalidBlockNumber;
                            _bt_wrtbuf(rel, leafbuffer);
                        } else {
                            _bt_relbuf(rel, leafbuffer);
                        }
                    } else {
                        _bt_relbuf(rel, leafbuffer);
                    }
                }

                if (deleteit) {
                    LockBuffer(rel,buffer,BUFFER_LOCK_CRITICAL);
                    PageIndexTupleDelete(page, current);
                    current = OffsetNumberPrev(current);
                    changed = true;                  
                    LockBuffer(rel,buffer,BUFFER_LOCK_NOTCRITICAL);
                } else {
                    /*
                                    elog(NOTICE,"Validated btree index tuple block: %d offset: %d",block,current);
                     */
                }
            }             
        }
    } else {

    }

    if (changed) {
        _bt_wrtbuf(rel, buffer);
    } else {
        _bt_relbuf(rel, buffer);
    }
    return LongGetDatum(_bt_check_pagelinks(rel, block));
}

/*
 * Bulk deletion of all index entries pointing to a set of heap tuples.
 * The set of target tuples is specified via a callback routine that tells
 * whether any given heap tuple (identified by ItemPointer) is being deleted.
 *
 * Result: a palloc'd struct containing statistical info for VACUUM displays.
 */
Datum
btbulkdelete(Relation rel, int delcount, ItemPointerData* tuple_deletes) {
    long tuples_removed;
    long num_index_tuples;
    IndexScanDesc scan;
    BTScanOpaque so;
    ItemPointer current;

    long used_pages = 0;

    tuples_removed = 0;
    num_index_tuples = 0;

    /*  array is seq scan so ItemPointers are in order */
    /* maybe just make sure  */
    qsort(tuple_deletes, delcount, sizeof (ItemPointerData), cmp_itemptr);

    /*
     * We use a standard IndexScanDesc scan object, but to speed up the
     * loop, we skip most of the wrapper layers of index_getnext and
     * instead call _bt_step directly.	This implies holding buffer lock
     * on a target page throughout the loop over the page's tuples.
     *
     * Whenever we step onto a new page, we have to trade in the read
     * lock acquired by _bt_first or _bt_step for an exclusive write lock
     * (fortunately, _bt_relbuf doesn't care which kind of lock it's
     * releasing when it comes time for _bt_step to release our lock).
     *
     * Note that we exclusive-lock every leaf page, or at least every one
     * containing data items.  It sounds attractive to only exclusive-lock
     * those containing items we need to delete, but unfortunately that
     * is not safe: we could then pass a stopped indexscan, which could
     * in rare cases lead to deleting the item it needs to find when it
     * resumes.  (See _bt_restscan --- this could only happen if an indexscan
     * stops on a deletable item and then a page split moves that item
     * into a page further to its right, which the indexscan will have no
     * pin on.)
     */
    scan = (IndexScanDesc) DatumGetPointer(btbeginscan(rel, 0, 0, (ScanKey) NULL));
    so = (BTScanOpaque) scan->opaque;
    current = &(scan->currentItemData);

    /* Use _bt_first to get started, then _bt_step to remaining tuples */
    if (_bt_first(scan, ForwardScanDirection)) {

        Page page = NULL;
        BTPageOpaque opaque = NULL;
        bool dirtied = false;
        Buffer buf = InvalidBuffer;
        Buffer lockedbuf = InvalidBuffer;
        long unlocked = 0;
        /* we have the buffer pinned and read-locked */
        buf = so->btso_curbuf;
        Assert(BufferIsValid(buf));

        while (BufferIsValid(buf)) {
            OffsetNumber offnum;
            BTItem btitem;
            IndexTuple itup;
            ItemPointer htup;
            ItemId itemid;

            /*
             * Make sure we have a super-exclusive write lock on this page.
             *
             * We assume that only concurrent insertions, not deletions,
             * can occur while we're not holding the page lock (the
             * caller should hold a suitable relation lock to ensure
             * this). Therefore, no items can escape being scanned because
             * of this temporary lock release.
             */
            if (!BufferIsValid(lockedbuf)) {
                /* need an extra reference to the buffer for writing 
                 * out dirty pages 
                 */
                IncrBufferRefCount(rel, buf);
                used_pages++;
                LockBuffer((rel), buf, BUFFER_LOCK_UNLOCK);
                LockBuffer((rel), buf, BUFFER_LOCK_REF_EXCLUSIVE);

                lockedbuf = buf;
                /*
                 * If the page was formerly rightmost but was split while we
                 * didn't hold the lock, and ip_posid is pointing to item
                 * 1, then ip_posid now points at the high key not a valid
                 * data item. In this case we need to step forward.
                 */
                /* current is the next index tuple */
                page = BufferGetPage(lockedbuf);
                opaque = (BTPageOpaque) PageGetSpecialPointer(page);
                if (ItemPointerGetOffsetNumber(current) < P_FIRSTDATAKEY(opaque)) {
                    ItemPointerSet(current, BufferGetBlockNumber(buf), P_FIRSTDATAKEY(opaque));
                }
            }

            offnum = ItemPointerGetOffsetNumber(current);
            itemid = PageGetItemId(page, offnum);
            btitem = (BTItem) PageGetItem(page, itemid);
            itup = &btitem->bti_itup;
            htup = &(itup->t_tid);
            /*  if the heap tuple item pointer is found in the list, delete it  */
            if (bsearch(htup, tuple_deletes, delcount, sizeof (ItemPointerData), cmp_itemptr) != NULL) {
                /* Okay to delete the item from the page */
                _bt_itemdel(rel, buf, current);
                dirtied = true;
                tuples_removed += 1;
                /*
                 * We now need to back up the scan one item, so that the next
                 * cycle will re-examine the same offnum on this page (which
                 * now holds the next item).
                 *
                 * For now, just hack the current-item index.  Will need to
                 * be smarter when deletion includes removal of empty
                 * index pages.
                 */
                ItemPointerSetUnchecked(current, BufferGetBlockNumber(lockedbuf), (offnum - 1));
            } else {
                num_index_tuples += 1;
            }

            if (tuples_removed == delcount) {
                /*  need to release buffer because scan may think 
                 *  the pointer is invalid due to the hack above 
                 *  possibly setting the offset number to 0, 
                 *
                 *  1 hr later...check that, modified btendscan instead,
                 *  makes more sense
                 */
                LockBuffer((rel), buf, BUFFER_LOCK_UNLOCK);
                buf = InvalidBuffer;
            } else {
                if (_bt_step(scan, ForwardScanDirection)) {
                    buf = so->btso_curbuf;
                } else {
                    buf = InvalidBuffer;
                }
            }

            if (buf != lockedbuf) {
                Assert(BufferIsValid(lockedbuf));
                if (dirtied) {
                    WriteBuffer(rel, lockedbuf);
                    dirtied = false;
                } else {
                    ReleaseBuffer(rel, lockedbuf);
                }
                unlocked++;
                lockedbuf = InvalidBuffer;
                page = NULL;

                if (IsShutdownProcessingMode())
                    elog(ERROR, "shutting down");
            }

        }
        Assert(used_pages == unlocked);
        Assert(!BufferIsValid(lockedbuf));
        Assert(!BufferIsValid(buf));

    }

    btendscan(scan);

    return LongGetDatum(tuples_removed);
}

/*
 * Restore scan position when btgettuple is called to continue a scan.
 *
 * This is nontrivial because concurrent insertions might have moved the
 * index tuple we stopped on.  We assume the tuple can only have moved to
 * the right from our stop point, because we kept a pin on the buffer,
 * and so no deletion can have occurred on that page.
 *
 * On entry, we have a pin but no read lock on the buffer that contained
 * the index tuple we stopped the scan on.  On exit, we have pin and read
 * lock on the buffer that now contains that index tuple, and the scandesc's
 * current position is updated to point at it.
 */
static void
_bt_restscan(IndexScanDesc scan) {
    Relation rel = scan->relation;
    BTScanOpaque so = (BTScanOpaque) scan->opaque;
    Buffer buf = so->btso_curbuf;
    Page page;
    ItemPointer current = &(scan->currentItemData);
    OffsetNumber offnum = ItemPointerGetOffsetNumber(current),
            maxoff;

    BTPageOpaque opaque;
    Buffer nextbuf;
    ItemPointerData target = so->curHeapIptr;
    BTItem item;
    BlockNumber blkno;

    /*
     * Reacquire read lock on the buffer.  (We should still have
     * a reference-count pin on it, so need not get that.)
     */
    LockBuffer((rel), buf, BT_READ);

    page = BufferGetPage(buf);
    maxoff = PageGetMaxOffsetNumber(page);
    opaque = (BTPageOpaque) PageGetSpecialPointer(page);

    /*
     * We use this as flag when first index tuple on page is deleted but
     * we do not move left (this would slowdown vacuum) - so we set
     * current->ip_posid before first index tuple on the current page
     * (_bt_step will move it right)...  XXX still needed?
     */
    if (!ItemPointerIsValid(&target)) {
        ItemPointerSet(current, ItemPointerGetBlockNumber(current), OffsetNumberPrev(P_FIRSTDATAKEY(opaque)));
        return;
    }

    /*
     * The item we were on may have moved right due to insertions. Find it
     * again.  We use the heap TID to identify the item uniquely.
     */
    for (;;) {
        /* Check for item on this page */
        for (;
                offnum <= maxoff;
                offnum = OffsetNumberNext(offnum)) {
            item = (BTItem) PageGetItem(page, PageGetItemId(page, offnum));
            if ((ItemPointerGetBlockNumber(&item->bti_itup.t_tid) == ItemPointerGetBlockNumber(&target)) &&
                    (ItemPointerGetOffsetNumber(&item->bti_itup.t_tid) == ItemPointerGetOffsetNumber(&target))) {
                /* Found it */
                ItemPointerSet(current, ItemPointerGetBlockNumber(current), offnum);
                return;
            }
        }

        /*
         * The item we're looking for moved right at least one page, so
         * move right.  We are careful here to pin and read-lock the next
         * page before releasing the current one.  This ensures that a
         * concurrent btbulkdelete scan cannot pass our position --- if it
         * did, it might be able to reach and delete our target item before
         * we can find it again.
         */
        if (P_RIGHTMOST(opaque))
            elog(FATAL, "_bt_restscan: my bits moved right off the end of the world!"
                "\n\tRecreate index %s.", RelationGetRelationName(rel));

        blkno = opaque->btpo_next;
        nextbuf = _bt_getbuf(rel, blkno, BT_READ);
        _bt_relbuf(rel, buf);
        so->btso_curbuf = buf = nextbuf;
        page = BufferGetPage(buf);
        maxoff = PageGetMaxOffsetNumber(page);
        opaque = (BTPageOpaque) PageGetSpecialPointer(page);
        offnum = P_FIRSTDATAKEY(opaque);
        ItemPointerSet(current, blkno, offnum);
    }
}

static BlockNumber
_bt_check_pagelinks(Relation rel, BlockNumber target) {
    Buffer tbuffer;
    Page tpage;
    BTPageOpaque topaque;
    BlockNumber reap = InvalidBlockNumber;

    tbuffer = _bt_getbuf(rel, target, BT_WRITE);
    tpage = BufferGetPage(tbuffer);
    topaque = (BTPageOpaque) PageGetSpecialPointer(tpage);
    
    if ( PageIsNew(tpage) ) {
        LockBuffer(rel,tbuffer,BUFFER_LOCK_CRITICAL);
        _bt_pageinit(tpage,BufferGetPageSize(tbuffer));
        ((BTPageOpaque)PageGetSpecialPointer(tpage))->btpo_flags |= BTP_REAPED;
        _bt_wrtbuf(rel, tbuffer);
        reap = target;
    } else if ( P_ISREAPED(topaque) ) {
        _bt_relbuf(rel,tbuffer);
        return target;
    } else if ( P_ISROOT(topaque) ) {
	Buffer		metabuf,rootbuf;
	Page		metapg,rootpg;
	BTMetaPageData *metad;
        BlockNumber     root;
        
        bool reroot = (PageGetMaxOffsetNumber(tpage) == 1);
        
        _bt_relbuf(rel, tbuffer);

        if ( reroot ) {
            metabuf = _bt_getbuf(rel, BTREE_METAPAGE, BT_WRITE);
            metapg = BufferGetPage(metabuf);
            metad = BTPageGetMeta(metapg);

            Assert(((BTPageOpaque)PageGetSpecialPointer(metapg))->btpo_flags & BTP_META);
            Assert(metad->btm_magic == BTREE_MAGIC);
            Assert(metad->btm_version == BTREE_VERSION);

            root = metad->btm_root;
            
            rootbuf = _bt_getbuf(rel, root, BT_WRITE);
            rootpg = BufferGetPage(rootbuf);
            while (PageGetMaxOffsetNumber(rootpg) == 1) {
                BTItem item = (BTItem) PageGetItem(rootpg, PageGetItemId(rootpg, 1));
                ItemPointer pointer = &item->bti_itup.t_tid;
                BlockNumber lblock = ItemPointerGetBlockNumber(pointer);
                Buffer leafbuffer = InvalidBuffer;    
                Page leafpage = NULL;

                leafbuffer = _bt_getbuf(rel,lblock,BT_READYWRITE);
                leafpage = BufferGetPage(leafbuffer);
                Assert(((BTPageOpaque)PageGetSpecialPointer(leafpage))->btpo_prev == 0);
                Assert(((BTPageOpaque)PageGetSpecialPointer(leafpage))->btpo_next == 0);
                ((BTPageOpaque)PageGetSpecialPointer(leafpage))->btpo_flags |= BTP_ROOT;
                ((BTPageOpaque)PageGetSpecialPointer(leafpage))->btpo_parent |= BTREE_METAPAGE;

                LockBuffer(rel,metabuf,BUFFER_LOCK_CRITICAL);
                metad->btm_root = lblock;

                LockBuffer(rel,rootbuf,BUFFER_LOCK_CRITICAL);
                ((BTPageOpaque)PageGetSpecialPointer(rootpg))->btpo_flags |= BTP_REAPED;

                _bt_wrtbuf(rel,rootbuf);
                rootbuf = leafbuffer;
                rootpg = leafpage;
                reap = root;
            }
            
            _bt_wrtbuf(rel,rootbuf);
            _bt_wrtbuf(rel,metabuf);
            reap = root;
        } 
    } else if ( P_LEFTMOST(topaque) && BTreeInvalidParent(topaque) ) {
        reap = target;
        /*  if this is rightmost too, it should never get here. */
        Assert(!P_RIGHTMOST(topaque));
        Buffer nbuffer = _bt_getbuf(rel,topaque->btpo_next,BT_READYWRITE);
        Page npage = BufferGetPage(nbuffer);
        BTPageOpaque nopaque = (BTPageOpaque) PageGetSpecialPointer(npage);
        nopaque->btpo_prev = 0;
        LockBuffer(rel,nbuffer,BUFFER_LOCK_NOTCRITICAL);
        LockBuffer(rel,tbuffer,BUFFER_LOCK_CRITICAL);
        ((BTPageOpaque)PageGetSpecialPointer(tpage))->btpo_flags |= BTP_REAPED;
        _bt_wrtbuf(rel, nbuffer);
        _bt_wrtbuf(rel, tbuffer);
    } else if ( P_RIGHTMOST(topaque) ) {
        _bt_relbuf(rel, tbuffer);
    } else if ( PageGetPageSize(tpage) != BufferGetPageSize(tbuffer) ) {
        LockBuffer(rel,tbuffer,BUFFER_LOCK_CRITICAL);
        _bt_pageinit(tpage,BufferGetPageSize(tbuffer));
        ((BTPageOpaque)PageGetSpecialPointer(tbuffer))->btpo_flags |= BTP_REAPED;
        _bt_wrtbuf(rel, tbuffer);
        reap = target;
    } else {
        Buffer nbuffer = _bt_getbuf(rel,topaque->btpo_next,BT_READYWRITE);
        Page npage = BufferGetPage(nbuffer);
        BTPageOpaque nopaque = (BTPageOpaque) PageGetSpecialPointer(npage);
        if ( BTreeInvalidParent(nopaque) ) {
            if (nopaque->btpo_prev == topaque->btpo_prev && P_ISSPLIT(nopaque)) {
                nopaque->btpo_parent = topaque->btpo_parent;
                nopaque->btpo_flags &= ~(BTP_SPLIT);
                LockBuffer(rel,tbuffer,BUFFER_LOCK_CRITICAL);
                memmove(tpage, npage, PageGetPageSize(npage));
                _bt_pageinit(npage,PageGetPageSize(npage));
                ((BTPageOpaque)PageGetSpecialPointer(npage))->btpo_flags |= BTP_REAPED;
                _bt_wrtbuf(rel, nbuffer);
                _bt_wrtbuf(rel, tbuffer);
            } else {
                Assert(_bt_empty(npage));
                reap = BufferGetBlockNumber(nbuffer);
                ((BTPageOpaque)PageGetSpecialPointer(npage))->btpo_flags |= BTP_REAPED;
                LockBuffer(rel,nbuffer,BUFFER_LOCK_NOTCRITICAL);
                if ( !P_RIGHTMOST(nopaque) ) {
                    Buffer sbuffer = _bt_getbuf(rel, nopaque->btpo_next, BT_READYWRITE);
                    Page spage = BufferGetPage(sbuffer);
                    BTPageOpaque sopaque = (BTPageOpaque) PageGetSpecialPointer(spage); 
                    sopaque->btpo_prev = target;
                    _bt_wrtbuf(rel, sbuffer);
                }
                LockBuffer(rel,tbuffer,BUFFER_LOCK_CRITICAL);
                topaque->btpo_next = nopaque->btpo_next;
                _bt_wrtbuf(rel, nbuffer);
                _bt_wrtbuf(rel, tbuffer);
            }
        } else {
            if ( nopaque->btpo_prev != target ) {
                nopaque->btpo_prev = target;
                _bt_wrtbuf(rel, nbuffer);
            } else {
                _bt_relbuf(rel, nbuffer);
            }
            _bt_relbuf(rel, tbuffer);
        }
    }

    return reap;
}

static int
cmp_itemptr(const void *left, const void *right) {
    BlockNumber lblk,
            rblk;
    OffsetNumber loff,
            roff;

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

