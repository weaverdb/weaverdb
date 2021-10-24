/*-------------------------------------------------------------------------
 *
 * transam.c
 *	  postgres transaction log/time interface routines
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  $Header: /cvs/weaver/mtpgsql/src/backend/access/transam/transam.c,v 1.2 2006/08/13 00:39:27 synmscott Exp $
 *
 * NOTES
 *	  This file contains the high level access-method interface to the
 *	  transaction system.
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include "env/env.h"

#include "access/heapam.h"
#include "catalog/catname.h"
#include "utils/relcache.h"
#include "storage/sinval.h"
#include "env/dbwriter.h"

static int RecoveryCheckingEnabled(void);
static void TransRecover(Relation logRelation);
static bool TransactionLogTest(TransactionInfo* env,TransactionId transactionId, XidStatus status);
static void TransactionLogUpdate(TransactionId transactionId,XidStatus status);
/* ----------------
 *	  global variables holding pointers to relations used
 *	  by the transaction system.  These are initialized by
 *	  InitializeTransactionLog().
 * ----------------
 */
/*
Relation	LogRelation = (Relation) NULL;
static Relation	VariableRelation = (Relation) NULL;   
*/


bool TransactionSystemInitialized = false;
/* ----------------
 *		global variables holding cached transaction id's and statuses.
 * ----------------
 */
/*
*    Moved to env MKS 7/30/2000
*
*TransactionId cachedTestXid;
*XidStatus	cachedTestXidStatus;
*/
/* ----------------
 *		transaction system constants
 * ----------------
 */
/* ----------------------------------------------------------------
 *		transaction system constants
 *
 *		read the comments for GetNewTransactionId in order to
 *		understand the initial values for AmiTransactionId and
 *		FirstTransactionId. -cim 3/23/90
 * ----------------------------------------------------------------
 */
TransactionId NullTransactionId = (TransactionId) 0;

TransactionId AmiTransactionId = (TransactionId) 512;

TransactionId FirstTransactionId = (TransactionId) 514;

/* ----------------
 *		transaction recovery state variables
 *
 *		When the transaction system is initialized, we may
 *		need to do recovery checking.  This decision is decided
 *		by the postmaster or the user by supplying the backend
 *		with a special flag.  In general, we want to do recovery
 *		checking whenever we are running without a postmaster
 *		or when the number of backends running under the postmaster
 *		goes from zero to one. -cim 3/21/90
 * ----------------
 */


int			RecoveryCheckingEnableState = 0;


/* ------------------
 *		spinlock for oid generation
 * -----------------
 */
/*  Moved to env MKS   7/30/2000
*
*
*
*
*/
extern int	OidGenLockId;
/* ----------------
 *		recovery checking accessors
 * ----------------
 */
static int
RecoveryCheckingEnabled(void)
{
	return RecoveryCheckingEnableState;
}


void
SetRecoveryCheckingEnabled(bool state)
{
	RecoveryCheckingEnableState = (state == true);
}



/* ----------------------------------------------------------------
 *		postgres log access method interface
 *
 *		TransactionLogTest
 *		TransactionLogUpdate
 *		========
 *		   these functions do work for the interface
 *		   functions - they search/retrieve and append/update
 *		   information in the log and time relations.
 * ----------------------------------------------------------------
 */

/* --------------------------------
 *		TransactionLogTest
 * --------------------------------
 */

static bool						/* true/false: does transaction id have
								 * specified status? */
TransactionLogTest(TransactionInfo* env, TransactionId transactionId, /* transaction id to test */
				   XidStatus mask)	/* transaction status */
{
/*	BlockNumber blockNumber;     */
	volatile XidStatus	xidstatus;		/* recorded status of xid */
	bool		fail = false;		/* success/failure */
	/* ----------------
	 *	during initialization consider all transactions
	 *	as having been committed
	 * ----------------
	 */
	if (!TransactionSystemInitialized)
		return (bool)(mask == XID_COMMIT_TEST);

	if ( TransactionIdEquals(transactionId,InvalidTransactionId) ) {
		elog(DEBUG,"TransactionLogTest -- testing invalid transaction id");
		return (bool)(mask == XID_ABORT_TEST);
	}
	/* ----------------
	 *	 before going to the buffer manager, check our single
	 *	 item cache to see if we didn't just check the transaction
	 *	 status a moment ago.
	 * ----------------
	 */
	if (TransactionIdEquals(transactionId, env->cachedTestXid)) {
		xidstatus = env->cachedTestXidStatus;
        } else {
                /* ----------------
                 *	compute the item pointer corresponding to the
                 *	page containing our transaction id.  We save the item in
                 *	our cache to speed up things if we happen to ask for the
                 *	same xid's status more than once.
                 * ----------------
                 */
                if ( env->LogRelation == NULL || !RelationIsValid(env->LogRelation) ) {
                        Relation  logrelation = RelationNameGetRelation(LogRelationName,DEFAULTDBOID);
                        env->LogRelation = logrelation;
                        xidstatus = TransBlockNumberGetXidStatus(logrelation,transactionId,&fail);
                } else {
                        xidstatus = TransBlockNumberGetXidStatus(env->LogRelation,transactionId,&fail);
                }	
        }
	if (!fail)
	{
		/*
		 * DO NOT cache status for transactions in unknown state !!!
		 */
		if (xidstatus == XID_COMMIT || xidstatus == XID_ABORT)
		{
			env->cachedTestXid = transactionId;
			env->cachedTestXidStatus = xidstatus;
		}
                switch (mask) {
                    case XID_COMMIT_TEST:
                        return (bool)(xidstatus & 2);
                   case XID_ABORT_TEST:
                        return (bool)(xidstatus == XID_ABORT);             
                    case XID_HARD_COMMIT_TEST:
                        return (bool)(xidstatus == XID_COMMIT);
                    case XID_SOFT_COMMIT_TEST:
                        return (bool)(xidstatus == XID_SOFT_COMMIT);
                    case XID_INPROGRESS_TEST:
                        return (bool)(xidstatus == XID_INPROGRESS);
                    default:
                        elog(ERROR,"unknown xid test %d",mask);
                }
	}

	/* ----------------
	 *	  here the block didn't contain the information we wanted
	 * ----------------
	 */
	elog(ERROR, "TransactionLogTest: failed to get xidstatus");

	/*
	 * so lint is happy...
	 */
	return false;
}

/* --------------------------------
 *		TransactionLogUpdate
 * --------------------------------
 */
static void
TransactionLogUpdate(TransactionId transactionId,		/* trans id to update */
					 XidStatus status)	/* new trans status */
{
/*	BlockNumber blockNumber;    */

	TransactionInfo*	trans = GetTransactionInfo();
	
	/* ----------------
	 *	during initialization we don't record any updates.
	 * ----------------
	 */
	if (!TransactionSystemInitialized)
		return;

	/* ----------------
	 *	update the log relation
	 * ----------------
	 */
	if ( trans->LogRelation == NULL || !RelationIsValid(trans->LogRelation) ) {
		Relation logrelation = RelationNameGetRelation(LogRelationName,DEFAULTDBOID);
		trans->LogRelation = logrelation;
		TransBlockNumberSetXidStatus(logrelation,transactionId,status);
		RelationClose(logrelation);
	} else {
		TransBlockNumberSetXidStatus(trans->LogRelation,transactionId,status);
	}
	/*
	 * update (invalidate) our single item TransactionLogTest cache.
	 *
	 * if (status != XID_COMMIT)
	 *
	 * What's the hell ?! Why != XID_COMMIT ?!
	 */
	trans->cachedTestXid = transactionId;
	trans->cachedTestXidStatus = status;

}

/* ----------------------------------------------------------------
 *					 transaction recovery code
 * ----------------------------------------------------------------
 */

/* --------------------------------
 *		TransRecover
 *
 *		preform transaction recovery checking.
 *
 *		Note: this should only be preformed if no other backends
 *			  are running.	This is known by the postmaster and
 *			  conveyed by the postmaster passing a "do recovery checking"
 *			  flag to the backend.
 *
 *		here we get the last recorded transaction from the log,
 *		get the "last" and "next" transactions from the variable relation
 *		and then preform some integrity tests:
 *
 *		1) No transaction may exist higher then the "next" available
 *		   transaction recorded in the variable relation.  If this is the
 *		   case then it means either the log or the variable relation
 *		   has become corrupted.
 *
 *		2) The last committed transaction may not be higher then the
 *		   next available transaction for the same reason.
 *
 *		3) The last recorded transaction may not be lower then the
 *		   last committed transaction.	(the reverse is ok - it means
 *		   that some transactions have aborted since the last commit)
 *
 *		Here is what the proper situation looks like.  The line
 *		represents the data stored in the log.	'c' indicates the
 *		transaction was recorded as committed, 'a' indicates an
 *		abortted transaction and '.' represents information not
 *		recorded.  These may correspond to in progress transactions.
 *
 *			 c	c  a  c  .	.  a  .  .	.  .  .  .	.  .  .  .
 *					  |					|
 *					 last			   next
 *
 *		Since "next" is only incremented by GetNewTransactionId() which
 *		is called when transactions are started.  Hence if there
 *		are commits or aborts after "next", then it means we committed
 *		or aborted BEFORE we started the transaction.  This is the
 *		rational behind constraint (1).
 *
 *		Likewise, "last" should never greater then "next" for essentially
 *		the same reason - it would imply we committed before we started.
 *		This is the reasoning for (2).
 *
 *		(3) implies we may never have a situation such as:
 *
 *			 c	c  a  c  .	.  a  c  .	.  .  .  .	.  .  .  .
 *					  |					|
 *					 last			   next
 *
 *		where there is a 'c' greater then "last".
 *
 *		Recovery checking is more difficult in the case where
 *		several backends are executing concurrently because the
 *		transactions may be executing in the other backends.
 *		So, we only do recovery stuff when the backend is explicitly
 *		passed a flag on the command line.
 * --------------------------------
 */
static void
TransRecover(Relation logrelation)
{
	BlockNumber     masterblock = InvalidBlockNumber;  
	TransactionId   ctid;
	TransactionId	lowwater;	
	TransactionId	mark;	
	
	bool		rollback = false;

	Buffer		buffer = InvalidBuffer;			/* buffer associated with block */
	Block		block;			/* block containing xstatus */

	elog(DEBUG,"--Scanning Transacation Log--");
	MasterWriteLock();

	ctid = GetNewTransactionId();
	lowwater = GetTransactionRecoveryCheckpoint();
	
	mark = lowwater;
	
	
	elog(DEBUG,"xid is %llu",ctid);
	elog(DEBUG,"low water is %llu\n",lowwater);
	
	while (mark < ctid) {
	
		BlockNumber localblock = TransComputeBlockNumber(logrelation,mark);
		
		if ( localblock != masterblock ) {
			if ( BufferIsValid(buffer) ) {
				ReleaseBuffer(logrelation,buffer);
                                buffer = InvalidBuffer;
			}
			masterblock = localblock;
                        while (!BufferIsValid(buffer) ) {
                            buffer = ReadBuffer(logrelation,localblock);
                        }
			block = BufferGetBlock(buffer);
		}

		if ( rollback ) {
			TransBlockSetXidStatus(block, mark, XID_ABORT);
			WriteNoReleaseBuffer(logrelation,buffer);
		} else if ( TransactionIdDidSoftCommit(mark) ) {
			TransBlockSetXidStatus(block, mark, XID_ABORT);
			WriteNoReleaseBuffer(logrelation,buffer);
			elog(DEBUG,"soft to abort %llu",mark);
		/*  
		
		don't do this for now and see how it goes
		may have to abort every thing after 
		first soft commit
		
		on second thought...
		
		I think we should swept everything after
		the first soft commit so that things 
		like sums will be consistent    MKS  11.27.2001
		
					
			rollback = true;
			printf("aborting transactions from %llu to %llu\n",mark,ctid);
		
                */
                } 

		mark++;
		
	}

	if ( BufferIsValid(buffer) ) {
		ReleaseBuffer(logrelation,buffer);
	}
        
        FlushAllDirtyBuffers(true);
        MasterUnLock();
	SetTransactionRecoveryCheckpoint(ctid);
	elog(DEBUG,"Recovery checking finished");

}

/* ----------------------------------------------------------------
 *						Interface functions
 *
 *		InitializeTransactionLog
 *		========
 *		   this function (called near cinit) initializes
 *		   the transaction log, time and variable relations.
 *
 *		TransactionId DidCommit
 *		TransactionId DidAbort
 *		TransactionId IsInProgress
 *		========
 *		   these functions test the transaction status of
 *		   a specified transaction id.
 *
 *		TransactionId Commit
 *		TransactionId Abort
 *		TransactionId SetInProgress
 *		========
 *		   these functions set the transaction status
 *		   of the specified xid. TransactionIdCommit() also
 *		   records the current time in the time relation
 *		   and updates the variable relation counter.
 *
 * ----------------------------------------------------------------
 */

/*
 * InitializeTransactionLog
 *		Initializes transaction logging.
 */
bool
InitializeTransactionLog(void)
{
	Relation	logRelation;
	Relation	variablerelation;
	MemoryContext oldContext;
    TransactionInfo*	info = GetTransactionInfo();

	/* ----------------
	 *	  don't do anything during bootstrapping
	 * ----------------
	 */
	if (AMI_OVERRIDE)
		return false;


	/* ----------------
	 *	make sure allocations occur within the top memory context
	 *	so that our log management structures are protected from
	 *	garbage collection at the end of every transaction.
	 * ----------------
	 */
	oldContext = MemoryContextSwitchTo(MemoryContextGetTopContext());

	/* ----------------
	 *	 first open the log and time relations
	 *	 (these are created by amiint so they are guaranteed to exist)
	 * ----------------
	 */
	logRelation = heap_openr(LogRelationName, NoLock);
	Assert(logRelation != NULL);
	variablerelation = heap_openr(VariableRelationName, NoLock);
	Assert(variablerelation != NULL);

	/* ----------------
	 *	 XXX TransactionLogUpdate requires that LogRelation
	 *	 is valid so we temporarily set it so we can initialize
	 *	 things properly. This could be done cleaner.
	 * ----------------
	 */
	TransactionSystemInitialized  = true;
/*	LogRelation = logRelation;           */            

	/* ----------------
	 *	 if we have a virgin database, we initialize the log
	 *	 relation by committing the AmiTransactionId (id 512) and we
	 *	 initialize the variable relation by setting the next available
	 *	 transaction id to FirstTransactionId (id 514).  OID initialization
	 *	 happens as a side effect of bootstrapping in varsup.c.
	 * ----------------
	 */
	SpinAcquire(OidGenLockId);
	
/* set the variable cache low water mark here we need to do this before any checks 
of the pg_log for commits
 */
	if (!TransactionIdDidCommit(AmiTransactionId))
	{		
		/* ----------------
		 *	SOMEDAY initialize the information stored in
		 *			the headers of the log/variable relations.
		 * ----------------
		 */
		if ( IsMultiuser() ) 
			elog(FATAL,"this should not be happening");
		
		TransactionLogUpdate(AmiTransactionId, XID_COMMIT);
		info->cachedTestXid = AmiTransactionId;
		info->cachedTestXidStatus = XID_COMMIT;
		SetTransactionLowWaterMark(FirstTransactionId);
		
		VariableRelationPutNextXid(FirstTransactionId);
	}
	else if (RecoveryCheckingEnabled())
	{
		/* ----------------
		 *		if we have a pre-initialized database and if the
		 *		perform recovery checking flag was passed then we
		 *		do our database integrity checking.
		 * ----------------
		 */
		SpinRelease(OidGenLockId);
                smgrreplaylogs();
		TransRecover(logRelation);
		SpinAcquire(OidGenLockId);
	} else {
		SpinRelease(OidGenLockId);
		SetTransactionRecoveryCheckpoint(GetNewTransactionId());
		SpinAcquire(OidGenLockId);
	}
	InitTransactionLowWaterMark();
        VacuumTransactionLog();
        
	TransactionSystemInitialized  = true;
	SpinRelease(OidGenLockId);


	/* ----------------
	 *	instantiate the global variables
	 * ----------------
	 */
/*	LogRelation = logRelation;		*/	

	/* ----------------
	 *	restore the memory context to the previous context
	 *	before we return from initialization.
	 * ----------------
	 */
	 
	 heap_close(logRelation,NoLock);
	 heap_close(variablerelation,NoLock);
	MemoryContextSwitchTo(oldContext);

        return true;
}


/* --------------------------------
 *		TransactionId DidCommit
 *		TransactionId DidAbort
 *		TransactionId IsInProgress
 * --------------------------------
 */

/*
 * TransactionIdDidCommit
 *		True iff transaction associated with the identifier did commit.
 *
 * Note:
 *		Assumes transaction identifier is valid.
 */
bool							/* true if given transaction committed */
TransactionIdDidCommit(TransactionId transactionId)
{
	TransactionInfo* info = GetTransactionInfo();
	if (AMI_OVERRIDE)
		return true;

	return (TransactionLogTest(info,transactionId, XID_COMMIT_TEST));
}

/*
 * TransactionIdDidAborted
 *		True iff transaction associated with the identifier did abort.
 *
 * Note:
 *		Assumes transaction identifier is valid.
 *		XXX Is this unneeded?
 */
bool							/* true if given transaction aborted */
TransactionIdDidAbort(TransactionId transactionId)
{
	TransactionInfo* info = GetTransactionInfo();
	if (AMI_OVERRIDE)
            return false;

	return TransactionLogTest(info,transactionId, XID_ABORT_TEST);
}

/*
*	TransactionIdDidSoftCommit
*
*		True iff transaction associated with the identifier did a soft commit
*
*
*
*
*
*/

bool
TransactionIdDidSoftCommit(TransactionId transactionId)
{
	TransactionInfo* info = GetTransactionInfo();
    if (AMI_OVERRIDE)
		return false;

	return TransactionLogTest(info,transactionId, XID_SOFT_COMMIT_TEST);
}

/*
*	TransactionIdDidHardCommit
*
*		True iff transaction associated with the identifier did a hard commit
*
*
*
*
*
*/

bool
TransactionIdDidHardCommit(TransactionId transactionId)
{
	TransactionInfo* info = GetTransactionInfo();
	if (AMI_OVERRIDE)
		return false;

	return (TransactionLogTest(info,transactionId,XID_HARD_COMMIT_TEST));
}


/*
*	TransactionIdDidCrash
*
*		True iff transaction associated with the identifier first is definitely not in progress
*		and did not commit and alos did not abort
*
*
*/

bool
TransactionIdDidCrash(TransactionId transactionId)
{
	TransactionInfo* info = GetTransactionInfo();
	if (AMI_OVERRIDE)
		return false;

	if (TransactionLogTest(info,transactionId,XID_INPROGRESS_TEST)) {
	    if (TransactionIdBeforeCheckpoint(transactionId) || !TransactionIdIsInProgress(transactionId)) {
                bool crashed = TransactionLogTest(info,transactionId,XID_INPROGRESS_TEST);
                return crashed;
            } 
        } 
        
        return false;
}
