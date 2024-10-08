/*-------------------------------------------------------------------------
 *
 * tupmacs.h
 *	  Tuple macros used by both index tuples and heap tuples.
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
#ifndef TUPMACS_H
#define TUPMACS_H

#include "utils/memutils.h"

/*
 * check to see if the ATT'th bit of an array of 8-bit bytes is set.
 */
#define att_isnull(ATT, BITS) (!((BITS)[(ATT) >> 3] & (1 << ((ATT) & 0x07))))

/*
 * given a Form_pg_attribute and a pointer into a tuple's data
 * area, return the correct value or pointer.
 *
 * We return a 4 byte (char *) value in all cases.	If the attribute has
 * "byval" false or has variable length, we return the same pointer
 * into the tuple data area that we're passed.  Otherwise, we return
 * the 1, 2, or 4 bytes pointed to by it, properly extended to 4
 * bytes, depending on the length of the attribute.
 *
 * note that T must already be properly LONGALIGN/SHORTALIGN'd for
 * this to work correctly.
 *
 * the double-cast is to stop gcc from (correctly) complaining about
 * casting integer types with size < sizeof(char *) to (char *).
 * sign-extension may get weird if you use an integer type that
 * isn't the same size as (char *) for the first cast.  (on the other
 * hand, it's safe to use another type for the (foo *)(T).)
 *
 * attbyval seems to be fairly redundant.  We have to return a pointer if
 * the value is longer than 4 bytes or has variable length; returning the
 * value would be useless.	In fact, for at least the variable length case,
 * the caller assumes we return a pointer regardless of attbyval.
 * I would eliminate attbyval altogether, but I don't know how.  -BRYANH.
 */
 #ifdef NOTUSED
 
#define fetchatt(A, T) \
( \
	((*(A))->attbyval && (*(A))->attlen != -1) ? \
	( \
		((*(A))->attlen > (int) sizeof(int16)) ? \
		( \
            ((*(A))->attlen == (int) sizeof(long)) ? \
                (Datum) *((long *)(T)) : \
                (Datum) *((int32 *)(T)) \
		) \
		: \
		( \
			((*(A))->attlen < (int) sizeof(int16)) ? \
				(Datum) *((char *)(T)) \
			: \
				(Datum) *((int16 *)(T))) \
		) \
	: \
	(Datum)(T) \
)

#endif

#define fetchatt  HeapFetchAtt

/* att_align aligns the given offset as needed for a datum of length attlen
 * and alignment requirement attalign.	In practice we don't need the length.
 * The attalign cases are tested in what is hopefully something like their
 * frequency of occurrence.
 */
#define att_align(cur_offset, attlen, attalign) \
( \
	((attalign) == 'i') ? INTALIGN(cur_offset) : \
	 (((attalign) == 'l') ? LONGALIGN(cur_offset) : \
	  (((attalign) == 'c') ? ((long)(cur_offset)) : \
	   (((attalign) == 'd') ? DOUBLEALIGN(cur_offset) : \
		( \
			AssertMacro((attalign) == 's'), \
			SHORTALIGN(cur_offset) \
		)))) \
)

#define att_addlength(cur_offset, attlen, attval) \
( \
	((attlen) != -1) ? \
	( \
		(cur_offset) + (attlen) \
	) \
	: \
	( \
		(cur_offset) + VARATT_SIZE(DatumGetPointer(attval)) \
	) \
)

#endif
