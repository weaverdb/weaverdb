/*-------------------------------------------------------------------------
 *
 * bufpage.c
 *	  POSTGRES standard buffer page code.
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  $Header: /cvs/weaver/mtpgsql/src/backend/storage/page/bufpage.c,v 1.2 2007/05/23 15:39:24 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */
#include <sys/types.h>
#include <sys/file.h>

#include "postgres.h"

#include "storage/bufpage.h"
#include "env/pg_crc.h"

bool        disable_crc = true;

static void PageIndexTupleDeleteAdjustLinePointers(PageHeader phdr,
									   char *location, Size size);

static bool PageManagerShuffle = true;	/* default is shuffle mode */
	
static void PageSetLinePointerCount(Page page,Size loc);

/* ----------------------------------------------------------------
 *						Page support functions
 * ----------------------------------------------------------------
 */

/*
 * PageInit
 *		Initializes the contents of a page.
 */
void
PageInit(Page page, Size pageSize, Size specialSize)
{
	PageHeader	p = (PageHeader) page;

	Assert(pageSize == BLCKSZ);
	Assert(pageSize >
		   specialSize + sizeof(PageHeaderData) - sizeof(ItemIdData));

	specialSize = MAXALIGN(specialSize);

	p->pd_lower = ((unsigned long)&p->pd_linp) - ((unsigned long)p);
	p->pd_upper = pageSize - specialSize;
	p->pd_special = pageSize - specialSize;
        p->checksum = InitCRC64;
	PageSetPageSize(page, pageSize);
}

/*
 * PageAddItem
 *		Adds item to the given page.
 *
 * Note:
 *		This does not assume that the item resides on a single page.
 *		It is the responsiblity of the caller to act appropriately
 *		depending on this fact.  The "pskip" routines provide a
 *		friendlier interface, in this case.
 *
 *		This does change the status of any of the resources passed.
 *		The semantics may change in the future.
 *
 *		This routine should probably be combined with others?
 */
/* ----------------
 *		PageAddItem
 *
 *		add an item to a page.
 *
 *	 Notes on interface:
 *		If offsetNumber is valid, shuffle ItemId's down to make room
 *		to use it, if PageManagerShuffle is true.  If PageManagerShuffle is
 *		false, then overwrite the specified ItemId.  (PageManagerShuffle is
 *		true by default, and is modified by calling PageManagerModeSet.)
 *		If offsetNumber is not valid, then assign one by finding the first
 *		one that is both unused and deallocated.
 *
 *	 NOTE: If offsetNumber is valid, and PageManagerShuffle is true, it
 *		is assumed that there is room on the page to shuffle the ItemId's
 *		down by one.
 * ----------------
 */
OffsetNumber
PageAddItem(Page page,
			Item item,
			Size size,
			OffsetNumber offsetNumber,
			ItemIdFlags flags)
{
	int			i;
	Size		alignedSize;
	Offset		lower;
	Offset		upper;
	ItemId		itemId;
	ItemId		fromitemId,
				toitemId;
	OffsetNumber limit;

	bool		shuffled = false;

	/*
	 * Find first unallocated offsetNumber
	 */
	limit = OffsetNumberNext(PageGetMaxOffsetNumber(page));

	/* was offsetNumber passed in? */
	if (OffsetNumberIsValid(offsetNumber))
	{
		if (PageManagerShuffle == true)
		{
			/* shuffle ItemId's (Do the PageManager Shuffle...) */
			for (i = (limit - 1); i >= offsetNumber; i--)
			{
				fromitemId = &((PageHeader) page)->pd_linp[i - 1];
				toitemId = &((PageHeader) page)->pd_linp[i];
				*toitemId = *fromitemId;
			}
			shuffled = true;	/* need to increase "lower" */
		}
		else
		{						/* overwrite mode */
			itemId = &((PageHeader) page)->pd_linp[offsetNumber - 1];
			if ((itemId->lp_flags & LP_USED) ||
				(itemId->lp_len != 0))
			{
				elog(ERROR, "PageAddItem: tried overwrite of used ItemId");
				return InvalidOffsetNumber;
			}
		}
	}
	else
	{							/* offsetNumber was not passed in, so find
								 * one */
		/* look for "recyclable" (unused & deallocated) ItemId */
		for (offsetNumber = 1; offsetNumber < limit; offsetNumber++)
		{
			itemId = &((PageHeader) page)->pd_linp[offsetNumber - 1];
                        if ( !(itemId->lp_flags & LP_USED) && (itemId->lp_len == 0) ) {
				break;
                        }
		}
	}
	if (offsetNumber > limit)
		lower = (Offset) (((char *) (&((PageHeader) page)->pd_linp[offsetNumber])) - ((char *) page));
	else if (offsetNumber == limit || shuffled == true)
		lower = ((PageHeader) page)->pd_lower + sizeof(ItemIdData);
	else
		lower = ((PageHeader) page)->pd_lower;

	alignedSize = MAXALIGN(size);

	upper = ((PageHeader) page)->pd_upper - alignedSize;

	if (lower > upper)
		return InvalidOffsetNumber;

	itemId = &((PageHeader) page)->pd_linp[offsetNumber - 1];
	itemId->lp_off = upper;
	itemId->lp_len = size;
	itemId->lp_flags = flags;

	memmove((char *) page + upper, item, size);

	((PageHeader) page)->pd_lower = lower;
	((PageHeader) page)->pd_upper = upper;

	return offsetNumber;
}

/*
 * PageGetTempPage
 *		Get a temporary page in local memory for special processing
 */
Page
PageGetTempPage(Page page, Size specialSize)
{
	Size		pageSize;
	Size		size;
	Page		temp;
	PageHeader	thdr;

	pageSize = PageGetPageSize(page);

	if ((temp = (Page) palloc(pageSize)) == (Page) NULL)
		elog(FATAL, "Cannot allocate %ld bytes for temp page.", pageSize);
	thdr = (PageHeader) temp;

	/* copy old page in */
	memmove(temp, page, pageSize);

	/* clear out the middle */
	size = (pageSize - sizeof(PageHeaderData)) + sizeof(ItemIdData);
	size -= MAXALIGN(specialSize);
	MemSet((char *) &(thdr->pd_linp[0]), 0, size);

	/* set high, low water marks */
	thdr->pd_lower = ((unsigned long)&thdr->pd_linp) - ((unsigned long)thdr);
	thdr->pd_upper = pageSize - MAXALIGN(specialSize);

	return temp;
}

/*
 * PageRestoreTempPage
 *		Copy temporary page back to permanent page after special processing
 *		and release the temporary page.
 */
void
PageRestoreTempPage(Page tempPage, Page oldPage)
{
	Size		pageSize;

	pageSize = PageGetPageSize(tempPage);
	memmove((char *) oldPage, (char *) tempPage, pageSize);

	pfree(tempPage);
}

/* ----------------
 *		itemid stuff for PageRepairFragmentation
 * ----------------
 */
struct itemIdSortData
{
	int			offsetindex;	/* linp array index */
	ItemIdData	itemiddata;
};

static int
itemidcompare(const void *itemidp1, const void *itemidp2)
{
	if (((struct itemIdSortData *) itemidp1)->itemiddata.lp_off ==
		((struct itemIdSortData *) itemidp2)->itemiddata.lp_off)
		return 0;
	else if (((struct itemIdSortData *) itemidp1)->itemiddata.lp_off <
			 ((struct itemIdSortData *) itemidp2)->itemiddata.lp_off)
		return 1;
	else
		return -1;
}

int 
PageCompactPage(Page page)
{
	int			i;
	ItemId		lp;
	int			nline;

	nline = (PageGetMaxOffsetNumber(page));
        
        if ( nline == 0 ) return 0;

	for (i = (nline); i > 0; i--)
	{
            lp = PageGetItemId(page,i);
            if (ItemIdIsUsed(lp) || ItemIdGetLength(lp) > 0 )
                break;
	}

        if (i != nline) {
            PageSetLinePointerCount(page, i);
	}
	return i;
}

/*
 * PageRepairFragmentation
 *		Frees fragmented space on a page.
 */
int
PageRepairFragmentation(Page page)
{
	int			i;
	struct itemIdSortData *itemidbase,
			   *itemidptr;
	ItemId		lp;
	int			nline,
				nused,
				free;
	Offset		upper;
	Size		alignedSize;

	nline = PageGetMaxOffsetNumber(page);
        
	nused = free = 0;
        
	for (i = 0; i < nline; i++)
	{
		lp = ((PageHeader) page)->pd_linp + i;
		if (lp->lp_flags & LP_USED)
			nused++;
		else 
			free++;
	}

	if (nused == 0)
	{
            for (i = 0; i < nline; i++)
            {
                    lp = ((PageHeader) page)->pd_linp + i;
                    lp->lp_len = 0;		/* indicate unused & deallocated */
            }
            ((PageHeader) page)->pd_upper = ((PageHeader) page)->pd_special;
	}
	else
	{							/* nused != 0 */
		itemidbase = (struct itemIdSortData *)
			palloc(sizeof(struct itemIdSortData) * nused);
		MemSet((char *) itemidbase, 0, sizeof(struct itemIdSortData) * nused);
		itemidptr = itemidbase;
		for (i = 0; i < nline; i++)
		{
			lp = ((PageHeader) page)->pd_linp + i;
			if (lp->lp_flags & LP_USED)
			{
                            itemidptr->offsetindex = i;
                            itemidptr->itemiddata = *lp;
                            itemidptr++;
			}
			else
			{
                            lp->lp_len = 0;	/* indicate unused & deallocated */
			}
		}

		/* sort itemIdSortData array... */
		qsort((char *) itemidbase, nused, sizeof(struct itemIdSortData),
			  itemidcompare);

		/* compactify page */
		((PageHeader) page)->pd_upper = ((PageHeader) page)->pd_special;

		for (i = 0, itemidptr = itemidbase; i < nused; i++, itemidptr++)
		{
			lp = ((PageHeader) page)->pd_linp + itemidptr->offsetindex;
			alignedSize = MAXALIGN(lp->lp_len);
			upper = ((PageHeader) page)->pd_upper - alignedSize;
			memmove((char *) page + upper,
                                (char *) page + lp->lp_off,
                                lp->lp_len);
			lp->lp_off = upper;
			((PageHeader) page)->pd_upper = upper;
		}

		pfree(itemidbase);
	}
	return free;
}

/*
 * PageGetFreeSpace
 *		Returns the size of the free (allocatable) space on a page.
 */
Size
PageGetFreeSpace(Page page)
{
	Size		space;

        if ( ((PageHeader) page)->pd_upper < ((PageHeader) page)->pd_lower ) {
            return 0;
        }


	space = ((PageHeader) page)->pd_upper - ((PageHeader) page)->pd_lower;

	if (space < sizeof(ItemIdData))
		return 0;
	space -= sizeof(ItemIdData);/* XXX not always true */

	return space;
}

/*
 * PageManagerModeSet
 *
 *	 Sets mode to either: ShufflePageManagerMode (the default) or
 *	 OverwritePageManagerMode.	For use by access methods code
 *	 for determining semantics of PageAddItem when the offsetNumber
 *	 argument is passed in.
 */
void
PageManagerModeSet(PageManagerMode mode)
{
	if (mode == ShufflePageManagerMode)
		PageManagerShuffle = true;
	else if (mode == OverwritePageManagerMode)
		PageManagerShuffle = false;
}

bool
DisableCRC(bool enable) {
    bool prev = disable_crc;
    disable_crc = !enable;
	return prev != disable_crc;
}

/*
 *----------------------------------------------------------------
 * PageIndexTupleDelete
 *----------------------------------------------------------------
 *
 *		This routine does the work of removing a tuple from an index page.
 */
void
PageIndexTupleDelete(Page page, OffsetNumber offnum)
{
	PageHeader	phdr;
	char	   *addr;
	ItemId		tup;
	Size		size;
	char	   *locn;
	int			nbytes;
	int			offidx;

	phdr = (PageHeader) page;

	/* change offset number to offset index */
	offidx = offnum - 1;

	tup = PageGetItemId(page, offnum);
	size = ItemIdGetLength(tup);
	size = MAXALIGN(size);

	/* location of deleted tuple data */
	locn = (char *) (page + ItemIdGetOffset(tup));

	/*
	 * First, we want to get rid of the pd_linp entry for the index tuple.
	 * We copy all subsequent linp's back one slot in the array.
	 */

	nbytes = phdr->pd_lower -
		((char *) &phdr->pd_linp[offidx + 1] - (char *) phdr);
	memmove((char *) &(phdr->pd_linp[offidx]),
			(char *) &(phdr->pd_linp[offidx + 1]),
			nbytes);

	/*
	 * Now move everything between the old upper bound (beginning of tuple
	 * space) and the beginning of the deleted tuple forward, so that
	 * space in the middle of the page is left free.  If we've just
	 * deleted the tuple at the beginning of tuple space, then there's no
	 * need to do the copy (and bcopy on some architectures SEGV's if
	 * asked to move zero bytes).
	 */

	/* beginning of tuple space */
	addr = (char *) (page + phdr->pd_upper);

	if (locn != addr)
		memmove(addr + size, addr, (int) (locn - addr));

	/* adjust free space boundary pointers */
	phdr->pd_upper += size;
	phdr->pd_lower -= sizeof(ItemIdData);

	/* finally, we need to adjust the linp entries that remain */
	if (!PageIsEmpty(page))
		PageIndexTupleDeleteAdjustLinePointers(phdr, locn, size);
}

/*
 *----------------------------------------------------------------
 * PageIndexTupleDeleteAdjustLinePointers
 *----------------------------------------------------------------
 *
 *		Once the line pointers and tuple data have been shifted around
 *		on the page, we need to go down the line pointer vector and
 *		adjust pointers to reflect new locations.  Anything that used
 *		to be before the deleted tuple's data was moved forward by the
 *		size of the deleted tuple.
 *
 *		This routine does the work of adjusting the line pointers.
 *		Location is where the tuple data used to lie; size is how
 *		much space it occupied.  We assume that size has been aligned
 *		as required by the time we get here.
 *
 *		This routine should never be called on an empty page.
 */
static void
PageIndexTupleDeleteAdjustLinePointers(PageHeader phdr,
									   char *location,
									   Size size)
{
	int			i;
	unsigned	offset;

	/* location is an index into the page... */
	offset = (unsigned) (location - (char *) phdr);

	for (i = PageGetMaxOffsetNumber((Page) phdr) - 1; i >= 0; i--)
	{
		if (phdr->pd_linp[i].lp_off <= offset)
			phdr->pd_linp[i].lp_off += size;
	}
}

OffsetNumber PageGetMaxOffsetNumber(Page page)
{
	PageHeader ph = (PageHeader)page;
	long tail = ph->pd_lower;
	long head = (((unsigned long)&(ph->pd_linp)) - ((unsigned long)ph));  
        if ( tail <= head ) return 0;
	long max = ( tail - head ) / MAXALIGN(sizeof(ItemIdData));
 /*       printf("g max:%i\n",max);      */
        return max;
}
	
void PageSetLinePointerCount(Page page,Size loc)
{
	PageHeader ph = (PageHeader)page;
	long head = (((unsigned long)&(ph->pd_linp)) - ((unsigned long)ph));  
	ph->pd_lower = (loc * MAXALIGN(sizeof(ItemIdData))) + head;
        Assert(ph->pd_lower < ph->pd_upper);
}

bool PageIsEmpty(Page page)
{
        PageHeader ph = (PageHeader)page;
        long tail = ph->pd_lower;
        long head = (((unsigned long)&(ph->pd_linp)) - ((unsigned long)ph));  
        if ( head >= tail ) {
            return true;
        } else {
            return false;
        }
}

crc64 PageInsertChecksum(Page page)
{
    PageHeader ph = (PageHeader)page;
    long len = BLCKSZ;

    if ( disable_crc ) {
        ph->checksum = InvalidCRC64;
        return ph->checksum;
    } else {
        len = len - ((unsigned long)&ph->pd_linp - (unsigned long)page);
        ph->checksum = (uint64)checksum_block((unsigned char*)&ph->pd_linp,len); 
        return (crc64)ph->checksum;
    }
}

crc64 PageInsertInvalidChecksum(Page page)
{
    PageHeader ph = (PageHeader)page;
    ph->checksum = InvalidCRC64;
    return ph->checksum;
}

bool PageConfirmChecksum(Page page)
{
    crc64 checksum,confirm;
    PageHeader ph = (PageHeader)page;
    
    if ( disable_crc ) return true;

    if ( ph->checksum == InvalidCRC64 || ph->checksum == InitCRC64 ) return true;

    int len = BLCKSZ;
    len = len - ((unsigned long)&ph->pd_linp - (unsigned long)page);
    checksum = ph->checksum;
    confirm = (crc64)checksum_block((unsigned char*)&ph->pd_linp,len); 
    return eq_crc64(checksum,confirm);
}

bool PageChecksumIsInvalid(Page page) {
    PageHeader ph = (PageHeader)page;
    return ph->checksum == InvalidCRC64;    
}

bool PageChecksumIsInit(Page page) {
    PageHeader ph = (PageHeader)page;
    return ph->checksum == InitCRC64;
}