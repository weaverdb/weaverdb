/*-------------------------------------------------------------------------
 *
 * bufmgr.h
 *	  POSTGRES buffer manager definitions.
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * $Id: bufmgr.h,v 1.8 2007/05/23 15:40:04 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */
#ifndef BUFMGR_H
#define BUFMGR_H

#include "storage/ipc.h"
#include "storage/block.h"
#include "storage/buf.h"
#include "storage/buf_internals.h"
#include "utils/rel.h"

/*
 * the maximum size of a disk block for any possible installation.
 *
 * in theory this could be anything, but in practice this is actually
 * limited to 2^13 bytes because we have limited ItemIdData.lp_off and
 * ItemIdData.lp_len to 13 bits (see itemid.h).
 *
 * limit is now 2^15.  Took four bits from ItemIdData.lp_flags and gave
 * two apiece to ItemIdData.lp_len and lp_off. darrenk 01/06/98
 *
 */

#define MAXBLCKSZ		32768

typedef void* Block;

/* special pageno for bget */
#define P_NEW	InvalidBlockNumber		/* grow the file to get a new page */

typedef bits16 BufferLock;


/**********************************************************************

  the rest is function defns in the bufmgr that are externally callable

 **********************************************************************/

/*
 * These routines are beaten on quite heavily, hence the macroization.
 * See buf_internals.h for a related comment.
 */
#define BufferDescriptorGetBuffer(bdesc) ((bdesc)->buf_id + 1)

PG_EXTERN int	ShowPinTrace;

/*
 * Buffer context lock modes
 */
#define BUFFER_LOCK_UNLOCK			0
#define BUFFER_LOCK_SHARE			1
#define BUFFER_LOCK_EXCLUSIVE                   2
#define BUFFER_LOCK_REF_EXCLUSIVE               4

/*  bit flag set if lock should fail instead of block  */
#define BUFFER_LOCK_UPGRADE			8	
#define BUFFER_LOCK_WRITEIO                     16   /*  blocks exclusive locks until write io is finished */


/*
 * BufferIsValid
 *		True iff the given buffer number is valid (either as a shared
 *		or local buffer).
 *
 * Note:
 *		BufferIsValid(InvalidBuffer) is False.
 *		BufferIsValid(UnknownBuffer) is False.
 *
 * Note: For a long time this was defined the same as BufferIsPinned,
 * that is it would say False if you didn't hold a pin on the buffer.
 * I believe this was bogus and served only to mask logic errors.
 * Code should always know whether it has a buffer reference,
 * independently of the pin state.
 */
#define BufferIsValid(bufnum) \
( \
	BufferIsLocal(bufnum) ? \
		((bufnum) >= -NLocBuffer) \
	: \
		(! BAD_BUFFER_ID(bufnum)) \
)

/*
 * BufferIsPinned
 *		True iff the buffer is pinned (also checks for valid buffer number).
 *
 *		NOTE: what we check here is that *this* backend holds a pin on
 *		the buffer.  We do not care whether some other backend does.
 */
#define BufferIsPinned(bufnum) \
( \
	BufferIsLocal(bufnum) ? \
		((bufnum) >= -NLocBuffer && GetBufferEnv()->LocalRefCount[-(bufnum) - 1] > 0) \
	: \
	( \
		BAD_BUFFER_ID(bufnum) ? \
			false \
		: \
			(GetBufferEnv()->PrivateRefCount[(bufnum) - 1] > 0) \
	) \
)


/*
 * prototypes for functions in bufmgr.c
 */
 
 #ifdef __cplusplus
 extern "C" {
 #endif

PG_EXTERN Buffer ReadBuffer(Relation reln, BlockNumber blockNum);
PG_EXTERN int	WriteBuffer(Relation reln, Buffer buffer);
PG_EXTERN int	WriteNoReleaseBuffer(Relation reln, Buffer buffer);     
PG_EXTERN Buffer ReleaseAndReadBuffer(Buffer buffer, Relation relation,
					 BlockNumber blockNum);
PG_EXTERN int	ReleaseBuffer(Relation reln, Buffer buffer);

PG_EXTERN int	FlushBuffer(Relation reln,Buffer buffer, bool release);
PG_EXTERN int	PrivateWriteBuffer(Relation rel, Buffer buffer, bool release);
PG_EXTERN int	SyncRelation(Relation rel);

PG_EXTERN void InitBufferPool(IPCKey key);
PG_EXTERN void InitThreadBuffer(void);

PG_EXTERN void ResetBufferPool(bool isCommit);
PG_EXTERN int	BufferPoolCheckLeak(void);
PG_EXTERN int BufferPoolCountHolds();

PG_EXTERN BlockNumber BufferGetBlockNumber(Buffer buffer);
/*
PG_EXTERN int	FlushRelationBuffers(Relation rel, BlockNumber firstDelBlock);
*/
PG_EXTERN void InvalidateRelationBuffers(Relation rel);

PG_EXTERN void DropBuffers(Oid dbid);
PG_EXTERN void PrintPinnedBufs(void);
PG_EXTERN int	BufferShmemSize(void);

PG_EXTERN int BiasBuffer(Relation rel, Buffer buffer);

PG_EXTERN void SetBufferCommitInfoNeedsSave(Buffer buffer);

PG_EXTERN void UnlockBuffers(void);
PG_EXTERN int LockBuffer(Relation rel, Buffer buffer, int mode);

PG_EXTERN bool BufferHasError(Buffer buf);

PG_EXTERN void AbortBufferIO(void);
PG_EXTERN bool ErrorBufferIO(BufferDesc* buf);
PG_EXTERN bool IsDirtyBufferIO(BufferDesc* buf);
        
PG_EXTERN bool ReadBufferIO(BufferDesc *buf);
PG_EXTERN bool WriteBufferIO(BufferDesc *buf, bool flush);
PG_EXTERN bool LogBufferIO(BufferDesc *buf);
PG_EXTERN void TerminateBufferIO(BufferDesc *buf);

PG_EXTERN Block BufferGetBlock(Buffer buffer);

PG_EXTERN void IncrBufferRefCount(Relation rel, Buffer buffer);
PG_EXTERN bool CheckBufferId(BufferDesc* buf, BlockNumber block, Oid relid, Oid dbid);
PG_EXTERN BufferCxt GetBufferCxt();


 #ifdef __cplusplus
}
 #endif


#endif
