/*-------------------------------------------------------------------------
 *
 * transsup.c
 *	  postgres transaction access method support code
 *
 * Portions Copyright (c) 2000-2024, Myron Scott  <myron@weaverdb.org>
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *
 *
 * NOTES
 *	  This file contains support functions for the high
 *	  level access method interface routines found in transam.c
 *
 *-------------------------------------------------------------------------
 */

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

static const char *
xid_status_name(XidStatus status)
{
	switch (status)
	{
		case XID_INPROGRESS:
			return "inprogress(0)";
		case XID_ABORT:
			return "abort(1)";
		case XID_SOFT_COMMIT:
			return "soft_commit(2)";
		case XID_COMMIT:
			return "commit(3)";
		default:
			return "unknown";
	}
}

static size_t
xid_status_tb_size(void)
{
	unsigned long seg = 0;

	return sizeof(seg) * 4;
}

static unsigned long
xid_status_shift(Index index, size_t tb_size)
{
	return ((tb_size * 2) - 2) - ((index % tb_size) * 2);
}

static XidStatus
xid_status_from_word(unsigned long word, Index index, size_t tb_size)
{
	const unsigned long mask = 3;

	return (XidStatus) ((word >> xid_status_shift(index, tb_size)) & mask);
}

static bool
xid_shares_ami_page(TransactionId transactionId)
{
	unsigned long itemsPerBlock = TP_NumXidStatusPerBlock;

	if (ShmemVariableCache != NULL
		&& ShmemVariableCache->xid_low_water_mark > AmiTransactionId)
		return false;
	return (transactionId / itemsPerBlock) == (AmiTransactionId / itemsPerBlock);
}

static XidStatus
TransPageGetXidStatus(Block tblock, TransactionId transactionId)
{
	Index		index;
	volatile unsigned long *finder;
	size_t		tb_size = xid_status_tb_size();

	if (tblock == NULL)
		return XID_INPROGRESS;
	index = transactionId % TP_NumXidStatusPerBlock;
	finder = ((volatile unsigned long *) tblock + (index / tb_size));
	return xid_status_from_word(*finder, index, tb_size);
}

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
	XidStatus	xstatus;

/*  Lock this just to see if the problems with weird updates goes away  */
	S_LOCK(&SLockArray[XidSetLockId]);
	xstatus = TransPageGetXidStatus(tblock, transactionId);
	S_UNLOCK(&SLockArray[XidSetLockId]);

	return xstatus;
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
	unsigned long			updated = 0;
	unsigned long                   mask = 3;
	unsigned long 			shift = 0;
	size_t                  tb_size = xid_status_tb_size();
	XidStatus				oldstatus;
	XidStatus				newstatus;
	XidStatus				old_ami;
	XidStatus				new_ami;
	Index					amiIndex;
	bool					protect_ami;

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
			return;
	}



	shift = ((index % tb_size) * 2);
	seg <<= (((tb_size * 2 ) - 2) - shift);
	mask <<= (((tb_size * 2 ) - 2) - shift);
	finder = ((volatile unsigned long*)tblock + (index / tb_size));
	amiIndex = AmiTransactionId % TP_NumXidStatusPerBlock;
	protect_ami = xid_shares_ami_page(transactionId)
		&& (index / tb_size) == (amiIndex / tb_size);

	S_LOCK(&SLockArray[XidSetLockId]);	

	ref = *finder;
	oldstatus = xid_status_from_word(ref, index, tb_size);
	old_ami = xid_status_from_word(ref, amiIndex, tb_size);

        /*  check to see that no mutually exclusive state 
            has already been set 
        */
/*  erase the right bits  */
            updated = (ref & ~(mask)) | seg;
	newstatus = xid_status_from_word(updated, index, tb_size);
	new_ami = xid_status_from_word(updated, amiIndex, tb_size);

	if (protect_ami && old_ami != XID_INPROGRESS && new_ami == XID_INPROGRESS)
	{
		S_UNLOCK(&SLockArray[XidSetLockId]);
		elog(FATAL, "Ami xid 512 overwritten with 0 by xid %llu (%s -> %s) word %lx -> %lx",
			 (unsigned long long) transactionId,
			 xid_status_name(oldstatus),
			 xid_status_name(newstatus),
			 ref, updated);
		return;
	}

/*  write the long section to the block */
            *finder = updated;

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

