/*-------------------------------------------------------------------------
 *
 * attnum.h
 *	  POSTGRES attribute number definitions.
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
#ifndef ATTNUM_H
#define ATTNUM_H

#include "c.h"

/*
 * user defined attribute numbers start at 1.	-ay 2/95
 */

typedef int16 AttrNumber;

#define InvalidAttrNumber		0

/* ----------------
 *		support macros
 * ----------------
 */
/*
 * AttributeNumberIsValid
 *		True iff the attribute number is valid.
 */
#define AttributeNumberIsValid(attributeNumber) \
	((bool) ((attributeNumber) != InvalidAttrNumber))

/*
 * AttrNumberIsForUserDefinedAttr
 *		True iff the attribute number corresponds to an user defined attribute.
 */
#define AttrNumberIsForUserDefinedAttr(attributeNumber) \
	((bool) ((attributeNumber) > 0))

/*
 * AttrNumberGetAttrOffset
 *		Returns the attribute offset for an attribute number.
 *
 * Note:
 *		Assumes the attribute number is for an user defined attribute.
 */
#define AttrNumberGetAttrOffset(attNum) \
( \
	AssertMacro(AttrNumberIsForUserDefinedAttr(attNum)), \
	((attNum) - 1) \
)

/*
 * AttributeOffsetGetAttributeNumber
 *		Returns the attribute number for an attribute offset.
 */
#define AttrOffsetGetAttrNumber(attributeOffset) \
	 ((AttrNumber) (1 + attributeOffset))

#endif	 /* ATTNUM_H */
