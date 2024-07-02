/*-------------------------------------------------------------------------
 *
 * bit.h
 *	  Standard bit array definitions.
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 *
 *-------------------------------------------------------------------------
 */
#ifndef BIT_H
#define BIT_H

typedef bits8 *BitArray;
typedef uint32 BitIndex;

#define BitsPerByte		8

/*
 * BitArraySetBit
 *		Sets (to 1) the value of a bit in a bit array.
 */
PG_EXTERN void BitArraySetBit(BitArray bitArray, BitIndex bitIndex);

/*
 * BitArrayClearBit
 *		Clears (to 0) the value of a bit in a bit array.
 */
PG_EXTERN void BitArrayClearBit(BitArray bitArray, BitIndex bitIndex);

/*
 * BitArrayBitIsSet
 *		True iff the bit is set (1) in a bit array.
 */
PG_EXTERN bool BitArrayBitIsSet(BitArray bitArray, BitIndex bitIndex);

#endif	 /* BIT_H */
