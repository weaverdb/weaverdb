/*-------------------------------------------------------------------------
 *
 * heapam.c
 *	  heap access method code
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  $Header: /cvs/weaver/mtpgsql/src/backend/access/heap/heapam.c,v 1.2 2007/05/23 15:39:24 synmscott Exp $
 *
 *
 * INTERFACE ROUTINES
 *		heapgettup		- fetch next heap tuple from a scan
 *		heap_open		- open a heap relation by relationId
 *		heap_openr		- open a heap relation by name
 *		heap_close		- close a heap relation
 *		heap_beginscan	- begin relation scan
 *		heap_rescan		- restart a relation scan
 *		heap_endscan	- end relation scan
 *		heap_getnext	- retrieve next tuple in scan
 *		heap_fetch		- retrive tuple with tid
 *		heap_insert		- insert tuple into a relation
 *		heap_delete		- delete a tuple from a relation
 *		heap_update - replace a tuple in a relation with another tuple
 *		heap_markpos	- mark scan position
 *		heap_restrpos	- restore position to marked location
 *
 * NOTES
 *	  This file contains the heap_ routines which implement
 *	  the POSTGRES heap access method used for all POSTGRES
 *	  relations.
 *
 * OLD COMMENTS
 *		struct relscan hints:  (struct should be made AM independent?)
 *
 *		rs_ctid is the tid of the last tuple returned by getnext.
 *		rs_ptid and rs_ntid are the tids of the previous and next tuples
 *		returned by getnext, respectively.	NULL indicates an end of
 *		scan (either direction); NON indicates an unknow value.
 *
 *		possible combinations:
 *		rs_p	rs_c	rs_n			interpretation
 *		NULL	NULL	NULL			empty scan
 *		NULL	NULL	NON				at begining of scan
 *		NULL	NULL	t1				at begining of scan (with cached tid)
 *		NON		NULL	NULL			at end of scan
 *		t1		NULL	NULL			at end of scan (with cached tid)
 *		NULL	t1		NULL			just returned only tuple
 *		NULL	t1		NON				just returned first tuple
 *		NULL	t1		t2				returned first tuple (with cached tid)
 *		NON		t1		NULL			just returned last tuple
 *		t2		t1		NULL			returned last tuple (with cached tid)
 *		t1		t2		NON				in the middle of a forward scan
 *		NON		t2		t1				in the middle of a reverse scan
 *		ti		tj		tk				in the middle of a scan (w cached tid)
 *
 *		Here NULL is ...tup == NULL && ...buf == InvalidBuffer,
 *		and NON is ...tup == NULL && ...buf == UnknownBuffer.
 *
 *		Currently, the NONTID values are not cached with their actual
 *		values by getnext.	Values may be cached by markpos since it stores
 *		all three tids.
 *
 *		NOTE:  the calls to elog() must stop.  Should decide on an interface
 *		between the general and specific AM calls.
 *
 *		XXX probably do not need a free tuple routine for heaps.
 *		Huh?  Free tuple is not necessary for tuples returned by scans, but
 *		is necessary for tuples which are returned by
 *		RelationGetTupleByItemPointer. -hirohama
 *
 *-------------------------------------------------------------------------
 */
#include <signal.h>

#include "postgres.h"
#include "env/env.h"

#include "env/dbwriter.h"
#include "access/heapam.h"
#include "access/hio.h"
#include "env/freespace.h"
#ifdef NOTUSED
#include "access/valid.h"
#endif
#include "access/tuptoaster.h"
#include "catalog/catalog.h"
#include "miscadmin.h"
#include "storage/smgr.h"
#include "utils/builtins.h"
#include "utils/inval.h"
#include "utils/relcache.h"
#include "access/blobstorage.h"

static Buffer
NextGenGetTup(Relation relation,
		   HeapTuple tuple,
		   Buffer buffer,
		   Snapshot snapshot,
		   int nkeys,
		   ScanKey key);
/*
static OffsetNumber BufferGetMaxOffset(Relation rel, Buffer buffer);
*/
/* ----------------------------------------------------------------
 *						 heap support routines
 * ----------------------------------------------------------------
 */

/* ----------------
 *		initscan - scan code common to heap_beginscan and heap_rescan
 * ----------------
 */
static void
initscan(HeapScanDesc scan,
		 Relation relation,
		 unsigned nkeys,
		 ScanKey key)
{
	/* ----------------
	 *	Make sure we have up-to-date idea of number of blocks in relation.
	 *	It is sufficient to do this once at scan start, since any tuples
	 *	added while the scan is in progress will be invisible to my
	 *	transaction anyway...
	 * ----------------
	 */
	relation->rd_nblocks = RelationGetNumberOfBlocks(relation);

	if (relation->rd_nblocks == 0)
	{
		/* ----------------
		 *	relation is empty
		 * ----------------
		 */
		scan->rs_ctup.t_datamcxt = NULL;
		scan->rs_ctup.t_datasrc = NULL;
		scan->rs_ctup.t_info = 0;
		scan->rs_ctup.t_data = NULL;
		scan->rs_cbuf = InvalidBuffer;
	} else {
			/* ----------------
		 *	forward scan
		 * ----------------
		 */
		scan->rs_ctup.t_datamcxt = NULL;
		scan->rs_ctup.t_datasrc = NULL;
		scan->rs_ctup.t_info = 0;
		scan->rs_ctup.t_data = NULL;
		scan->rs_cbuf = InvalidBuffer;
	}							/* invalid too */

	/* we don't have a marked position... */
	ItemPointerSetInvalid(&(scan->rs_mctid));
	ItemPointerSetInvalid(&(scan->rs_mcd));

	/* ----------------
	 *	copy the scan key, if appropriate
	 * ----------------
	 */
	if (key != NULL)
		memmove(scan->rs_key, key, nkeys * sizeof(ScanKeyData));
}

/* ----------------
 *		unpinscan - code common to heap_rescan and heap_endscan
 * ----------------
 */
static void
unpinscan(HeapScanDesc scan)
{

	/* ------------------------------------
	 *	Scan will pin buffer once for each non-NULL tuple pointer
	 *	(ptup, ctup, ntup), so they have to be unpinned multiple
	 *	times.
	 * ------------------------------------
	 */
	if (BufferIsValid(scan->rs_cbuf))
		ReleaseBuffer(scan->rs_rd, scan->rs_cbuf);

}

/* ------------------------------------------
 *		nextpage
 *
 *		figure out the next page to scan after the current page
 *		taking into account of possible adjustment of degrees of
 *		parallelism
 * ------------------------------------------
 */
static BlockNumber
nextpage(int page)
{
	return page + 1;
}

/* ----------------------------------------------------------------
 *					 heap access method interface
 * ----------------------------------------------------------------
 */
/* ----------------
 *		heap_open - open a heap relation by relationId
 *
 *		If lockmode is "NoLock", no lock is obtained on the relation,
 *		and the caller must check for a NULL return value indicating
 *		that no such relation exists.
 *		Otherwise, an error is raised if the relation does not exist,
 *		and the specified kind of lock is obtained on the relation.
 * ----------------
 */
Relation
heap_open(Oid relationId, LOCKMODE lockmode)
{
	Relation	r;

	Assert(lockmode >= NoLock && lockmode < MAX_LOCKMODES);

	/* ----------------
	 *	increment access statistics
	 * ----------------
	 */
#ifdef USESTATS
	IncrHeapAccessStat(local_open);
	IncrHeapAccessStat(global_open);
#endif

	/* The relcache does all the real work... */
	r = RelationIdGetRelation(relationId,DEFAULTDBOID);

	if (!RelationIsValid(r))
		elog(ERROR, "Relation %u does not exist", relationId);

	/* Under no circumstances will we return an index as a relation. */
	if (r->rd_rel->relkind == RELKIND_INDEX)
		elog(ERROR, "%s is an index relation", RelationGetRelationName(r));

        r->rd_nblocks = RelationGetNumberOfBlocks(r);

	if (lockmode == NoLock)
		return r;				/* caller must check RelationIsValid! */

	LockRelation(r, lockmode);

	return r;
}

/* ----------------
 *		heap_openr - open a heap relation by name
 *
 *		If lockmode is "NoLock", no lock is obtained on the relation,
 *		and the caller must check for a NULL return value indicating
 *		that no such relation exists.
 *		Otherwise, an error is raised if the relation does not exist,
 *		and the specified kind of lock is obtained on the relation.
 * ----------------
 */
Relation
heap_openr(const char *relationName, LOCKMODE lockmode)
{
	Relation	r;

	Assert(lockmode >= NoLock && lockmode < MAX_LOCKMODES);

	/* ----------------
	 *	increment access statistics
	 * ----------------
	 */
#ifdef USESTATS
	IncrHeapAccessStat(local_openr);
	IncrHeapAccessStat(global_openr);
#endif

	/* The relcache does all the real work... */
	r = RelationNameGetRelation(relationName,DEFAULTDBOID);

        if (!RelationIsValid(r)) {
		elog(ERROR, "Relation '%s' does not exist", relationName);
	}
        
        /* Under no circumstances will we return an index as a relation. */
	if (r->rd_rel->relkind == RELKIND_INDEX)
		elog(ERROR, "%s is an index relation", RelationGetRelationName(r));

        r->rd_nblocks = RelationGetNumberOfBlocks(r);

        if (lockmode == NoLock)
		return r;				/* caller must check RelationIsValid! */

	LockRelation(r, lockmode);

	return r;
}

/* ----------------
 *		heap_close - close a heap relation
 *
 *		If lockmode is not "NoLock", we first release the specified lock.
 *		Note that it is often sensible to hold a lock beyond heap_close;
 *		in that case, the lock is released automatically at xact end.
 * ----------------
 */
void
heap_close(Relation relation, LOCKMODE lockmode)
{
	Assert(lockmode >= NoLock && lockmode < MAX_LOCKMODES);

	/* ----------------
	 *	increment access statistics
	 * ----------------
	 */
#ifdef USESTATS
	IncrHeapAccessStat(local_close);
	IncrHeapAccessStat(global_close);
#endif

	if (lockmode != NoLock)
		UnlockRelation(relation, lockmode);

	/* The relcache does the real work... */
	RelationClose(relation);
}


/* ----------------
 *		heap_beginscan	- begin relation scan
 * ----------------
 */
HeapScanDesc
heap_beginscan(Relation relation,
			   Snapshot snapshot,
			   unsigned nkeys,
			   ScanKey key)
{
	HeapScanDesc scan;

	/* ----------------
	 *	increment access statistics
	 * ----------------
	 */
#ifdef USESTATS
	IncrHeapAccessStat(local_beginscan);
	IncrHeapAccessStat(global_beginscan);
#endif

	/* ----------------
	 *	sanity checks
	 * ----------------
	 */
	if (!RelationIsValid(relation))
		elog(ERROR, "heap_beginscan: !RelationIsValid(relation)");

	/* ----------------
	 *	increment relation ref count while scanning relation
	 * ----------------
	 */
	RelationIncrementReferenceCount(relation);

	/* ----------------
	 *	Acquire AccessShareLock for the duration of the scan
	 *
	 *	Note: we could get an SI inval message here and consequently have
	 *	to rebuild the relcache entry.	The refcount increment above
	 *	ensures that we will rebuild it and not just flush it...
	 * ----------------
	 */
	LockRelation(relation, AccessShareLock);

	/* XXX someday assert SelfTimeQual if relkind == RELKIND_UNCATALOGED */
	if (relation->rd_rel->relkind == RELKIND_UNCATALOGED)
		snapshot = SnapshotSelf;

	/* ----------------
	 *	allocate and initialize scan descriptor
	 * ----------------
	 */
	scan = (HeapScanDesc) palloc(sizeof(HeapScanDescData));

	scan->rs_rd = relation;

	scan->rs_snapshot = snapshot;
	scan->rs_nkeys = (short) nkeys;

	if (nkeys)

		/*
		 * we do this here instead of in initscan() because heap_rescan
		 * also calls initscan() and we don't want to allocate memory
		 * again
		 */
		scan->rs_key = (ScanKey) palloc(sizeof(ScanKeyData) * nkeys);
	else
		scan->rs_key = NULL;

	initscan(scan, relation,nkeys, key);

	return scan;
}

/* ----------------
 *		heap_rescan		- restart a relation scan
 * ----------------
 */
void
heap_rescan(HeapScanDesc scan,
			ScanKey key)
{
	/* ----------------
	 *	increment access statistics
	 * ----------------
	 */
#ifdef USESTATS
	IncrHeapAccessStat(local_rescan);
	IncrHeapAccessStat(global_rescan);
#endif

	/* Note: set relation level read lock is still set */

	/* ----------------
	 *	unpin scan buffers
	 * ----------------
	 */
	unpinscan(scan);

	/* ----------------
	 *	reinitialize scan descriptor
	 * ----------------
	 */
	initscan(scan, scan->rs_rd, scan->rs_nkeys, key);
}

/* ----------------
 *		heap_endscan	- end relation scan
 *
 *		See how to integrate with index scans.
 *		Check handling if reldesc caching.
 * ----------------
 */
void
heap_endscan(HeapScanDesc scan)
{
	/* ----------------
	 *	increment access statistics
	 * ----------------
	 */
#ifdef USESTATS
	IncrHeapAccessStat(local_endscan);
	IncrHeapAccessStat(global_endscan);
#endif

	/* Note: no locking manipulations needed */

	/* ----------------
	 *	unpin scan buffers
	 * ----------------
	 */
	unpinscan(scan);

	/* ----------------
	 *	Release AccessShareLock acquired by heap_beginscan()
	 * ----------------
	 */
	UnlockRelation(scan->rs_rd, AccessShareLock);

	/* ----------------
	 *	decrement relation reference count and free scan descriptor storage
	 * ----------------
	 */
	RelationDecrementReferenceCount(scan->rs_rd);

	if (scan->rs_key)
		pfree(scan->rs_key);

	pfree(scan);
}

/* ----------------
 *		heap_getnext	- retrieve next tuple in scan
 *
 *		Fix to work with index relations.
 *		We don't return the buffer anymore, but you can get it from the
 *		returned HeapTuple.
 * ----------------
 */

#ifdef HEAPDEBUGALL
#define HEAPDEBUG_1 \
elog(DEBUG, "heap_getnext([%s,nkeys=%d],backw=%d) called", \
	 RelationGetRelationName(scan->rs_rd), scan->rs_nkeys, backw)

#define HEAPDEBUG_2 \
	 elog(DEBUG, "heap_getnext called with backw (no tracing yet)")

#define HEAPDEBUG_3 \
	 elog(DEBUG, "heap_getnext returns NULL at end")

#define HEAPDEBUG_4 \
	 elog(DEBUG, "heap_getnext valid buffer UNPIN'd")

#define HEAPDEBUG_5 \
	 elog(DEBUG, "heap_getnext next tuple was cached")

#define HEAPDEBUG_6 \
	 elog(DEBUG, "heap_getnext returning EOS")

#define HEAPDEBUG_7 \
	 elog(DEBUG, "heap_getnext returning tuple");
#else
#define HEAPDEBUG_1
#define HEAPDEBUG_2
#define HEAPDEBUG_3
#define HEAPDEBUG_4
#define HEAPDEBUG_5
#define HEAPDEBUG_6
#define HEAPDEBUG_7
#endif	 /* !defined(HEAPDEBUGALL) */


HeapTuple
heap_getnext(HeapScanDesc scandesc)
{
	HeapScanDesc scan = scandesc;

	/* ----------------
	 *	increment access statistics
	 * ----------------
	 */
#ifdef USESTATS
	IncrHeapAccessStat(local_getnext);
	IncrHeapAccessStat(global_getnext);
#endif

	/* Note: no locking manipulations needed */

	/* ----------------
	 *	argument checks
	 * ----------------
	 */
	if (scan == NULL)
		elog(ERROR, "heap_getnext: NULL relscan");

	/* ----------------
	 *	initialize return buffer to InvalidBuffer
	 * ----------------
	 */

        scan->rs_cbuf = NextGenGetTup(scan->rs_rd,
                           &(scan->rs_ctup),
                           scan->rs_cbuf,
                           scan->rs_snapshot,
                           scan->rs_nkeys,
                           scan->rs_key);
        
        if (scan->rs_ctup.t_data == NULL) {
            Assert(!BufferIsValid(scan->rs_cbuf));
        }
	/* ----------------
	 *	if we get here it means we have a new current scan tuple, so
	 *	point to the proper return buffer and return the tuple.
	 * ----------------
	 */
	return ((scan->rs_ctup.t_data == NULL) ? NULL : &(scan->rs_ctup));
}

/* ----------------
 *		heap_fetch		- retrive tuple with tid
 *
 *		Currently ignores LP_IVALID during processing!
 *
 *		Because this is not part of a scan, there is no way to
 *		automatically lock/unlock the shared buffers.
 *		For this reason, we require that the user retrieve the buffer
 *		value, and they are required to BufferRelease() it when they
 *		are done.  If they want to make a copy of it before releasing it,
 *		they can call heap_copytuple().
 * ----------------
 */
bool
heap_fetch(Relation relation,
		   Snapshot snapshot,
		   HeapTuple tuple,
		   Buffer *userbuf)
{
	Buffer		buffer;
        bool            valid = FALSE;

        tuple->t_info = 0;
        buffer = RelationGetHeapTuple(relation,tuple);

        if ( BufferIsValid(buffer) ) {
            LockHeapTuple(relation,buffer,tuple,TUPLE_LOCK_READ);
            valid = HeapTupleSatisfies(relation, buffer, tuple, snapshot, 0, (ScanKey) NULL);
            LockHeapTuple(relation,buffer,tuple,TUPLE_LOCK_UNLOCK);
            
            if ( valid ) {
                *userbuf = buffer;    
            } else {
		tuple->t_data = NULL;
		tuple->t_len = 0;
		ReleaseBuffer(relation, buffer);
		*userbuf = InvalidBuffer;
                return false;
            }
        }

        return valid;
}

/* ----------------
 *	heap_get_latest_tid -  get the latest tid of a specified tuple
 *
 * ----------------
 */
ItemPointerData
heap_get_latest_tid(Relation relation,
                            Snapshot snapshot,
                            ItemPointer tid)
{
	Buffer		buffer;
	HeapTupleData tp;
        ItemPointerData     checkid;
	bool		valid,invalidBlock,
				linkend;

 	tp.t_datamcxt = NULL;
	tp.t_datasrc = NULL;
        tp.t_info = 0;
        tp.t_self = *tid;
        
        buffer = RelationGetHeapTuple(relation, &tp);
        ItemPointerSetInvalid(&checkid);

        if (BufferIsValid(buffer)) {
            LockHeapTuple(relation,buffer,&tp,TUPLE_LOCK_READ);
            ItemPointerCopy(&tp.t_data->t_ctid,&checkid);
            valid = HeapTupleSatisfies(relation, buffer, &tp, 
					   snapshot, 0, (ScanKey) NULL);

            linkend = true;
            if ((tp.t_data->t_infomask & HEAP_XMAX_COMMITTED) &&
                    !ItemPointerEquals(&tp.t_self, &checkid)) {
                linkend = false;
            }
            LockHeapTuple(relation,buffer,&tp,TUPLE_LOCK_UNLOCK);
            ReleaseBuffer(relation, buffer);

            if (!valid)
            {
                if (linkend) {
                    ItemPointerSetInvalid(&checkid);
                    return checkid;
                }
                return heap_get_latest_tid(relation, snapshot, &checkid);
            }
        }

	return checkid;
}

/* ----------------
 *		heap_insert		- insert tuple
 *
 *		The assignment of t_min (and thus the others) should be
 *		removed eventually.
 *
 *		Currently places the tuple onto the last page.	If there is no room,
 *		it is placed on new pages.	(Heap relations)
 *		Note that concurrent inserts during a scan will probably have
 *		unexpected results, though this will be fixed eventually.
 *
 *		Fix to work with indexes.
 * ----------------
 */
Oid
heap_insert(Relation relation, HeapTuple tup)
{
	TransactionId   xid;
	
	/* ----------------
	 *	increment access statistics
	 * ----------------
	 */
#ifdef USESTATS
	IncrHeapAccessStat(local_insert);
	IncrHeapAccessStat(global_insert);
#endif

	/* ----------------
	 *	If the object id of this tuple has already been assigned, trust
	 *	the caller.  There are a couple of ways this can happen.  At initial
	 *	db creation, the backend program sets oids for tuples.	When we
	 *	define an index, we set the oid.  Finally, in the future, we may
	 *	allow users to set their own object ids in order to support a
	 *	persistent object store (objects need to contain pointers to one
	 *	another).
	 * ----------------
	 */
/*  we are going to default that if the table is not a system table 
	we are going to use a generic oid   MKS  11.29.2001  */

	if (!OidIsValid(tup->t_data->t_oid) )
	{
		if ( IsSystemRelationName(NameStr(relation->rd_rel->relname)) ) {
			tup->t_data->t_oid = newoid();
		} else {
			tup->t_data->t_oid = GetGenId();
		}
		GetEnv()->LastOidProcessed = tup->t_data->t_oid;
	}

	xid = GetCurrentTransactionId();
	tup->t_data->t_xmin = xid;
	tup->t_data->progress.cmd.t_cmin = GetCurrentCommandId();	
	tup->t_data->progress.cmd.t_cmax = FirstCommandId;
	tup->t_data->t_xmax = InvalidTransactionId;
	
	tup->t_data->t_infomask &= ~(HEAP_XACT_MASK);
	tup->t_data->t_infomask |= HEAP_XMAX_INVALID;

	RelationPutHeapTupleAtFreespace(relation, tup, 0);

	if (IsSystemRelationName(RelationGetRelationName(relation)))
		RelationMark4RollbackHeapTuple(relation, tup);

	return tup->t_data->t_oid;
}

/*
 *	heap_delete		- delete a tuple
 */
int
heap_delete(Relation relation, ItemPointer tid, ItemPointer ctid, Snapshot snapshot)
{
	HeapTupleData       tp;
	Buffer              buffer;
	int                 result = -1;
	TransactionId       xid;
        int                updateable;
        
	Assert(ItemPointerIsValid(tid));
        tp.t_datamcxt = NULL;
        tp.t_datasrc = NULL;
        tp.t_info = 0;
        tp.t_self = *tid;
            
        updateable = LockHeapTupleForUpdate(relation, &buffer, &tp, snapshot);

	if (updateable != HeapTupleMayBeUpdated)
	{
		if (ctid != NULL) {
			*ctid = tp.t_data->t_ctid;
                }
                UnlockHeapTuple(relation,buffer,&tp);
		ReleaseBuffer(relation, buffer);
		return updateable;
	}

	xid = GetCurrentTransactionId();

	/* store transaction information of xact deleting the tuple */
	if ( tp.t_data->t_infomask & HEAP_MOVED_IN ) {
		tp.t_data->t_xmin = tp.t_data->progress.t_vtran;
		tp.t_data->progress.cmd.t_cmin = FirstCommandId;
	}
	tp.t_data->t_xmax = xid;
	tp.t_data->progress.cmd.t_cmax = GetCurrentCommandId();
	tp.t_data->t_infomask &= 
                ~(HEAP_XMAX_COMMITTED | HEAP_XMAX_INVALID | HEAP_MARKED_FOR_UPDATE | HEAP_MOVED_IN);
	tp.t_data->t_ctid = tp.t_self;
        UnlockHeapTuple(relation,buffer,&tp);
    
	if ( HeapTupleHasBlob(&tp) ) {
		delete_tuple_blob(relation,&tp);
	}
	/* invalidate caches */
	RelationInvalidateHeapTuple(relation, &tp);

	WriteBuffer(relation, buffer);

	return HeapTupleMayBeUpdated;
}

/*
 *	heap_update - replace a tuple
 */
int
heap_update(Relation relation, ItemPointer otid, HeapTuple newtup,
			ItemPointer ctid, Snapshot snapshot)
{
	HeapTupleData       oldtup;
	Buffer              buffer;
	int                 result = -1;
	TransactionId       xid;
        Size                pageSize;
        int                updateable;

	Assert(ItemPointerIsValid(otid));
        oldtup.t_self = *otid;
        oldtup.t_info = 0;

        updateable = LockHeapTupleForUpdate(relation, &buffer, &oldtup, snapshot);
        
        if (updateable != HeapTupleMayBeUpdated) {
            Assert(updateable == HeapTupleSelfUpdated || updateable == HeapTupleUpdated);
            if (ctid != NULL) {
                    *ctid = oldtup.t_data->t_ctid;
            }
            UnlockHeapTuple(relation,buffer,&oldtup);
            ReleaseBuffer(relation, buffer);
            return result;
	} 
        
	/* XXX order problems if not atomic assignment ??? */
	newtup->t_data->t_oid = oldtup.t_data->t_oid;
	xid = GetCurrentTransactionId();

	newtup->t_data->t_xmin = xid;
	newtup->t_data->progress.cmd.t_cmin = GetCurrentCommandId();	
	newtup->t_data->progress.cmd.t_cmax = FirstCommandId;
	newtup->t_data->t_xmax = InvalidTransactionId;
	
	newtup->t_data->t_infomask &= ~(HEAP_XACT_MASK);
	newtup->t_data->t_infomask |= (HEAP_XMAX_INVALID | HEAP_UPDATED);

        if ( oldtup.t_data->t_infomask & HEAP_MOVED_IN ) {
                oldtup.t_data->t_xmin = oldtup.t_data->progress.t_vtran;
                oldtup.t_data->progress.cmd.t_cmin = FirstCommandId;
        }
        oldtup.t_data->t_xmax = xid;
        oldtup.t_data->progress.cmd.t_cmax = GetCurrentCommandId();
        oldtup.t_data->t_infomask &= ~(HEAP_XMAX_COMMITTED |
                          HEAP_XMAX_INVALID | HEAP_MARKED_FOR_UPDATE | HEAP_MOVED_IN );
        oldtup.t_data->t_ctid = newtup->t_self;

        /* test to see if putting at freespace is more
        efficient by commenting out first option
          MKS  8.9.2002  */
        
/* insert new item */
        pageSize = PageGetFreeSpace(BufferGetPage(buffer));
	if (  !BufferHasError(buffer) && (unsigned) MAXALIGN(newtup->t_len) <= pageSize && !(newtup->t_info & TUPLE_HASBUFFERED) ) {
        	RelationPutHeapTuple(relation, buffer, newtup);  
    	} else {
		/*
		 * New item won't fit on same page as old item, have to look for a
		 * new place to put it. Note that we have to unlock current buffer
		 * context - not good but RelationPutHeapTupleAtEnd uses extend
		 * lock.
		 */
                LockHeapTuple(relation,buffer,&oldtup,TUPLE_LOCK_UNLOCK);
		RelationPutHeapTupleAtFreespace(relation, newtup, 0);
    		LockHeapTuple(relation,buffer,&oldtup,TUPLE_LOCK_WRITE);
	}
        oldtup.t_data->t_ctid = newtup->t_self;
    	LockHeapTuple(relation,buffer,&oldtup,TUPLE_LOCK_UNLOCK);
	/* mark for rollback caches */
	RelationMark4RollbackHeapTuple(relation, newtup);
    	
	if ( HeapTupleHasBlob(&oldtup) ) {
		delete_tuple_blob(relation,&oldtup);
	}
	/* invalidate caches */
	RelationInvalidateHeapTuple(relation, &oldtup);

	WriteBuffer(relation, buffer);

	return HeapTupleMayBeUpdated;
}

/*
 *	heap_mark4update		- mark a tuple for update
 */
int
heap_mark4update(Relation relation, Buffer *buffer, HeapTuple tuple,  Snapshot snapshot)
{
	ItemPointer tid = &(tuple->t_self);
	int			result;
	TransactionId		xid;

        result = LockHeapTupleForUpdate(relation, buffer, tuple, snapshot);

	if ( result != HeapTupleMayBeUpdated )
	{
		Assert(result == HeapTupleSelfUpdated || result == HeapTupleUpdated);
		tuple->t_self = tuple->t_data->t_ctid;
                UnlockHeapTuple(relation,*buffer,tuple);
		return result;
	}

	/* store transaction information of xact marking the tuple */
	xid = GetCurrentTransactionId();
	tuple->t_data->t_xmax = xid;
	tuple->t_data->progress.cmd.t_cmax = GetCurrentCommandId();
	tuple->t_data->t_infomask &= ~(HEAP_XMAX_COMMITTED | HEAP_XMAX_INVALID);
	tuple->t_data->t_infomask |= HEAP_MARKED_FOR_UPDATE;
        UnlockHeapTuple(relation,*buffer,tuple);

	WriteNoReleaseBuffer(relation, *buffer);

	return HeapTupleMayBeUpdated;
}

/* ----------------
 *		heap_markpos	- mark scan position
 *
 *		Note:
 *				Should only one mark be maintained per scan at one time.
 *		Check if this can be done generally--say calls to get the
 *		next/previous tuple and NEVER pass struct scandesc to the
 *		user AM's.  Now, the mark is sent to the executor for safekeeping.
 *		Probably can store this info into a GENERAL scan structure.
 *
 *		May be best to change this call to store the marked position
 *		(up to 2?) in the scan structure itself.
 *		Fix to use the proper caching structure.
 * ----------------
 */
void
heap_markpos(HeapScanDesc scan)
{

	/* ----------------
	 *	increment access statistics
	 * ----------------
	 */
#ifdef USESTATS
	IncrHeapAccessStat(local_markpos);
	IncrHeapAccessStat(global_markpos);
#endif

	/* Note: no locking manipulations needed */

	/* ----------------
	 * Should not unpin the buffer pages.  They may still be in use.
	 * ----------------
	 */
	if (scan->rs_ctup.t_data != NULL)
		scan->rs_mctid = scan->rs_ctup.t_self;
	else
		ItemPointerSetInvalid(&scan->rs_mctid);
}

/* ----------------
 *		heap_restrpos	- restore position to marked location
 *
 *		Note:  there are bad side effects here.  If we were past the end
 *		of a relation when heapmarkpos is called, then if the relation is
 *		extended via insert, then the next call to heaprestrpos will set
 *		cause the added tuples to be visible when the scan continues.
 *		Problems also arise if the TID's are rearranged!!!
 *
 *		Now pins buffer once for each valid tuple pointer (rs_ptup,
 *		rs_ctup, rs_ntup) referencing it.
 *		 - 01/13/94
 *
 * XXX	might be better to do direct access instead of
 *		using the generality of heapgettup().
 *
 * XXX It is very possible that when a scan is restored, that a tuple
 * XXX which previously qualified may fail for time range purposes, unless
 * XXX some form of locking exists (ie., portals currently can act funny.
 * ----------------
 */
void
heap_restrpos(HeapScanDesc scan)
{
	/* ----------------
	 *	increment access statistics
	 * ----------------
	 */
#ifdef USESTATS
	IncrHeapAccessStat(local_restrpos);
	IncrHeapAccessStat(global_restrpos);
#endif

	/* XXX no amrestrpos checking that ammarkpos called */

	/* Note: no locking manipulations needed */

	unpinscan(scan);

	/* force heapgettup to pin buffer for each loaded tuple */
	scan->rs_cbuf = InvalidBuffer;

	if (!ItemPointerIsValid(&scan->rs_mctid))
	{
		scan->rs_ctup.t_datamcxt = NULL;
		scan->rs_ctup.t_datasrc = NULL;
		scan->rs_ctup.t_info = 0;
		scan->rs_ctup.t_data = NULL;
	}
	else
	{
            HeapTuple tuple = &scan->rs_ctup;
            tuple->t_self = scan->rs_mctid;
            tuple->t_info = 0;
            scan->rs_cbuf = RelationGetHeapTupleWithBuffer(scan->rs_rd,tuple,scan->rs_cbuf);
        }
}


static Buffer
NextGenGetTup(Relation relation,
		   HeapTuple tuple,
		   Buffer target,
		   Snapshot snapshot,
		   int nkeys,
		   ScanKey key)
{
        BlockNumber             page = 0;
	BlockNumber		total_pages = relation->rd_nblocks;
	int			lines = 0;
	OffsetNumber            lineoff;
	ItemPointer             tid = (tuple->t_data == NULL) ?
            (ItemPointer) NULL : &(tuple->t_self);

	/* ----------------
	 *	return null immediately if relation is empty
	 * ----------------
	 */
	if (total_pages == 0) {
            ItemPointerSetInvalid(&tuple->t_self);
            tuple->t_datamcxt = NULL;
            tuple->t_datasrc = NULL;
            tuple->t_info = 0;
            tuple->t_data = NULL;
            tuple->t_len = 0;
            return InvalidBuffer;
	}
        
        Assert(total_pages != InvalidBlockNumber);

        if (!ItemPointerIsValid(tid))
        {
                page = 0;			/* first page */
                lineoff = FirstOffsetNumber;		/* first offnum */
        }
        else
        {
                page = ItemPointerGetBlockNumber(tid);		/* current page */
                lineoff =			/* next offnum */
                        OffsetNumberNext(ItemPointerGetOffsetNumber(tid));
        }

        if (page >= total_pages)
        {
                if (BufferIsValid(target))
                        ReleaseBuffer(relation,target);
                tuple->t_datamcxt = NULL;
                tuple->t_datasrc = NULL;
                tuple->t_info = 0;
                tuple->t_data = NULL;
		tuple->t_len = 0;
		ItemPointerSetInvalid(&tuple->t_self);
                return InvalidBuffer;
        }

        /* ----------------
	 *	advance the scan until we find a qualifying tuple or
	 *	run out of stuff to scan
	 * ----------------
	 */

        while (page < total_pages && !IsShutdownProcessingMode()) {
            Page   dp = NULL;
            
            target = ReleaseAndReadBuffer(target, relation, page);
        
            if (!BufferIsValid(target))
                    elog(ERROR, "heapgettup: failed ReadBuffer");
            
           LockBuffer(relation,target,BUFFER_LOCK_SHARE);
           dp = BufferGetPage(target);
           lines = PageGetMaxOffsetNumber(dp);
            
            for (;lineoff <= lines;lineoff = OffsetNumberNext(lineoff))
            {
		ItemId itemid = PageGetItemId(dp,lineoff);
		if ( ItemIdIsUsed(itemid) ) {
                    tuple->t_data = (HeapTupleHeader) PageGetItem(dp, itemid);
                    tuple->t_len = ItemIdGetLength(itemid);
                    tuple->t_info = 0;
                    ItemPointerSet(&tuple->t_self,page,lineoff);
                    if ( !(tuple->t_data->t_infomask & HEAP_BLOB_SEGMENT) ) {
                        if  ( HeapTupleSatisfies(relation, target, tuple,  snapshot, nkeys, key) ) {
                            LockBuffer(relation,target,BUFFER_LOCK_UNLOCK);
                            return target;
                        }
                    }
                }
            }
            LockBuffer(relation,target,BUFFER_LOCK_UNLOCK);
 
            page = nextpage(page);
            lineoff = FirstOffsetNumber;            
        }
        if ( BufferIsValid(target) ) {
           ReleaseBuffer(relation,target);
        }
        
        tuple->t_datamcxt = NULL;
        tuple->t_datasrc = NULL;
        tuple->t_info = 0;
        tuple->t_data = NULL;
	tuple->t_len = 0;
	ItemPointerSetInvalid(&tuple->t_self);
        return InvalidBuffer;
}

