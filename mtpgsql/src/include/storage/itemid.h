/*-------------------------------------------------------------------------
 *
 * itemid.h
 *	  Standard POSTGRES buffer page item identifier definitions.
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * $Id: itemid.h,v 1.1.1.1 2006/08/12 00:22:24 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */
#ifndef ITEMID_H
#define ITEMID_H

typedef uint32 ItemOffset;
typedef uint16 ItemLength;

typedef bits16 ItemIdFlags;



typedef struct ItemIdData
{								/* line pointers */
	uint32	lp_off;		/* offset to find tup */
	/* can be reduced by 2 if necc. */
	uint16	lp_len;		/* length of tuple */
	unsigned char	lp_flags;		/* flags on tuple */
	unsigned char	lp_overflow;		/* flags on tuple */
} ItemIdData;

typedef struct ItemIdData *ItemId;

#ifndef LP_USED
#define LP_USED			0x01	/* this line pointer is being used */
#endif

/* ----------------
 *		support macros
 * ----------------
 */
/*
 *		ItemIdGetLength
 */
#define ItemIdGetLength(itemId) \
   ((itemId)->lp_len)

/*
 *		ItemIdGetOffset
 */
#define ItemIdGetOffset(itemId) \
   ((itemId)->lp_off)

/*
 *		ItemIdGetFlags
 */
#define ItemIdGetFlags(itemId) \
   ((itemId)->lp_flags)

/*
 * ItemIdIsValid
 *		True iff disk item identifier is valid.
 */
#define ItemIdIsValid(itemId)	PointerIsValid(itemId)

/*
 * ItemIdIsUsed
 *		True iff disk item identifier is in use.
 *
 * Note:
 *		Assumes disk item identifier is valid.
 */
#define ItemIdIsUsed(itemId) \
( \
	AssertMacro(ItemIdIsValid(itemId)), \
	(bool) ((itemId)->lp_flags & LP_USED) \
)

#endif	 /* ITEMID_H */
