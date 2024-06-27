/*-------------------------------------------------------------------------
 *
 * block.h
 *	  POSTGRES disk block definitions.
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * $Id: block.h,v 1.1.1.1 2006/08/12 00:22:23 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */
#ifndef BLOCK_H
#define BLOCK_H

/*
 * BlockNumber:
 *
 * each data file (heap or index) is divided into postgres disk blocks
 * (which may be thought of as the unit of i/o -- a postgres buffer
 * contains exactly one disk block).  the blocks are numbered
 * sequentially, 0 to 0xFFFFFFFE.
 *
 * InvalidBlockNumber is the same thing as P_NEW in buf.h.
 *
 * the access methods, the buffer manager and the storage manager are
 * more or less the only pieces of code that should be accessing disk
 * blocks directly.
 */
typedef unsigned long  BlockNumber;

#ifdef _LP64
#define InvalidBlockNumber		((BlockNumber) 0x0000FFFFFFFFFFFF)
#define EndBlockNumber			((BlockNumber) 0x0000FFFFFFFFFFFE)
#else
#define InvalidBlockNumber		((BlockNumber) 0xFFFFFFFF)
#define EndBlockNumber			((BlockNumber) 0xFFFFFFFE)
#endif

/*
 * BlockId:
 *
 * this is a storage type for BlockNumber.	in other words, this type
 * is used for on-disk structures (e.g., in HeapTupleData) whereas
 * BlockNumber is the type on which calculations are performed (e.g.,
 * in access method code).
 *
 * there doesn't appear to be any reason to have separate types except
 * for the fact that BlockIds can be SHORTALIGN'd (and therefore any
 * structures that contains them, such as ItemPointerData, can also be
 * SHORTALIGN'd).  this is an important consideration for reducing the
 * space requirements of the line pointer (ItemIdData) array on each
 * page and the header of each heap or index tuple, so it doesn't seem
 * wise to change this without good reason.
 */
 
typedef BlockNumber BlockIdData;

typedef BlockIdData *BlockId;	/* block identifier */

/* ----------------
 *		support macros
 * ----------------
 */

/*
 * BlockNumberIsValid
 *		True iff blockNumber is valid.
 */
#define BlockNumberIsValid(blockNumber) \
	((bool) ((BlockNumber) (blockNumber) != InvalidBlockNumber))

/*
 * BlockIdIsValid
 *		True iff the block identifier is valid.
 */
#define BlockIdIsValid(blockId) \
	((bool) PointerIsValid(blockId))


#define BlockIdSet(blockId,blockNumber) \
( \
    *blockId = blockNumber \
)

#define BlockIdCopy(toBlockId,fromBlockId) \
( \
    *toBlockId = *fromBlockId \
)

#define BlockIdEquals(blockId1,blockId2) \
    ( *blockId1 == *blockId2 )

#define BlockIdGetBlockNumber(blockId) \
(	((BlockNumber)*blockId)  )

#endif	 /* BLOCK_H */
