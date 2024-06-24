/*-------------------------------------------------------------------------
 *
 * transsup.c
 *	  postgres transaction access method support code
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  $Header: /cvs/weaver/mtpgsql/src/backend/access/transam/transsup.c,v 1.3 2007/02/24 02:58:47 synmscott Exp $
 *
 * NOTES
 *	  This file contains support functions for the high
 *	  level access method interface routines found in transam.c
 *
 *-------------------------------------------------------------------------
 */

#include <signal.h>
#include <string.h>


#include "postgres.h"

#include "env/env.h"
#include "access/xact.h"
#include "utils/bit.h"
#include "access/transam.h"
#include "storage/smgr.h"
#include "catalog/catname.h"
#include "utils/relcache.h"
#include "storage/m_lock.h"

/* defined in varsup.c    */
extern VariableCache ShmemVariableCache;
/* defined in tramsam.c  */
static XidStatus
TransBlockGetXidStatus(Block tblock,TransactionId transactionId);
/* ----------------------------------------------------------------
 *					  general support routines
 * ----------------------------------------------------------------
 */


SPINLOCK	XidSetLockId;

/* --------------------------------
 *		TransComputeBlockNumber
 * --------------------------------
 */
BlockNumber
TransComputeBlockNumber(Relation relation,		/* relation to test */
						TransactionId transactionId)
{
	BlockNumber		compublock = InvalidBlockNumber;
        unsigned long		itemsPerBlock = 0;
	unsigned long 		blocksToRemove = 0;
	unsigned long		absoluteBlocks = 0;

	/* ----------------
	 *	we calculate the block number of our transaction
	 *	by dividing the transaction id by the number of
	 *	transaction things per block.
	 * ----------------
	 */
	if (strcmp(LogRelationName,relation->rd_rel->relname.data) == 0)
		itemsPerBlock = TP_NumXidStatusPerBlock;
	else
		elog(ERROR, "TransComputeBlockNumber: unknown relation");

	/* ----------------
	 *	warning! if the transaction id's get too large
	 *	then a BlockNumber may not be large enough to hold the results
	 *	of our division.
	 *
	 *	XXX  this will all vanish soon when we implement an improved
	 *		 transaction id schema -cim 3/23/90
	 *
	 *	This has vanished now that xid's are 4 bytes (no longer 5).
	 *	-mer 5/24/92
	 * ----------------
	 */
	 blocksToRemove = (unsigned long)(ShmemVariableCache->xid_low_water_mark / itemsPerBlock);
	 if ( blocksToRemove < 0 ) elog(FATAL,"negative blocks to remove in pg_log code: %d",998);
	 
	absoluteBlocks = ((unsigned long)((transactionId) / itemsPerBlock));
	compublock = (absoluteBlocks - blocksToRemove);
	if ((absoluteBlocks - blocksToRemove) < 0 ) {
		elog(FATAL,"transaction id %llu block number %lu",(transactionId),compublock);
	}
	if ( compublock > 8192 ) {
		elog(NOTICE,"Transaction Log is very large vacuum all databases soon. transaction id %llu block number %lu",(transactionId),compublock);
        }
        return compublock;
}


/* --------------------------------
 *		TransBlockGetXidStatus
 *
 *		This returns the status of the desired transaction
 * --------------------------------
 */
 
XidStatus
TransBlockGetXidStatus(Block tblock,TransactionId transactionId)
{
	Index		index;
	volatile unsigned long  *   finder;
	unsigned long		seg;
        size_t                  tb_size = sizeof(seg) * 4;
   const unsigned long            mask = 3;

/*  Lock this just to see if the problems with weird updates goes away  */
	S_LOCK(&SLockArray[XidSetLockId]);   

	/* ----------------
	 *	calculate the index into the transaction data where
	 *	our transaction status is located
	 *
	 *	XXX this will be replaced soon when we move to the
	 *		new transaction id scheme -cim 3/23/90
	 *
	 *	The old system has now been replaced. -mer 5/24/92
	 * ----------------
	 */
	index = transactionId % TP_NumXidStatusPerBlock;
	seg = index / tb_size;
	finder = ((volatile unsigned long*)tblock + seg);
	seg = *finder;

	seg >>= (((tb_size * 2) - 2) - ((index % tb_size) * 2));
	seg = seg & mask;

	S_UNLOCK(&SLockArray[XidSetLockId]);   

	return (XidStatus) seg;
}

/* --------------------------------
 *		TransBlockSetXidStatus
 *
 *		This sets the status of the desired transaction
 * --------------------------------
 */
void
TransBlockSetXidStatus(Block tblock,
					   TransactionId transactionId,
					   XidStatus xstatus)
{
	Index                   index;
	volatile unsigned long  *       finder;
	unsigned long 			seg = 0;
	unsigned long 			ref = 0;
	unsigned long                   mask = 3;
	unsigned long 			shift = 0;
    size_t                  tb_size = sizeof(seg) * 4;

	/* ----------------
	 *	calculate the index into the transaction data where
	 *	we sould store our transaction status.
	 *
	 *	XXX this will be replaced soon when we move to the
	 *		new transaction id scheme -cim 3/23/90
	 *
	 *	The new scheme is here -mer 5/24/92
	 * ----------------
	 */
	index = transactionId % TP_NumXidStatusPerBlock;

	
	switch (xstatus)
	{
		case XID_SOFT_COMMIT:		/* set 10 */
                        mask = 0;  /*  don't erase any bits */
                        seg = 2;
			break;
		case XID_ABORT: /* set 01 */
                        seg = 1;
			break;
		case XID_INPROGRESS:	/* set 00 */
                        seg = 0;
			break;
		case XID_COMMIT:	/* set 11 */
                        seg = 3;       
			break;
		default:
			elog(NOTICE,
				 "TransBlockSetXidStatus: invalid status: %d (ignored)",
				 xstatus);
			break;
	}



	shift = ((index % tb_size) * 2);
	seg <<= (((tb_size * 2 ) - 2) - shift);
	mask <<= (((tb_size * 2 ) - 2) - shift);
	finder = ((volatile unsigned long*)tblock + (index / tb_size));

	S_LOCK(&SLockArray[XidSetLockId]);	

	ref = *finder;

        /*  check to see that no mutually exclusive state 
            has already been set 
        */
/*  erase the right bits  */
            ref &= ~(mask);
/*  write the new values to the right bits */
            ref |= seg;
/*  write the long section to the block */
            *finder = ref;

	S_UNLOCK(&SLockArray[XidSetLockId]);
}

/* ----------------------------------------------------------------
 *				   transam i/o support routines
 * ----------------------------------------------------------------
 */

/* --------------------------------
 *		TransBlockNumberGetXidStatus
 * --------------------------------
 */
 
XidStatus
TransBlockNumberGetXidStatus(Relation relation,TransactionId xid,bool *failP)
{
	Buffer		buffer;			/* buffer associated with block */
	volatile Block		block;			/* block containing xstatus */
	XidStatus	xstatus;		/* recorded status of xid */
	bool		localfail;		/* bool used if failP = NULL */
	BlockNumber	blockNumber;

	if ( !TransactionIdIsValid(xid) ) elog(ERROR,"testing invalid transaction id");

	if ( ShmemVariableCache->xid_low_water_mark > xid ) return XID_COMMIT;

	/* ----------------
	 *	get the page containing the transaction information
	 * ----------------
	 */

        blockNumber = TransComputeBlockNumber(relation,xid);
	if ( blockNumber > 32 * 1024) {
		return XID_ABORT;
	}
	buffer = ReadBuffer(relation, blockNumber);
/*        LockBuffer(relation,buffer, BUFFER_LOCK_SHARE);     */
        if (!BufferIsValid(buffer) ) elog(ERROR,"bad buffer read in transaction management");
	block = BufferGetBlock(buffer);

	/* ----------------
	 *	get the status from the block.	note, for now we always
	 *	return false in failP.
	 * ----------------
	 */
	if (failP == NULL)
		failP = &localfail;
	(*failP) = false;

	xstatus = TransBlockGetXidStatus(block, xid);

	/* ----------------
	 *	release the buffer and return the status
	 * ----------------
	 */
/*        LockBuffer(relation,buffer, BUFFER_LOCK_UNLOCK);        */
	ReleaseBuffer(relation,buffer);

	return xstatus;
}

/* --------------------------------
 *		TransBlockNumberSetXidStatus
 * --------------------------------
 */

void
TransBlockNumberSetXidStatus(Relation relation,TransactionId xid,XidStatus xstatus)
{
	Buffer		buffer;			/* buffer associated with block */
	Block		block;			/* block containing xstatus */
	BlockNumber	blockNumber;
        
 	blockNumber = TransComputeBlockNumber(relation,xid);

	buffer = ReadBuffer(relation, blockNumber);
        if (!BufferIsValid(buffer) ) elog(ERROR,"bad buffer read in transaction management");
/*  why lock this buffer, transaction ops should be atomic, we are only checking 2 bits  */
	block = BufferGetBlock(buffer);


	TransBlockSetXidStatus(block, xid, xstatus);

        if ( !IsMultiuser() ) {
            FlushBuffer(relation,buffer);	
        } else {
            WriteBuffer(relation,buffer);	
        }
}

