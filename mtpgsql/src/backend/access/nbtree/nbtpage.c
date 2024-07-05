/*-------------------------------------------------------------------------
 *
 * nbtpage.c
 *	  BTree-specific page management code for the Postgres btree access
 *	  method.
 *
 * Portions Copyright (c) 2000-2024, Myron Scott  <myron@weaverdb.org>
 * Portions Copyright (c) 1996-2002, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *
 *
 *	NOTES
 *	   Postgres btree pages look like ordinary relation pages.	The opaque
 *	   data at high addresses includes pointers to left and right siblings
 *	   and flag data describing page state.  The first page in a btree, page
 *	   zero, is special -- it stores meta-information describing the tree.
 *	   Pages one and higher store the actual tree data.
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"
#include "env/env.h"

#include <time.h>

#include "access/genam.h"
#include "access/nbtree.h"
#include "miscadmin.h"
#include "storage/lmgr.h"
#include "env/freespace.h"

static Buffer _bt_tryroot(Relation rel,bool create);
static bool _bt_buffer_reaped_check(Relation rel, Buffer buf);

/*
 *	We use high-concurrency locking on btrees.	There are two cases in
 *	which we don't do locking.  One is when we're building the btree.
 *	Since the creating transaction has not committed, no one can see
 *	the index, and there's no reason to share locks.  The second case
 *	is when we're just starting up the database system.  We use some
 *	special-purpose initialization code in the relation cache manager
 *	(see utils/cache/relcache.c) to allow us to do indexed scans on
 *	the system catalogs before we'd normally be able to.  This happens
 *	before the lock table is fully initialized, so we can't use it.
 *	Strictly speaking, this violates 2pl, but we don't do 2pl on the
 *	system catalogs anyway, so I declare this to be okay.
 */

#define USELOCKING		(!	GetIndexGlobals()->BuildingBtree && !IsInitProcessingMode())

/*
 *	_bt_metapinit() -- Initialize the metadata page of a btree.
 */
void
_bt_metapinit(Relation rel)
{
	Buffer		buf;
	Page		pg;
	BTMetaPageData metad;
	BTPageOpaque op;

	/* can't be sharing this with anyone, now... */
	if (USELOCKING)
		LockRelation(rel, AccessExclusiveLock);

	if (RelationGetNumberOfBlocks(rel) != 0)
		elog(ERROR, "Cannot initialize non-empty btree %s",
			 RelationGetRelationName(rel));
        
	buf = _bt_getbuf(rel,BTREE_METAPAGE,BT_READYWRITE);
        if (!BufferIsValid(buf) ) 
                    elog(ERROR,"bad buffer read while scanning btree %s",RelationGetRelationName(rel));
	pg = BufferGetPage(buf);
	_bt_pageinit(pg, BufferGetPageSize(buf));

	metad.btm_magic = BTREE_MAGIC;
	metad.btm_version = BTREE_VERSION;
	metad.btm_root = P_NONE;
	metad.btm_level = 0;
	memcpy((char *) BTPageGetMeta(pg), (char *) &metad, sizeof(metad));

	op = (BTPageOpaque) PageGetSpecialPointer(pg);
	op->btpo_flags = BTP_META;
        op->btpo_parent = 0;

	_bt_wrtbuf(rel, buf);

	/* all done */
	if (USELOCKING)
		UnlockRelation(rel, AccessExclusiveLock);
}

Buffer
_bt_getroot(Relation rel, int access)
{
    BlockNumber rootparent = InvalidBlockNumber;
    Buffer root = InvalidBuffer;
    bool create = (access == BT_WRITE);
   
    while ( !BufferIsValid(root) ) {
        root = (BlockNumberIsValid(rootparent)) ? _bt_getbuf(rel,rootparent,BT_READ) : _bt_tryroot(rel,create);
        if ( BufferIsValid(root) ) {
            Page rootpage = BufferGetPage(root);
            BTPageOpaque rootopaque = (BTPageOpaque) PageGetSpecialPointer(rootpage);
            if (!P_ISROOT(rootopaque)) {
                rootparent = rootopaque->btpo_parent;
                _bt_relbuf(rel,root);
                root = InvalidBuffer;
                Assert(!(rootparent == BTREE_METAPAGE));
                /*  try again */
            }
        } else {
            /*  no root page and don't create one  */
            if ( !create ) break;
        }
    }
    if ( create ) {
/*
        BiasBuffer(rel,root);
*/
    }
    return root;
}

/*
 *	_bt_getroot() -- Get the root page of the btree.
 *
 *		Since the root page can move around the btree file, we have to read
 *		its location from the metadata page, and then read the root page
 *		itself.  If no root page exists yet, we have to create one.  The
 *		standard class of race conditions exists here; I think I covered
 *		them all in the Hopi Indian rain dance of lock requests below.
 *
 *		The access type parameter (BT_READ or BT_WRITE) controls whether
 *		a new root page will be created or not.  If access = BT_READ,
 *		and no root page exists, we just return InvalidBuffer.	For
 *		BT_WRITE, we try to create the root page if it doesn't exist.
 *		NOTE that the returned root page will have only a read lock set
 *		on it even if access = BT_WRITE!
 *
 *		On successful return, the root page is pinned and read-locked.
 *		The metadata page is not locked or pinned on exit.
 */
Buffer
_bt_tryroot(Relation rel, bool create)
{
	Buffer		metabuf;
	Page		metapg;
	BTMetaPageData *metad;
        BlockNumber     root;

	metabuf = _bt_getbuf(rel, BTREE_METAPAGE, BT_READ);
	metapg = BufferGetPage(metabuf);
	metad = BTPageGetMeta(metapg);
        
	Assert(((BTPageOpaque)PageGetSpecialPointer(metapg))->btpo_flags & BTP_META);
        Assert(metad->btm_magic == BTREE_MAGIC);
        Assert(metad->btm_version == BTREE_VERSION);

        root = metad->btm_root;
        
        _bt_relbuf(rel,metabuf);

	/* if no root page initialized yet, do it */
	if (root == P_NONE) {
		/* If access = BT_READ, caller doesn't want us to create root yet */
		if (!create) {
                    return InvalidBuffer;
		}

		metabuf = _bt_getbuf(rel, BTREE_METAPAGE, BT_WRITE);
                metapg = BufferGetPage(metabuf);
                metad = BTPageGetMeta(metapg);
                 /*
		 * Race condition:	if someone else initialized the metadata
		 * between the time we released the read lock and acquired the
		 * write lock, above, we must avoid doing it again.
		 */
		if (metad->btm_root == P_NONE) {
			/*
			 * Get, initialize, write, and leave a lock of the appropriate
			 * type on the new root page.  Since this is the first page in
			 * the tree, it's a leaf as well as the root.
			 */
			Buffer rootbuf = _bt_getbuf(rel, P_NEW, BT_READYWRITE);
			Page rootpage = BufferGetPage(rootbuf);
                        root = BufferGetBlockNumber(rootbuf);

			/* NO ELOG(ERROR) till meta is updated */
			metad->btm_root = root;
			metad->btm_level = 1;

			_bt_pageinit(rootpage, BufferGetPageSize(rootbuf));
			((BTPageOpaque) PageGetSpecialPointer(rootpage))->btpo_flags |= (BTP_LEAF | BTP_ROOT);
                        ((BTPageOpaque) PageGetSpecialPointer(rootpage))->btpo_parent = BTREE_METAPAGE;

			_bt_wrtbuf(rel, rootbuf);

			/* okay, metadata is correct, write and release it */
			_bt_wrtbuf(rel, metabuf);
                        return _bt_getbuf(rel, root, BT_READ);
		} else {
			/*
			 * Metadata initialized by someone else.  In order to
			 * guarantee no deadlocks, we have to release the metadata
			 * page and start all over again.
			 */
			_bt_relbuf(rel, metabuf);
			return InvalidBuffer;
		}
	} else {
            return _bt_getbuf(rel, root, BT_READ);
	}
}

/*
 *	_bt_getbuf() -- Get a buffer by block number for read or write.
 *
 *		When this routine returns, the appropriate lock is set on the
 *		requested buffer and its reference count has been incremented
 *		(ie, the buffer is "locked and pinned").
 */
Buffer
_bt_getbuf(Relation rel, BlockNumber blkno, int access)
{
	Buffer		buf = InvalidBuffer;

	if (blkno != P_NEW) {
		/* Read an existing block of the relation */
            Assert(blkno == BTREE_METAPAGE || blkno < RelationGetNumberOfBlocks(rel));
		buf = ReadBuffer(rel, blkno);
                if ( !BufferIsValid(buf) ) {
/*  ok this is will undoubtly cause buffer leaks and 
    held locks but it should be ok to rely on AbortTransaction
    to clean up this situation properly */
                    elog(ERROR,"error reading index page %ld for index %s",blkno,RelationGetRelationName(rel));
                }
                LockBuffer((rel),buf, access);
	}
	else
	{
		Page		page;
                BTPageOpaque opaque;
                BTPageOpaqueData init = {
                    BTP_REAPED,
                    0,
                    InvalidBlockNumber,
                    0
                };
                 /*
		 * Extend the relation by one page.
		 *
		 * Extend bufmgr code is unclean and so we have to use extra locking
		 * here.
		 */
                while ( !BufferIsValid(buf) ) {
                    BlockNumber blk = BTREE_METAPAGE;
                    while( blk == BTREE_METAPAGE ) {
                        blk = AllocateMoreSpace(rel,(char*)&init,sizeof(BTPageOpaqueData));
                    }
                    buf = ReadBuffer(rel,blk);
                    
                    if ( !BufferIsValid(buf) ) {
                        elog(ERROR,"error creating new index page for index %s",RelationGetRelationName(rel));
                    }                    
                    
                   page = BufferGetPage(buf);
                   opaque = (BTPageOpaque)PageGetSpecialPointer(page);

                    /* Initialize the new page before returning it */
                    if ( !BufferPrivateCheck(rel, buf, _bt_buffer_reaped_check) ) {
                        ReleaseBuffer(rel,buf);
                        buf = InvalidBuffer;
                    }
                }

                Assert(access == BT_WRITE || access == BT_READYWRITE);
                LockBuffer((rel),  buf, access);

	}

	/* ref count and lock type are correct */
	return buf;
}

bool
_bt_buffer_reaped_check(Relation rel, Buffer buf) {
    Page page = BufferGetPage(buf);
        if ( BufferGetBlockNumber(buf) == 0 ) return false;
        if ( PageIsNew(page) || PageChecksumIsInit(page) || P_ISREAPED((BTPageOpaque)PageGetSpecialPointer(page)) )  {
            _bt_pageinit(page,BufferGetPageSize(buf));
            return true;
        }
        return false;
}

/*
 *	_bt_relbuf() -- release a locked buffer.
 *
 * Lock and pin (refcount) are both dropped.  Note that either read or
 * write lock can be dropped this way, but if we modified the buffer,
 * this is NOT the right way to release a write lock.
 */
void
_bt_relbuf(Relation rel, Buffer buf)
{
	LockBuffer((rel),  buf, BT_NONE);
	ReleaseBuffer(rel, buf);
}

/*
 *	_bt_wrtbuf() -- write a btree page to disk.
 *
 *		This routine releases the lock held on the buffer and our refcount
 *		for it.  It is an error to call _bt_wrtbuf() without a write lock
 *		and a pin on the buffer.
 *
 * NOTE: actually, the buffer manager just marks the shared buffer page
 * dirty here, the real I/O happens later.	Since we can't persuade the
 * Unix kernel to schedule disk writes in a particular order, there's not
 * much point in worrying about this.  The most we can say is that all the
 * writes will occur before commit.
 */
void
_bt_wrtbuf(Relation rel, Buffer buf)
{
	LockBuffer((rel),  buf, BT_NONE);
	WriteBuffer(rel, buf);
}

/*
 *	_bt_wrtnorelbuf() -- write a btree page to disk, but do not release
 *						 our reference or lock.
 *
 *		It is an error to call _bt_wrtnorelbuf() without a write lock
 *		and a pin on the buffer.
 *
 * See above NOTE.
 */
void
_bt_wrtnorelbuf(Relation rel, Buffer buf)
{
	WriteNoReleaseBuffer(rel, buf);
}

/*
 *	_bt_pageinit() -- Initialize a new page.
 */
void
_bt_pageinit(Page page, Size size)
{
        PageInit(page, size, sizeof(BTPageOpaqueData));
        PageInsertInvalidChecksum(page);
	((BTPageOpaque) PageGetSpecialPointer(page))->btpo_parent =
		InvalidBlockNumber;
	((BTPageOpaque) PageGetSpecialPointer(page))->btpo_next =
		0;       
	((BTPageOpaque) PageGetSpecialPointer(page))->btpo_prev =
		0;        
	((BTPageOpaque) PageGetSpecialPointer(page))->btpo_flags =
		0;                
}

bool 
_bt_empty(Page page) {
    BTPageOpaque opaque = (BTPageOpaque)PageGetSpecialPointer(page);
    OffsetNumber first = P_FIRSTDATAKEY(opaque);
    OffsetNumber max = PageGetMaxOffsetNumber(page);
    return (first > max);
}

/*
 *	_bt_metaproot() -- Change the root page of the btree.
 *
 *		Lehman and Yao require that the root page move around in order to
 *		guarantee deadlock-free short-term, fine-granularity locking.  When
 *		we split the root page, we record the new parent in the metadata page
 *		for the relation.  This routine does the work.
 *
 *		No direct preconditions, but if you don't have the write lock on
 *		at least the old root page when you call this, you're making a big
 *		mistake.  On exit, metapage data is correct and we no longer have
 *		a pin or lock on the metapage.
 */
void
_bt_metaproot(Relation rel, BlockNumber rootbknum, int level)
{
	Buffer		metabuf;
	Page		metap;
	BTPageOpaque metaopaque;
	BTMetaPageData *metad;

	metabuf = _bt_getbuf(rel, BTREE_METAPAGE, BT_WRITE);
	metap = BufferGetPage(metabuf);
	metaopaque = (BTPageOpaque) PageGetSpecialPointer(metap);
	Assert(metaopaque->btpo_flags & BTP_META);
	metad = BTPageGetMeta(metap);
	metad->btm_root = rootbknum;
	if (level == 0)				/* called from _do_insert */
		metad->btm_level += 1;
	else
		metad->btm_level = level;		/* called from btsort */
	_bt_wrtbuf(rel, metabuf);
}

/*
 * Delete an item from a btree page.
 *
 * This routine assumes that the caller has pinned and locked the buffer,
 * and will write the buffer afterwards.
 */
void
_bt_itemdel(Relation rel, Buffer buf, ItemPointer tid)
{
	Page		page = BufferGetPage(buf);
	OffsetNumber offno;

	offno = ItemPointerGetOffsetNumber(tid);


	PageIndexTupleDelete(page, offno);

}
