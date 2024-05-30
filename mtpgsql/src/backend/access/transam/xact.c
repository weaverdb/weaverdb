/*-------------------------------------------------------------------------
 *
 * xact.c
 *	  top level transaction system support routines
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  $Header: /cvs/weaver/mtpgsql/src/backend/access/transam/xact.c,v 1.1.1.1 2006/08/12 00:20:06 synmscott Exp $
 *
 * NOTES
 *		Transaction aborts can now occur two ways:
 *
 *		1)	system dies from some internal cause  (Assert, etc..)
 *		2)	user types abort
 *
 *		These two cases used to be treated identically, but now
 *		we need to distinguish them.  Why?	consider the following
 *		two situatuons:
 *
 *				case 1							case 2
 *				------							------
 *		1) user types BEGIN				1) user types BEGIN
 *		2) user does something			2) user does something
 *		3) user does not like what		3) system aborts for some reason
 *		   she shes and types ABORT
 *
 *		In case 1, we want to abort the transaction and return to the
 *		default state.	In case 2, there may be more commands coming
 *		our way which are part of the same transaction block and we have
 *		to ignore these commands until we see an END transaction.
 *		(or an ABORT! --djm)
 *
 *		Internal aborts are now handled by AbortTransactionBlock(), just as
 *		they always have been, and user aborts are now handled by
 *		UserAbortTransactionBlock().  Both of them rely on AbortTransaction()
 *		to do all the real work.  The only difference is what state we
 *		enter after AbortTransaction() does its work:
 *
 *		* AbortTransactionBlock() leaves us in TBLOCK_ABORT and
 *		* UserAbortTransactionBlock() leaves us in TBLOCK_ENDABORT
 *
 *	 NOTES
 *		This file is an attempt at a redesign of the upper layer
 *		of the V1 transaction system which was too poorly thought
 *		out to describe.  This new system hopes to be both simpler
 *		in design, simpler to extend and needs to contain added
 *		functionality to solve problems beyond the scope of the V1
 *		system.  (In particuler, communication of transaction
 *		information between parallel backends has to be supported)
 *
 *		The essential aspects of the transaction system are:
 *
 *				o  transaction id generation
 *				o  transaction log updating
 *				o  memory cleanup
 *				o  cache invalidation
 *				o  lock cleanup
 *
 *		Hence, the functional division of the transaction code is
 *		based on what of the above things need to be done during
 *		a start/commit/abort transaction.  For instance, the
 *		routine AtCommit_Memory() takes care of all the memory
 *		cleanup stuff done at commit time.
 *
 *		The code is layered as follows:
 *
 *				StartTransaction
 *				CommitTransaction
 *				AbortTransaction
 *				UserAbortTransaction
 *
 *		are provided to do the lower level work like recording
 *		the transaction status in the log and doing memory cleanup.
 *		above these routines are another set of functions:
 *
 *				StartTransactionCommand
 *				CommitTransactionCommand
 *				AbortCurrentTransaction
 *
 *		These are the routines used in the postgres main processing
 *		loop.  They are sensitive to the current transaction block state
 *		and make calls to the lower level routines appropriately.
 *
 *		Support for transaction blocks is provided via the functions:
 *
 *				StartTransactionBlock
 *				CommitTransactionBlock
 *				AbortTransactionBlock
 *
 *		These are invoked only in responce to a user "BEGIN", "END",
 *		or "ABORT" command.  The tricky part about these functions
 *		is that they are called within the postgres main loop, in between
 *		the StartTransactionCommand() and CommitTransactionCommand().
 *
 *		For example, consider the following sequence of user commands:
 *
 *		1)		begin
 *		2)		retrieve (foo.all)
 *		3)		append foo (bar = baz)
 *		4)		end
 *
 *		in the main processing loop, this results in the following
 *		transaction sequence:
 *
 *			/	StartTransactionCommand();
 *		1) /	ProcessUtility();				<< begin
 *		   \		StartTransactionBlock();
 *			\	CommitTransactionCommand();
 *
 *			/	StartTransactionCommand();
 *		2) <	ProcessQuery();					<< retrieve (foo.all)
 *			\	CommitTransactionCommand();
 *
 *			/	StartTransactionCommand();
 *		3) <	ProcessQuery();					<< append foo (bar = baz)
 *			\	CommitTransactionCommand();
 *
 *			/	StartTransactionCommand();
 *		4) /	ProcessUtility();				<< end
 *		   \		CommitTransactionBlock();
 *			\	CommitTransactionCommand();
 *
 *		The point of this example is to demonstrate the need for
 *		StartTransactionCommand() and CommitTransactionCommand() to
 *		be state smart -- they should do nothing in between the calls
 *		to StartTransactionBlock() and EndTransactionBlock() and
 *		outside these calls they need to do normal start/commit
 *		processing.
 *
 *		Furthermore, suppose the "retrieve (foo.all)" caused an abort
 *		condition.	We would then want to abort the transaction and
 *		ignore all subsequent commands up to the "end".
 *		-cim 3/23/90
 *
 *-------------------------------------------------------------------------
 */

/*
 * Large object clean up added in CommitTransaction() to prevent buffer leaks.
 * [PA, 7/17/98]
 * [PA] is Pascal Andr?? <andre@via.ecp.fr>
 */
#include "postgres.h"

#include "env/env.h"

#include "access/nbtree.h"
#include "catalog/heap.h"
#include "catalog/index.h"
#include "commands/async.h"
#include "commands/sequence.h"

#include "commands/trigger.h"

#include "storage/multithread.h"
#include "storage/sinval.h"
#include "storage/localbuf.h"
#include "utils/temprel.h"
#include "utils/inval.h"
#include "utils/portal.h"
#include "utils/relcache.h"
#include "utils/catcache.h"
#include "env/dbwriter.h"
#include "utils/memutils.h"
#include "access/genam.h"
#include "env/dolhelper.h"

/*
* Moved to env MKS  7/30/2000
*
*extern bool SharedBufferChanged;
*/
const TransactionStateData CurrentTransactionStateData = {
	0,							/* transaction id */
	FirstCommandId,				/* command id */
	0,							/* scan command id */
	0x0,						/* start time */
	TRANS_DEFAULT,				/* transaction state */
	TBLOCK_DEFAULT				/* transaction block state */
};



static SectionId transaction_id = SECTIONID("TRAN");

static TransactionInfo* InitializeTransactionGlobals(void);

/*  Thread Local Storage cache for TransactionInfo  */
#ifdef TLS
TLS  TransactionInfo*  trans_info = NULL;
#else
#define trans_info GetEnv()->transaction_info
#endif

static void AtAbort_Cache(void);
static void AtAbort_Locks(void);
static void AtAbort_Memory(void);
static void AtCommit_Cache(void);
static void AtCommit_LocalCache(void);
static void AtCommit_Locks(void);
static void AtCommit_Memory(void);
static void AtStart_Cache(void);
static void AtStart_Locks(void);
static void AtStart_Memory(void);
static void RecordTransactionAbort(void);
static int RecordTransactionCommit(void);

/* ----------------
 *		global variables holding the current transaction state.
 *
 *		Note: when we are running several slave processes, the
 *			  current transaction state data is copied into shared memory
 *			  and the CurrentTransactionState pointer changed to
 *			  point to the shared copy.  All this occurrs in slaves.c
 * ----------------
 */
 


/* TransactionState CurrentTransactionState = &CurrentTransactionStateData; */

int			DefaultXactIsoLevel = XACT_READ_COMMITTED;
/*
int			XactIsoLevel;
*/
/* ----------------
 *		info returned when the system is disabled
 *
 * Apparently a lot of this code is inherited from other prototype systems.
 * For DisabledStartTime, use a symbolic value to make the relationships clearer.
 * The old value of 1073741823 corresponds to a date in y2004, which is coming closer
 *	every day. It appears that if we return a value guaranteed larger than
 *	any real time associated with a transaction then comparisons in other
 *	modules will still be correct. Let's use BIG_ABSTIME for this. tgl 2/14/97
 *
 *		Note:  I have no idea what the significance of the
 *			   1073741823 in DisabledStartTime.. I just carried
 *			   this over when converting things from the old
 *			   V1 transaction system.  -cim 3/18/90
 * ----------------
 */
CommandId	DisabledCommandId = (CommandId) -1;

AbsoluteTime DisabledStartTime = (AbsoluteTime) BIG_ABSTIME;	/* 1073741823; */

static bool   TransactionSystemDisabled   = true;

/* ----------------
 *		overflow flag
 * ----------------
 */
/*
bool		CommandIdCounterOverflowFlag;
*/
/* ----------------
 *		catalog creation transaction bootstrapping flag.
 *		This should be eliminated and added to the transaction
 *		state stuff.  -cim 3/19/90
 * ----------------
 */
/* ----------------------------------------------------------------
 *					 transaction state accessors
 * ----------------------------------------------------------------
 */


/* --------------------------------
 *		IsAbortedTransactionBlockState
 *
 *		This returns true if we are currently running a query
 *		within an aborted transaction block.
 * --------------------------------
 */
bool
IsAbortedTransactionBlockState()
{
    TransactionInfo* info = GetTransactionInfo();
	TransactionState s = info->CurrentTransactionState;

	if (s->blockState == TBLOCK_ABORT || s->blockState == TBLOCK_ABORTONLY)
		return true;

	return false;
}

bool
IsTransactionSystemDisabled() {
    return TransactionSystemDisabled;
}

/* --------------------------------
 *		GetCurrentTransactionId
 *
 *		This returns the id of the current transaction, or
 *		the id of the "disabled" transaction.
 * --------------------------------
 */
TransactionId
GetCurrentTransactionId(void)
{
	TransactionState s = GetTransactionInfo()->CurrentTransactionState;

	/* ----------------
	 *	if the transaction system is disabled, we return
	 *	the special "disabled" transaction id.
	 * ----------------
	 */
	if (TransactionSystemDisabled) {
		return AmiTransactionId;
	}

	/* ----------------
	 *	otherwise return the current transaction id.
	 * ----------------
	 */
	 return s->transactionIdData;
}


/* --------------------------------
 *		GetCurrentCommandId
 * --------------------------------
 */
CommandId
GetCurrentCommandId()
{
	TransactionState s = GetTransactionInfo()->CurrentTransactionState;

	/* ----------------
	 *	if the transaction system is disabled, we return
	 *	the special "disabled" command id.
	 * ----------------
	 */
	if (TransactionSystemDisabled)
		return (CommandId) DisabledCommandId;


	return s->commandId;
}

CommandId
GetScanCommandId()
{
	TransactionState s = GetTransactionInfo()->CurrentTransactionState;

	/* ----------------
	 *	if the transaction system is disabled, we return
	 *	the special "disabled" command id.
	 * ----------------
	 */
	if (TransactionSystemDisabled)
		return (CommandId) DisabledCommandId;

	return s->scanCommandId;
}


/* --------------------------------
 *		GetCurrentTransactionStartTime
 * --------------------------------
 */
AbsoluteTime
GetCurrentTransactionStartTime()
{
	TransactionState s = GetTransactionInfo()->CurrentTransactionState;

	/* ----------------
	 *	if the transaction system is disabled, we return
	 *	the special "disabled" starting time.
	 * ----------------
	 */
	if (TransactionSystemDisabled)
		return (AbsoluteTime) DisabledStartTime;

	return s->startTime;
}


/* --------------------------------
 *		TransactionIdIsCurrentTransactionId
 * --------------------------------
 */
bool
TransactionIdIsCurrentTransactionId(TransactionId xid)
{
	if (TransactionSystemDisabled)
		return false;

	return GetTransactionInfo()->CurrentTransactionState->transactionIdData == xid;
}


/* --------------------------------
 *		CommandIdIsCurrentCommandId
 * --------------------------------
 */
bool
CommandIdIsCurrentCommandId(CommandId cid)
{
	if (TransactionSystemDisabled)
		return false;

	return (cid == GetTransactionInfo()->CurrentTransactionState->commandId);
}

bool
CommandIdGEScanCommandId(CommandId cid)
{
	if (TransactionSystemDisabled)
		return false;

	return (cid >= GetTransactionInfo()->CurrentTransactionState->scanCommandId);
}



/* --------------------------------
 *		CommandCounterIncrement
 * --------------------------------
 */
void
CommandCounterIncrement()
{

	if (TransactionSystemDisabled)
		return;

        TransactionInfo* info = GetTransactionInfo();
	
	TransactionState trans = info->CurrentTransactionState;

        if ( trans->state != TRANS_INPROGRESS ) {
            elog(ERROR,"Transaction not started");
        }
	
	trans->commandId += 1;
	if (trans->commandId == FirstCommandId)
	{
		info->CommandIdCounterOverflowFlag = true;
		elog(ERROR, "You may only have 2^32-1 commands per transaction");
	}

	trans->scanCommandId = trans->commandId;

	/*
	 * make cache changes visible to me.  AtCommit_LocalCache() instead of
	 * AtCommit_Cache() is called here.
	 */
	AtCommit_LocalCache();
	AtStart_Cache();

}

void
SetScanCommandId(CommandId savedId)
{

	TransactionState trans = (TransactionState)GetTransactionInfo()->CurrentTransactionState;
	trans->scanCommandId = savedId;

}

/* ----------------------------------------------------------------
 *						initialization stuff
 * ----------------------------------------------------------------
 */
void
InitializeTransactionSystem()
{
	TransactionSystemDisabled = !InitializeTransactionLog();
}

/* ----------------------------------------------------------------
 *						StartTransaction stuff
 * ----------------------------------------------------------------
 */

/* --------------------------------
 *		AtStart_Cache
 * --------------------------------
 */
static void
AtStart_Cache()
{
	DiscardInvalid();
}

/* --------------------------------
 *		AtStart_Locks
 * --------------------------------
 */
static void
AtStart_Locks()
{

	/*
	 * at present, it is unknown to me what belongs here -cim 3/18/90
	 *
	 * There isn't anything to do at the start of a xact for locks. -mer
	 * 5/24/92
	 */
/*  this sets the fact that someone is doing a transaction  */
	 TransactionLock();
}

/* --------------------------------
 *		AtStart_Memory
 * --------------------------------
 */
static void
AtStart_Memory()
{
    MemoryContextGlobals*  mem_env = MemoryContextGetEnv();

    if ( mem_env->TopTransactionContext != NULL ) {
        MemoryContextDelete(mem_env->TopTransactionContext);
    }
	/*
	 * Create a toplevel context for the transaction.
	 */
	mem_env->TopTransactionContext =
		AllocSetContextCreate(MemoryContextGetTopContext(),
							  "TopTransactionContext",
							  ALLOCSET_DEFAULT_MINSIZE,
							  ALLOCSET_DEFAULT_INITSIZE,
							  ALLOCSET_DEFAULT_MAXSIZE);

	/*
	 * Create a statement-level context and make it active.
	 */
	mem_env->TransactionCommandContext =
		AllocSetContextCreate(mem_env->TopTransactionContext,
							  "TransactionCommandContext",
							  ALLOCSET_DEFAULT_MINSIZE,
							  ALLOCSET_DEFAULT_INITSIZE,
							  ALLOCSET_DEFAULT_MAXSIZE);
                              
	MemoryContextSwitchTo(mem_env->TransactionCommandContext);
}


/* ----------------------------------------------------------------
 *						CommitTransaction stuff
 * ----------------------------------------------------------------
 */

/* --------------------------------
 *		RecordTransactionCommit
 *
 *		Note: the two calls to BufferManagerFlush() exist to ensure
 *			  that data pages are written before log pages.  These
 *			  explicit calls should be replaced by a more efficient
 *			  ordered page write scheme in the buffer manager
 *			  -cim 3/18/90
 * --------------------------------
 */
static int
RecordTransactionCommit()
{
	TransactionId xid;

	/* ----------------
	 *	get the current transaction id
	 * ----------------
	 */
	xid = GetCurrentTransactionId();

	/*
	 * flush the buffer manager pages.	Note: if we have stable main
	 * memory, dirty shared buffers are not flushed plai 8/7/90
	 */


	/*
	 * If no one shared buffer was changed by this transaction then we
	 * don't flush shared buffers and don't record commit status.
	 */
	if (GetTransactionInfo()->SharedBufferChanged)
	{
            CommitDBBufferWrites(xid,XID_COMMIT);
            return 1;
	} else {
            LocalBufferSync();
            ThreadTransactionReset();
        }
	return 0;
}


/* --------------------------------
 *		AtCommit_Cache
 * --------------------------------
 */
static void
AtCommit_Cache()
{
	/* ----------------
	 * Make catalog changes visible to all backend.
	 * ----------------
	 */
	RegisterInvalid(true);
        ResetCatalogCacheMemory();
}

/* --------------------------------
 *		AtCommit_LocalCache
 * --------------------------------
 */
static void
AtCommit_LocalCache()
{
	/* ----------------
	 * Make catalog changes visible to me for the next command.
	 * ----------------
	 */
	ImmediateLocalInvalidation(true);
}

/* --------------------------------
 *		AtCommit_Locks
 * --------------------------------
 */
static void
AtCommit_Locks()
{
	/* ----------------
	 *	XXX What if ProcReleaseLocks fails?  (race condition?)
	 *
	 *	Then you're up a creek! -mer 5/24/92
	 * ----------------
	 */
	ThreadReleaseLocks(true);
	TransactionUnlock();
}

/* --------------------------------
 *		AtCommit_Memory
 * --------------------------------
 */
static void
AtCommit_Memory()
{
    MemoryContextGlobals*  mem_env = MemoryContextGetEnv();
	/*
	 * Now that we're "out" of a transaction, have the system allocate
	 * things in the top memory context instead of per-transaction
	 * contexts.
	 */
	MemoryContextSwitchTo(MemoryContextGetTopContext());

	/*
	 * Release all transaction-local memory.
	 */
	Assert(mem_env->TopTransactionContext != NULL);
	MemoryContextDelete(mem_env->TopTransactionContext);
	mem_env->TopTransactionContext = NULL;
	mem_env->TransactionCommandContext = NULL;
}

/* ----------------------------------------------------------------
 *						AbortTransaction stuff
 * ----------------------------------------------------------------
 */

/* --------------------------------
 *		RecordTransactionAbort
 * --------------------------------
 */
static void
RecordTransactionAbort()
{
	TransactionId xid;

	/* ----------------
	 *	get the current transaction id
	 * ----------------
	 */
	xid = GetCurrentTransactionId();

	/*
	 * Have the transaction access methods record the status of this
	 * transaction id in the pg_log relation. We skip it if no one shared
	 * buffer was changed by this transaction.
	 */
	if (GetTransactionInfo()->SharedBufferChanged ) {
            CommitDBBufferWrites(xid,XID_ABORT);
        } else {
            LocalBufferSync();
            ThreadTransactionReset();
        }
	/*
	 * Tell bufmgr and smgr to release resources.
	 */
	ResetBufferPool(false);		/* false -> is abort */
}

/* --------------------------------
 *		AtAbort_Cache
 * --------------------------------
 */
static void
AtAbort_Cache()
{
	RelationCacheAbort();
	RegisterInvalid(false);
        ResetCatalogCacheMemory();
}

/* --------------------------------
 *		AtAbort_Locks
 * --------------------------------
 */
static void
AtAbort_Locks()
{
	/* ----------------
	 *	XXX What if ProcReleaseLocks() fails?  (race condition?)
	 *
	 *	Then you're up a creek without a paddle! -mer
	 * ----------------
	 */
	ThreadReleaseLocks(false);
	TransactionUnlock();
}


/* --------------------------------
 *		AtAbort_Memory
 * --------------------------------
 */
static void
AtAbort_Memory()
{
    MemoryContextGlobals*  mem_env = MemoryContextGetEnv();
	/*
	 * Make sure we are in a valid context (not a child of
	 * TransactionCommandContext...).  Note that it is possible for this
	 * code to be called when we aren't in a transaction at all; go
	 * directly to TopMemoryContext in that case.
	 */

	MemoryContextSwitchTo(MemoryContextGetTopContext());

	/*
	 * Release all transaction-local memory.
	 */
	if ( mem_env->TopTransactionContext != NULL ) 
            MemoryContextDelete(mem_env->TopTransactionContext);
	mem_env->TopTransactionContext = NULL;
	mem_env->TransactionCommandContext = NULL;
}

/* ----------------------------------------------------------------
 *						interface routines
 * ----------------------------------------------------------------
 */

/* --------------------------------
 *		StartTransaction
 *
 * --------------------------------
 */
void
StartTransaction()
{
	TransactionInfo*  info = GetTransactionInfo();
	TransactionState s = info->CurrentTransactionState;
	FreeXactSnapshot();
	TransactionId 		xid = InvalidTransactionId;

	/* ----------------
	 *	Check the current transaction state.  If the transaction system
	 *	is switched off, or if we're already in a transaction, do nothing.
	 *	We're already in a transaction when the monitor sends a null
	 *	command to the backend to flush the comm channel.  This is a
	 *	hacky fix to a communications problem, and we keep having to
	 *	deal with it here.	We should fix the comm channel code.  mao 080891
	 * ----------------
	 */
	if (TransactionSystemDisabled) 
		return;

	if (s->state == TRANS_INPROGRESS)
		elog(NOTICE,"transaction already in progress");
	/* ----------------
	 *	set the current transaction state information
	 *	appropriately during start processing
	 * ----------------
	 */
	s->state = TRANS_START;
        info->backupState = s->state;

	ResetReindexProcessing();
	/* ----------------
	 *	generate a new transaction id
	 * ----------------
	 */
	xid = GetNewTransactionId();

	s->transactionIdData = xid;

	ThreadTransactionStart(xid);





	XactLockTableInsert(xid);

	/* ----------------
	 *	initialize current transaction state fields
	 * ----------------
	 */
	s->commandId = FirstCommandId;
	s->scanCommandId = FirstCommandId;
	s->startTime = GetCurrentAbsoluteTime();

	/* ----------------
	 *	initialize the various transaction subsystems
	 * ----------------
	 */
        ResetTransactionCommitType();
	AtStart_Locks();
	AtStart_Cache();
	AtStart_Memory();

	/* --------------
	   initialize temporary relations list
	   the tempRelList is a list of temporary relations that
	   are created in the course of the transactions
	   they need to be destroyed properly at the end of the transactions
	 */
	InitNoNameRelList();

	/* ----------------
	 *	Tell the trigger manager to we're starting a transaction
	 * ----------------
	 */
	DeferredTriggerBeginXact();

	/* ----------------
	 *	done with start processing, set current transaction
	 *	state to "in progress"
	 * ----------------
	 */
	s->state = TRANS_INPROGRESS;
        info->backupState = s->state;

}

/* ---------------
 * Tell me if we are currently in progress
 * ---------------
 */
bool
CurrentXactInProgress()
{
	return ((TransactionState)GetTransactionInfo()->CurrentTransactionState)->state == TRANS_INPROGRESS;
}

/* --------------------------------
 *		CommitTransaction
 *
 * --------------------------------
 */
void
CommitTransaction()
{
        TransactionInfo* info = GetTransactionInfo();
	TransactionState s = info->CurrentTransactionState;

	/* ----------------
	 *	check the current transaction state
	 * ----------------
	 */
	if (TransactionSystemDisabled)
		return;

	if (s->state != TRANS_INPROGRESS)
		elog(NOTICE, "CommitTransaction and not in in-progress state ");

	/* ----------------
	 *	Tell the trigger manager that this transaction is about to be
	 *	committed. He'll invoke all trigger deferred until XACT before
	 *	we really start on committing the transaction.
	 * ----------------
	 */
	DeferredTriggerEndXact();

	/* ----------------
	 *	set the current transaction state information
	 *	appropriately during the abort processing
	 * ----------------
	 */
	s->state = TRANS_COMMIT;
        info->backupState = s->state;
        
	/* ----------------
	 *	do commit processing
	 * ----------------
	 */
  /* make sure all the DolHelpers are done  and stopped */     
         CancelDolHelpers();

	/* NOTIFY commit must also come before lower-level cleanup */
	AtCommit_Notify();

	CloseSequences();
	DropNoNameRels();
        RelationCacheCommit();
	
        ThreadTransactionEnd();
        
	RecordTransactionCommit();
	/*
	 * Let others know about no transaction in progress by me. Note that
	 * this must be done _before_ releasing locks we hold and
	 * SpinAcquire(SInvalLock) is required: UPDATE with xid 0 is blocked
	 * by xid 1' UPDATE, xid 1 is doing commit while xid 2 gets snapshot -
	 * if xid 2' GetSnapshotData sees xid 1 as running then it must see
	 * xid 0 as running as well or it will see two tuple versions - one
	 * deleted by xid 1 and one inserted by xid 0.
	 */
	RelationPurgeLocalRelation(true);

	AtCommit_Cache();
	AtCommit_Locks();
	AtCommit_Memory();
	AtEOXact_Files();

#ifdef  USE_ASSERT_CHECKING  
        if ( BufferPoolCheckLeak() ) {
           ResetBufferPool(true); 
        }
#else
        ResetLocalBufferPool();
#endif

	/* ----------------
	 *	done with commit processing, set current transaction
	 *	state back to default
	 * ----------------
	 */
	s->state = TRANS_DEFAULT;
	info->SharedBufferChanged = false;/* safest place to do it */
}

/* --------------------------------
 *		AbortTransaction
 *
 * --------------------------------
 */
void
AbortTransaction()
{
        TransactionInfo* info = GetTransactionInfo();
	
	TransactionState s = info->CurrentTransactionState;

	/*
	 * Let others to know about no transaction in progress - vadim
	 * 11/26/96
	 */



	/* ----------------
	 *	check the current transaction state
	 * ----------------
	 */
	if (TransactionSystemDisabled)
		return;

	if (s->state != TRANS_INPROGRESS && s->state != TRANS_START)
		elog(NOTICE, "AbortTransaction and not in start or in-progress state ");

	/* ----------------
	 *	Tell the trigger manager that this transaction is about to be
	 *	aborted.
	 * ----------------
	 */
        DeferredTriggerAbortXact();
	/* ----------------
	 *	set the current transaction state information
	 *	appropriately during the abort processing
	 * ----------------
	 */
	s->state = TRANS_ABORT;
        info->backupState = TRANS_ABORT;

	/* ----------------
	 *	do abort processing
	 * ----------------
	 */
/*	lo_commit(false);	*/		/* 'false' means it's abort */

	UnlockBuffers();


  /* make sure all the DolHelpers are done  and stopped */     
        CancelDolHelpers();
	AtAbort_Notify();
	CloseSequences();

        RecordTransactionAbort();
        
	RelationPurgeLocalRelation(false);
	DropNoNameRels();
	invalidate_temp_relations();

	AtAbort_Cache();
	AtAbort_Locks();
	AtAbort_Memory();
	AtEOXact_Files();

        ResetLocalBufferPool();

	/* ----------------
	 *	done with abort processing, set current transaction
	 *	state back to default
	 * ----------------
	 */
	s->state = TRANS_DEFAULT;
	info->SharedBufferChanged = false;/* safest place to do it */
}

/* --------------------------------
 *		StartTransactionCommand
 * --------------------------------
 */
void
StartTransactionCommand()
{
	TransactionState s = GetTransactionInfo()->CurrentTransactionState;

	switch (s->blockState)
	{
			/* ----------------
			 *		if we aren't in a transaction block, we
			 *		just do our usual start transaction.
			 * ----------------
			 */
		case TBLOCK_DEFAULT:
			StartTransaction();
			s->blockState = TBLOCK_AUTO;
			break;


		case TBLOCK_AUTO:
			elog(NOTICE,"StartTransactionCommand: unexpected TBLOCK_AUTO");
			break;

		case TBLOCK_MANUAL:
			break;

		case TBLOCK_ABORT:
			elog(NOTICE,"StartTransactionCommand: unexpected TBLOCK_ABORT");
			break;
		case TBLOCK_COMMIT:
			elog(NOTICE,"StartTransactionCommand: unexpected TBLOCK_COMMIT");
			break;	
	}
        if (!TransactionSystemDisabled) {
            MemoryContextSwitchTo(MemoryContextGetEnv()->TransactionCommandContext);
        }
}

/* --------------------------------
 *		CommitTransactionCommand
 * --------------------------------
 */
void
CommitTransactionCommand()
{
	
	TransactionState s = GetTransactionInfo()->CurrentTransactionState;

	switch (s->blockState)
	{
		case TBLOCK_DEFAULT:
			elog(NOTICE,"CommitTransactionCommand: unexpected TBLOCK_DEFAULT");
			break;

		case TBLOCK_AUTO:
			CommitTransaction();
			s->blockState = TBLOCK_DEFAULT;
			break;

		case TBLOCK_MANUAL:
			CommandCounterIncrement();
			MemoryContextResetAndDeleteChildren(MemoryContextGetEnv()->TransactionCommandContext);
			break;
            
                case TBLOCK_ABORTONLY:
                        AbortTransaction();
                        s->blockState = TBLOCK_DEFAULT;
                        break;

		case TBLOCK_COMMIT:
			CommitTransaction();
			s->blockState = TBLOCK_DEFAULT;
			break;

		case TBLOCK_ABORT:
			AbortTransaction();
			s->blockState = TBLOCK_DEFAULT;
			break;

	}
}

/* ----------------------------------------------------------------
 *					   transaction block support
 * ----------------------------------------------------------------
 */
/* --------------------------------
 *		BeginTransactionBlock
 * --------------------------------
 */
void
BeginTransactionBlock(void)
{
	TransactionState s = GetTransactionInfo()->CurrentTransactionState;

	/* ----------------
	 *	check the current transaction state
	 * ----------------
	 */
	if (TransactionSystemDisabled)
		return;

	if (s->blockState == TBLOCK_MANUAL || s->blockState == TBLOCK_ABORTONLY)
		elog(NOTICE, "BEGIN: already a transaction in progress");

	/* ----------------
	 *	set the current transaction block state information
	 *	appropriately during begin processing
	 * ----------------
	 */
	s->blockState = TBLOCK_MANUAL;

	/* ----------------
	 *	do begin processing
	 * ----------------
	 */
}

void
AbortTransactionBlock(void)
{
	TransactionState s = GetTransactionInfo()->CurrentTransactionState;

	/* ----------------
	 *	check the current transaction state
	 * ----------------
	 */
	if (TransactionSystemDisabled)
		return;

	if (s->blockState == TBLOCK_MANUAL || s->blockState == TBLOCK_ABORTONLY)
	{
		/* ----------------
		 *	here we are in a transaction block which should commit
		 *	when we get to the upcoming CommitTransactionCommand()
		 *	so we set the state to "END".  CommitTransactionCommand()
		 *	will recognize this and commit the transaction and return
		 *	us to the default state
		 * ----------------
		 */
		s->blockState = TBLOCK_ABORT;
		return;
	}

	/* ----------------
	 *	We should not get here, but if we do, we go to the ENDABORT
	 *	state after printing a warning.  The upcoming call to
	 *	CommitTransactionCommand() will then put us back into the
	 *	default state.
	 * ----------------
	 */
        s->blockState = TBLOCK_DEFAULT;
	elog(NOTICE, "ABORT: no transaction in progress");
}

void 
CloneParentTransaction() {
/*  
    we are going to need to assume the transaction identity of the 
    parent environment so save these items before we switch environments.
*/
        Env* env = GetEnv();
        
        if ( env->parent == NULL ) {
            elog(ERROR,"not a sub-connection");
        }
        
        TransactionInfo* ts = env->parent->transaction_info;
        SnapshotHolder* snapshot = env->parent->snapshot_holder;
/*  
    copy the current transaction info and snapshot data 
    this should remain read only in both the parent and child 
    need to set up checks to make sure
*/
        TransactionInfo* ns = GetTransactionInfo();
        memcpy(ns->CurrentTransactionState,ts->CurrentTransactionState,sizeof(CurrentTransactionStateData));
        ns->XactIsoLevel = ts->XactIsoLevel;
        
        SnapshotHolder* nh = GetSnapshotHolder();
        nh->QuerySnapshot = snapshot->QuerySnapshot;
        nh->SerializableSnapshot = snapshot->SerializableSnapshot;	
        nh->UserSnapshot = snapshot->UserSnapshot;
        
        AtStart_Memory();        
}

void
CloseSubTransaction() {
        TransactionInfo* info = GetTransactionInfo();
	TransactionState s = info->CurrentTransactionState;

	/* ----------------
	 *	check the current transaction state
	 * ----------------
	 */
	if (TransactionSystemDisabled)
		return;

	if (s->state != TRANS_INPROGRESS)
		elog(NOTICE, "CommitTransaction and not in in-progress state ");

	/* ----------------
	 *	set the current transaction state information
	 *	appropriately during the abort processing
	 * ----------------
	 */
	s->state = TRANS_COMMIT;
	/* ----------------
	 *	do commit processing
	 * ----------------
	 */
	CloseSequences();
	DropNoNameRels();

      	/*
	 * Let others know about no transaction in progress by me. Note that
	 * this must be done _before_ releasing locks we hold and
	 * SpinAcquire(SInvalLock) is required: UPDATE with xid 0 is blocked
	 * by xid 1' UPDATE, xid 1 is doing commit while xid 2 gets snapshot -
	 * if xid 2' GetSnapshotData sees xid 1 as running then it must see
	 * xid 0 as running as well or it will see two tuple versions - one
	 * deleted by xid 1 and one inserted by xid 0.
	 */
	RelationPurgeLocalRelation(true);

	AtCommit_Memory();
	AtEOXact_Files();
        
 	UnlockBuffers();

#ifdef  USE_ASSERT_CHECKING  
        if ( BufferPoolCheckLeak() ) {
           ResetBufferPool(true); 
        }
#else
        ResetLocalBufferPool();
#endif
	/* ----------------
	 *	done with commit processing, set current transaction
	 *	state back to default
	 * ----------------
	 */
	s->state = TRANS_DEFAULT;
	info->SharedBufferChanged = false;/* safest place to do it */
}

void
CommitTransactionBlock(void)
{
	TransactionState s = GetTransactionInfo()->CurrentTransactionState;

	/* ----------------
	 *	check the current transaction state
	 * ----------------
	 */
	if (TransactionSystemDisabled)
		return;

	if (s->blockState == TBLOCK_MANUAL)
	{
		/* ----------------
		 *	here we are in a transaction block which should commit
		 *	when we get to the upcoming CommitTransactionCommand()
		 *	so we set the state to "END".  CommitTransactionCommand()
		 *	will recognize this and commit the transaction and return
		 *	us to the default state
		 * ----------------
		 */
		s->blockState = TBLOCK_COMMIT;
		return;
	}

	if (s->blockState == TBLOCK_ABORTONLY)
	{
		/* ----------------
		 *	here, we are in a transaction block which aborted
		 *	and since the AbortTransaction() was already done,
		 *	we do whatever is needed and change to the special
		 *	"END ABORT" state.	The upcoming CommitTransactionCommand()
		 *	will recognise this and then put us back in the default
		 *	state.
		 * ----------------
		 */
		s->blockState = TBLOCK_ABORT;
		elog(NOTICE,"abort only state");
		return;
	}

	/* ----------------
	 *	We should not get here, but if we do, we go to the ENDABORT
	 *	state after printing a warning.  The upcoming call to
	 *	CommitTransactionCommand() will then put us back into the
	 *	default state.
	 * ----------------
	 */
	elog(ERROR, "COMMIT: no transaction in progress");
}

void
SetAbortOnly() 
{
	TransactionState s = GetTransactionInfo()->CurrentTransactionState;

	if ( s->blockState == TBLOCK_AUTO ) {
            s->blockState = TBLOCK_ABORT;
        } else if ( s->blockState != TBLOCK_DEFAULT ) {
            s->blockState = TBLOCK_ABORTONLY;
        }
        
        TransactionUnlock();
}


bool
IsTransactionBlock()
{
	TransactionState s = GetTransactionInfo()->CurrentTransactionState;

	if (s->blockState == TBLOCK_MANUAL)
            return true;

	return false;
}

bool
AbandonTransactionBlock()
{
	TransactionState s = GetTransactionInfo()->CurrentTransactionState;

	bool wasBlocked = (s->blockState == TBLOCK_MANUAL || s->blockState == TBLOCK_ABORTONLY);

        s->blockState = TBLOCK_DEFAULT;

	return wasBlocked;
}

TransactionInfo*
GetTransactionInfo(void)
{
    TransactionInfo* transinfo = trans_info;

    if ( transinfo == NULL ) {
        transinfo = InitializeTransactionGlobals();
    }

    return transinfo;
}

TransactionInfo* 
InitializeTransactionGlobals(void) {

    TransactionInfo* info = AllocateEnvSpace(transaction_id,sizeof(TransactionInfo));

    info->XactIsoLevel = DefaultXactIsoLevel;
    info->CurrentTransactionState = MemoryContextAlloc(MemoryContextGetTopContext(),
        sizeof(TransactionStateData));
    memcpy(info->CurrentTransactionState,&CurrentTransactionStateData,sizeof(CurrentTransactionStateData));

    trans_info = info;

    return info;
}


