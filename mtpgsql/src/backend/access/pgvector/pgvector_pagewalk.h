/*-------------------------------------------------------------------------
 *
 * Bound HNSW/IVF page-chain walks after mid-write crashes.
 * Torn nextblkno / neighbor TIDs must not ReadBuffer out of range
 * (that extends the file) or follow cycles.
 *
 *-------------------------------------------------------------------------
 */
#ifndef PGVECTOR_PAGEWALK_H
#define PGVECTOR_PAGEWALK_H

#include "env/freespace.h"
#include "storage/block.h"
#include "storage/off.h"
#include "utils/rel.h"

static inline bool
PgvectorBlockInRange(Relation rel, BlockNumber blkno)
{
	return BlockNumberIsValid(blkno) &&
		blkno < RelationGetNumberOfBlocks(rel);
}

static inline BlockNumber
PgvectorSafeNextBlkno(Relation rel, BlockNumber current, BlockNumber next)
{
	if (!BlockNumberIsValid(next) || next == current)
		return InvalidBlockNumber;
	if (!PgvectorBlockInRange(rel, next))
		return InvalidBlockNumber;
	return next;
}

static inline OffsetNumber
PgvectorClampMaxOff(OffsetNumber maxoff)
{
	return maxoff > MaxOffsetNumber ? MaxOffsetNumber : maxoff;
}

#endif							/* PGVECTOR_PAGEWALK_H */
