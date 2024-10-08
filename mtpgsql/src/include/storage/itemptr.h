/*-------------------------------------------------------------------------
 *
 * itemptr.h
 *	  POSTGRES disk item pointer definitions.
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 *
 *-------------------------------------------------------------------------
 */
#ifndef ITEMPTR_H
#define ITEMPTR_H

#include "storage/block.h"
#include "storage/off.h"

/*
 * ItemPointer:
 *
 * this is a pointer to an item on another disk page in the same file.
 * blkid tells us which block, posid tells us which entry in the linp
 * (ItemIdData) array we want.
 */
/*  endian and 64-bit hacks MKS  7.12.2002  
    BlockNumber is 32bits for 32bit machines and 64bits for 64bit machines
*/
 #ifdef _LP64
typedef union ItemPointerData
{
	BlockIdData ip_blkid;
	OffsetNumber ip_posid;
} ItemPointerData;

#define OffsetNumberMask		(0xffff)		/* valid uint16 bits */
#define BlockNumberMask		(0x0000ffffffffffffL)
	
#if BYTE_ORDER == LITTLE_ENDIAN  

#define BlockNumberShift   16

#elif BYTE_ORDER == BIG_ENDIAN

#define BlockNumberShift   0

#endif

#else
typedef struct ItemPointerData
{
	BlockIdData ip_blkid;
	OffsetNumber ip_posid;
} ItemPointerData;

#define OffsetNumberMask		(0xffff)		/* valid uint16 bits */
#define BlockNumberMask		(0xffffffff)		/* valid uint16 bits */
#define BlockNumberShift	0
#endif

#define TIDSIZE		sizeof(ItemPointerData)

typedef ItemPointerData *ItemPointer;
/* ----------------
 *		support macros
 * ----------------
 */

/*
 * ItemPointerIsValid
 *		True iff the disk item pointer is not NULL.
 */
#define ItemPointerIsValid(pointer) \
	((bool) (PointerIsValid(pointer) && (((pointer)->ip_posid & OffsetNumberMask) != 0)))

/*
 * ItemPointerGetBlockNumber
 *		Returns the block number of a disk item pointer.
 */
#define ItemPointerGetBlockNumber(pointer) \
( \
	((BlockIdGetBlockNumber(&(pointer)->ip_blkid)*1L) >> BlockNumberShift) & BlockNumberMask \
)

/*
 * ItemPointerGetOffsetNumber
 *		Returns the offset number of a disk item pointer.
 */
#define ItemPointerGetOffsetNumber(pointer) \
( \
	AssertMacro(ItemPointerIsValid(pointer)), \
	(pointer)->ip_posid & OffsetNumberMask \
)

#define ItemPointerGetOffsetUnchecked(pointer) \
( \
	(pointer)->ip_posid & OffsetNumberMask \
)
/*
 * ItemPointerSet
 *		Sets a disk item pointer to the specified block and offset.
 */
#define ItemPointerSet(pointer, blockNumber, offNum) \
( \
	AssertMacro(PointerIsValid(pointer)), \
	BlockIdSet(&((pointer)->ip_blkid), (1L*blockNumber) << BlockNumberShift), \
	(pointer)->ip_posid = offNum \
)
/* set item pointer but don't check from validity so offset can be set to zero
	for nbtree delete hack
*/
#define ItemPointerSetUnchecked(pointer, blockNumber, offNum) \
( \
	BlockIdSet(&((pointer)->ip_blkid), (1L*blockNumber) << BlockNumberShift), \
	(pointer)->ip_posid = offNum \
)
/*
 * ItemPointerCopy
 *		Copies the contents of one disk item pointer to another.
 */
#define ItemPointerCopy(fromPointer, toPointer) \
( \
	AssertMacro(PointerIsValid(toPointer)), \
	AssertMacro(PointerIsValid(fromPointer)), \
	*(toPointer) = *(fromPointer) \
)

/*
 * ItemPointerSetInvalid
 *		Sets a disk item pointer to be invalid.
 */
#define ItemPointerSetInvalid(pointer) \
( \
	AssertMacro(PointerIsValid(pointer)), \
	BlockIdSet(&((pointer)->ip_blkid), InvalidBlockNumber), \
	(pointer)->ip_posid = InvalidOffsetNumber \
)

/* ----------------
 *		PG_EXTERNs
 * ----------------
 */

PG_EXTERN bool ItemPointerEquals(ItemPointer pointer1, ItemPointer pointer2);

#endif	 /* ITEMPTR_H */
