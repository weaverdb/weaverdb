/*-------------------------------------------------------------------------
 *
 * bufpage.h
 *	  Standard POSTGRES buffer page definitions.
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 *
 *-------------------------------------------------------------------------
 */
#ifndef BUFPAGE_H
#define BUFPAGE_H


#include "storage/buf.h"
#include "storage/page.h"
#include "storage/item.h"
#include "storage/itemid.h"
#include "storage/off.h"
#include "storage/bufmgr.h"
#include "env/pg_crc.h"
/*
 * a postgres disk page is an abstraction layered on top of a postgres
 * disk block (which is simply a unit of i/o, see block.h).
 *
 * specifically, while a disk block can be unformatted, a postgres
 * disk page is always a slotted page of the form:
 *
 * +----------------+---------------------------------+
 * | PageHeaderData | linp1 linp2 linp3 ...			  |
 * +-----------+----+---------------------------------+
 * | ... linpN |									  |
 * +-----------+--------------------------------------+
 * |		   ^ pd_lower							  |
 * |												  |
 * |			 v pd_upper							  |
 * +-------------+------------------------------------+
 * |			 | tupleN ...						  |
 * +-------------+------------------+-----------------+
 * |	   ... tuple3 tuple2 tuple1 | "special space" |
 * +--------------------------------+-----------------+
 *									^ pd_special
 *
 * a page is full when nothing can be added between pd_lower and
 * pd_upper.
 *
 * all blocks written out by an access method must be disk pages.
 *
 * EXCEPTIONS:
 *
 * obviously, a page is not formatted before it is initialized with by
 * a call to PageInit.
 *
 * the contents of the special pg_variable/pg_time/pg_log tables are
 * raw disk blocks with special formats.  these are the only "access
 * methods" that need not write disk pages.
 *
 * NOTES:
 *
 * linp1..N form an ItemId array.  ItemPointers point into this array
 * rather than pointing directly to a tuple.  Note that OffsetNumbers
 * conventionally start at 1, not 0.
 *
 * tuple1..N are added "backwards" on the page.  because a tuple's
 * ItemPointer points to its ItemId entry rather than its actual
 * byte-offset position, tuples can be physically shuffled on a page
 * whenever the need arises.
 *
 * AM-generic per-page information is kept in the pd_opaque field of
 * the PageHeaderData.	(Currently, only the page size is kept here.)
 *
 * AM-specific per-page data (if any) is kept in the area marked "special
 * space"; each AM has an "opaque" structure defined somewhere that is
 * stored as the page trailer.	an access method should always
 * initialize its pages with PageInit and then set its own opaque
 * fields.
 */

/*
 * PageIsValid
 *		True iff page is valid.
 */
#define PageIsValid(page) PointerIsValid(page)


/*
 * location (byte offset) within a page.
 *
 * note that this is actually limited to 2^15 because we have limited
 * ItemIdData.lp_off and ItemIdData.lp_len to 15 bits (see itemid.h).
 */
typedef uint32 LocationIndex;


/*
 * space management information generic to any page
 *
 *		od_pagesize		- size in bytes.
 *						  Minimum possible page size is perhaps 64B to fit
 *						  page header, opaque space and a minimal tuple;
 *						  of course, in reality you want it much bigger.
 *						  On the high end, we can only support pages up
 *						  to 32KB because lp_off/lp_len are 15 bits.
 */
typedef struct OpaqueData
{
	uint32		od_pagesize;
/*        uint64		synctoken;     */
} OpaqueData;

typedef OpaqueData *Opaque;


/*
 * disk page organization
 */
typedef struct PageHeaderData
{
    	uint64			checksum;
	LocationIndex pd_lower;		/* offset to start of free space */
	LocationIndex pd_upper;		/* offset to end of free space */
	LocationIndex pd_special;	/* offset to start of special space */
	OpaqueData	pd_opaque;	/* AM-generic information */
	ItemIdData	pd_linp[1];		/* beginning of line pointer array */
} PageHeaderData;

typedef PageHeaderData *PageHeader;

typedef enum
{
	ShufflePageManagerMode,
	OverwritePageManagerMode
} PageManagerMode;

/* ----------------------------------------------------------------
 *						page support macros
 * ----------------------------------------------------------------
 */
/*
 * PageIsValid -- This is defined in page.h.
 */

/*
 * PageIsUsed
 *		True iff the page size is used.
 *
 * Note:
 *		Assumes page is valid.
 */
#define PageIsUsed(page) \
( \
	AssertMacro(PageIsValid(page)), \
	((bool) (((PageHeader) (page))->pd_lower != 0)) \
)

/*
 * PageIsEmpty
 *		returns true iff no itemid has been allocated on the page
 */
 /*
#define PageIsEmpty(page) \
	(((PageHeader) (page))->pd_lower <= \
	 ((long)&((PageHeader)page)->pd_linp) - ((long)&page))
*/
/*
 * PageIsNew
 *		returns true iff page is not initialized (by PageInit)
 */

#define PageIsNew(page) (((PageHeader) (page))->pd_upper == 0)

/*
 * PageGetItemId
 *		Returns an item identifier of a page.
 */

#define PageGetItemId(page, offsetNumber) \
	((ItemId) (&((PageHeader) (page))->pd_linp[(offsetNumber) - 1]))


/* ----------------
 *		macros to access opaque space
 * ----------------
 */

/*
 * PageSizeIsValid
 *		True iff the page size is valid.
 *
 * XXX currently all page sizes are "valid" but we only actually
 *	   use BLCKSZ.
 *
 * 01/06/98 Now does something useful.	darrenk
 *
 */
#define PageSizeIsValid(pageSize) ((pageSize) == BLCKSZ)

/*
 * PageGetPageSize
 *		Returns the page size of a page.
 *
 * this can only be called on a formatted page (unlike
 * BufferGetPageSize, which can be called on an unformatted page).
 * however, it can be called on a page for which there is no buffer.
 */
#define PageGetPageSize(page) \
	((Size) ((PageHeader) (page))->pd_opaque.od_pagesize)

/*
 * PageSetPageSize
 *		Sets the page size of a page.
 */
#define PageSetPageSize(page, size) \
	(((PageHeader) (page))->pd_opaque.od_pagesize = (size))

/* ----------------
 *		page special data macros
 * ----------------
 */
/*
 * PageGetSpecialSize
 *		Returns size of special space on a page.
 *
 * Note:
 *		Assumes page is locked.
 */
#define PageGetSpecialSize(page) \
	((uint32) (PageGetPageSize(page) - ((PageHeader)(page))->pd_special))

/*
 * PageGetSpecialPointer
 *		Returns pointer to special space on a page.
 *
 * Note:
 *		Assumes page is locked.
 */
#define PageGetSpecialPointer(page) \
( \
	AssertMacro(PageIsValid(page)), \
	(char *) ((char *) (page) + ((PageHeader) (page))->pd_special) \
)

/*
 * PageGetItem
 *		Retrieves an item on the given page.
 *
 * Note:
 *		This does not change the status of any of the resources passed.
 *		The semantics may change in the future.
 */
#define PageGetItem(page, itemId) \
( \
	AssertMacro(PageIsValid(page)), \
	AssertMacro((itemId)->lp_flags & LP_USED), \
	(Item)(((((itemId)->lp_off) % MAXIMUM_ALIGNOF) == 0 ) ? \
	(((char *)(page)) + (itemId)->lp_off) : NULL) \
)

/*
 * BufferGetPageSize
 *		Returns the page size within a buffer.
 *
 * Notes:
 *		Assumes buffer is valid.
 *
 *		The buffer can be a raw disk block and need not contain a valid
 *		(formatted) disk page.
 */
/* XXX should dig out of buffer descriptor */
#define BufferGetPageSize(buffer) \
( \
	AssertMacro(BufferIsValid(buffer)), \
	(Size)BLCKSZ \
)

/*
 * BufferGetPage
 *		Returns the page associated with a buffer.
 */
#define BufferGetPage(buffer) ((Page)BufferGetBlock(buffer))

#define PageGetContents(page) \
	((char *) (&((PageHeader) (page))->pd_linp[0]))
        
/*
 * PageGetMaxOffsetNumber
 *		Returns the maximum offset number used by the given page.
 *		Since offset numbers are 1-based, this is also the number
 *		of items on the page.
 *
 *		NOTE: to ensure sane behavior if the page is not initialized
 *		(pd_lower == 0), cast the unsigned values to int before dividing.
 *		That way we get -1 or so, not a huge positive number...
 */
/*
#define PageGetMaxOffsetNumber(page) \
	(((int) (((PageHeader) (page))->pd_lower - \
			 (sizeof(PageHeaderData) - sizeof(ItemIdData)))) \
	 / ((int) sizeof(ItemIdData)))
*/

/* ----------------------------------------------------------------
 *		extern declarations
 * ----------------------------------------------------------------
 */
PG_EXTERN OffsetNumber PageGetMaxOffsetNumber(Page page);
PG_EXTERN bool PageIsEmpty(Page page);

PG_EXTERN void PageInit(Page page, Size pageSize, Size specialSize);
PG_EXTERN OffsetNumber PageAddItem(Page page, Item item, Size size,
			OffsetNumber offsetNumber, ItemIdFlags flags);
PG_EXTERN Page PageGetTempPage(Page page, Size specialSize);
PG_EXTERN void PageRestoreTempPage(Page tempPage, Page oldPage);
PG_EXTERN int PageRepairFragmentation(Page page);
PG_EXTERN int PageCompactPage(Page page);
PG_EXTERN Size PageGetFreeSpace(Page page);
PG_EXTERN void PageManagerModeSet(PageManagerMode mode);
PG_EXTERN void PageIndexTupleDelete(Page page, OffsetNumber offset);
PG_EXTERN crc64 PageInsertChecksum(Page page);
PG_EXTERN crc64 PageInsertInvalidChecksum(Page page);
PG_EXTERN bool PageChecksumIsInvalid(Page page);
PG_EXTERN bool PageChecksumIsInit(Page page);
PG_EXTERN bool PageConfirmChecksum(Page page);
PG_EXTERN bool DisableCRC(bool enable);

#endif	 /* BUFPAGE_H */
