/*-------------------------------------------------------------------------
 *
 * varsup.c
 *	  postgres variable relation support routines
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  $Header: /cvs/weaver/mtpgsql/src/backend/access/transam/varsup.c,v 1.2 2006/08/15 18:24:26 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */

#include <unistd.h>

#include "postgres.h"

#include "env/env.h"

#include "env/connectionutil.h"
#include "env/dbwriter.h"
#include "access/heapam.h"
#include "catalog/catname.h"
#include "utils/relcache.h"
#include "storage/multithread.h"
#include "storage/smgr.h"
#include "miscadmin.h"


typedef struct logbase {
	bool		init;
	Oid		database;
	TransactionId	lowwater;	
} LogBase;

typedef struct header {
	TransactionId	baseline;
	TransactionId	checkpoint;
	LogBase		databases;
} Header;

#define  VAR_OID_PREFETCH  8
#define VAR_XID_PREFETCH		( 8 )

static int                              xid_prefetch = VAR_XID_PREFETCH;
static int                              oid_prefetch = VAR_OID_PREFETCH;
static int 				nextoid;
static int 				oid_queue_count;
static pthread_mutex_t	oid_access;

static Oid VariableRelationGetNextOid(void);
static TransactionId VariableRelationGetNextXid(void);


/* ---------------------
 *		spin lock for oid generation
 * ---------------------
 */
/*
*	Moved to env MKS  7/30/2000
*
*
*
*/
int			OidGenLockId;

VariableCache ShmemVariableCache = NULL;

/* ----------------------------------------------------------------
 *			  variable relation query/update routines
 * ----------------------------------------------------------------
 */

/* --------------------------------
 *		VariableRelationGetNextXid
 * --------------------------------
 */
static TransactionId
VariableRelationGetNextXid(void)
{
	Buffer		buf;
	VariableRelationContents var;
	Relation	VariableRelation = RelationNameGetRelation(VariableRelationName,DEFAULTDBOID);
	TransactionId	xid;


	/* ----------------
	 * We assume that a spinlock has been acquire to guarantee
	 * exclusive access to the variable relation.
	 * ----------------
	 */

	/* ----------------
	 *	do nothing before things are initialized
	 * ----------------
	 */
	if (!RelationIsValid(VariableRelation))
		return InvalidTransactionId;

	LockRelation(VariableRelation,AccessExclusiveLock);

	/* ----------------
	 *	read the variable page, get the the nextXid field and
	 *	release the buffer
	 * ----------------
	 */
	buf = ReadBuffer(VariableRelation, 0);

	if (!BufferIsValid(buf))
	{
		UnlockRelation(VariableRelation,AccessExclusiveLock);
		RelationClose(VariableRelation);
		elog(ERROR, "VariableRelationGetNextXid: ReadBuffer failed");
	}

	var = (VariableRelationContents) BufferGetBlock(buf);

	xid = var->nextXidData;
	var->nextXidData += xid_prefetch;

	FlushBuffer(VariableRelation,buf);
	UnlockRelation(VariableRelation,AccessExclusiveLock);
	RelationClose(VariableRelation);
	return xid;
}

/* --------------------------------
 *		VariableRelationPutNextXid
 * --------------------------------
 */
void
VariableRelationPutNextXid(TransactionId xid)
{
	Buffer		buf;
	VariableRelationContents var;
	Relation	VariableRelation = RelationNameGetRelation(VariableRelationName,DEFAULTDBOID);

	/* ----------------
	 * We assume that a spinlock has been acquire to guarantee
	 * exclusive access to the variable relation.
	 * ----------------
	 */

	/* ----------------
	 *	do nothing before things are initialized
	 * ----------------
	 */
	if (!RelationIsValid(VariableRelation))
		return;
	LockRelation(VariableRelation,AccessExclusiveLock);
	/* ----------------
	 *	read the variable page, update the nextXid field and
	 *	write the page back out to disk (with immediate write).
	 * ----------------
	 */
	buf = ReadBuffer(VariableRelation, 0);

	if (!BufferIsValid(buf))
	{
		UnlockRelation(VariableRelation,AccessExclusiveLock);
		RelationClose(VariableRelation);
		elog(ERROR, "VariableRelationPutNextXid: ReadBuffer failed");
	}

	var = (VariableRelationContents) BufferGetBlock(buf);

	var->nextXidData = xid;

	FlushBuffer(VariableRelation, buf);
	UnlockRelation(VariableRelation,AccessExclusiveLock);
	RelationClose(VariableRelation);
}

/* --------------------------------
 *		VariableRelationGetNextOid
 * --------------------------------
 */
static Oid
VariableRelationGetNextOid(void)
{
	Buffer		buf;
	VariableRelationContents var;
	Relation	VariableRelation = RelationNameGetRelation(VariableRelationName,DEFAULTDBOID);
	Oid  oid_ret;
	/* ----------------
	 * We assume that a spinlock has been acquire to guarantee
	 * exclusive access to the variable relation.
	 * ----------------
	 */

	/* ----------------
	 *	if the variable relation is not initialized, then we
	 *	assume we are running at bootstrap time and so we return
	 *	an invalid object id -- during this time GetNextBootstrapObjectId
	 *	should be called instead..
	 * ----------------
	 */
	if (!RelationIsValid(VariableRelation))
	{
		return InvalidOid;
	}

	/* ----------------
	 *	read the variable page, get the the nextOid field and
	 *	release the buffer
	 * ----------------
	 */
	LockRelation(VariableRelation,AccessExclusiveLock);
	buf = ReadBuffer(VariableRelation, 0);

	if (!BufferIsValid(buf))
	{
		UnlockRelation(VariableRelation,AccessExclusiveLock);
		RelationClose(VariableRelation);
		elog(ERROR, "VariableRelationGetNextXid: ReadBuffer failed");
	}

	var = (VariableRelationContents) BufferGetBlock(buf);

		if (OidIsValid(var->nextOid)) {
			oid_ret = var->nextOid;
			var->nextOid += oid_prefetch;
			FlushBuffer(VariableRelation,buf);
		} else {
			oid_ret = BootstrapObjectIdData;
			var->nextOid = BootstrapObjectIdData + oid_prefetch;
			FlushBuffer(VariableRelation, buf);
		}

	UnlockRelation(VariableRelation,AccessExclusiveLock);
	RelationClose(VariableRelation);
	return oid_ret;
}

/* ----------------------------------------------------------------
 *				transaction id generation support
 * ----------------------------------------------------------------
 */

/* ----------------
 *		GetNewTransactionId
 *
 *		In the version 2 transaction system, transaction id's are
 *		restricted in several ways.
 *
 *		-- Old comments removed
 *
 *		Second, since we may someday preform compression of the data
 *		in the log and time relations, we cause the numbering of the
 *		transaction ids to begin at 512.  This means that some space
 *		on the page of the log and time relations corresponding to
 *		transaction id's 0 - 510 will never be used.  This space is
 *		in fact used to store the version number of the postgres
 *		transaction log and will someday store compression information
 *		about the log.	-- this is also old comments...
 *
 * ----------------
 */

static int checker = 0;

TransactionId
GetNewTransactionId(void)
{
        TransactionId xid = InvalidTransactionId;
	/* ----------------
	 *	during bootstrap initialization, we return the special
	 *	bootstrap transaction id.
	 * ----------------
	 */
	if (AMI_OVERRIDE)
	{
		return AmiTransactionId;
	}

	SpinAcquire(OidGenLockId);	/* not good for concurrency... */

	while (ShmemVariableCache->xid_count <= 0)
	{
		if ( ShmemVariableCache->xid_count == 0 ) {
			TransactionId nextid;
			
			ShmemVariableCache->xid_count = -1;

			SpinRelease(OidGenLockId);	/* not good for concurrency... */
			nextid = VariableRelationGetNextXid() + 1;
			SpinAcquire(OidGenLockId);	/* not good for concurrency... */

			ShmemVariableCache->nextXid = nextid;
			ShmemVariableCache->xid_count = xid_prefetch;
		} else {
			SpinRelease(OidGenLockId);
			/*usleep(50);*/
			SpinAcquire(OidGenLockId);
		}
	}

	xid = ShmemVariableCache->nextXid++;
	(ShmemVariableCache->xid_count)--;
	
	checker = xid;
	SpinRelease(OidGenLockId);
    return xid;
}

/*
 * Like GetNewTransactionId reads nextXid but don't fetch it.
 */
TransactionId
ReadNewTransactionId(void)
{
        TransactionId xid = InvalidTransactionId;

	/* ----------------
	 *	during bootstrap initialization, we return the special
	 *	bootstrap transaction id
	 * ----------------
	 */
	if (AMI_OVERRIDE)
	{
		return AmiTransactionId;
	}

	SpinAcquire(OidGenLockId);	/* not good for concurrency... */

	/*
	 * Note that we don't check is ShmemVariableCache->xid_count equal to
	 * 0 or not. This will work as long as we don't call
	 * ReadNewTransactionId() before GetNewTransactionId().
	 */
	if (ShmemVariableCache->nextXid <= 0)
		elog(ERROR, "ReadNewTransactionId: ShmemVariableCache->nextXid is not initialized");

	xid = ShmemVariableCache->nextXid;

	SpinRelease(OidGenLockId);
        
        return xid;
}

Oid
GetNewObjectId(void) /* place to return the new object id */
{
	Oid retoid;
	pthread_mutex_lock(&oid_access);
	while ( oid_queue_count <= 0 ) {
		if ( oid_queue_count == 0 ) {
			oid_queue_count = -1;
			pthread_mutex_unlock(&oid_access);
			nextoid = VariableRelationGetNextOid() + 1;
			pthread_mutex_lock(&oid_access);
			oid_queue_count = oid_prefetch;
		} else {
			pthread_mutex_unlock(&oid_access);
            /*usleep(50);*/
			pthread_mutex_lock(&oid_access);
		}
	}
	retoid = nextoid++;
	oid_queue_count--;
	pthread_mutex_unlock(&oid_access);
	return retoid;
}

Oid
GetGenId(void)
{
	return ~0;
}

/*  the low water mark is set every time vacuum is called
	We assume that any transaction id lower than this 
	is XID_COMMIT b/c vacuum has already removed anything 
	below it or set its heap flag as HEAP_XMAX_INVALID - MKS - 11.5.2000
*/

void 
InitTransactionLowWaterMark()
{
	VariableRelationContents var;
	Buffer 			first; 
	Block 			fb;
/*	
	Relation 		datar;
*/	
	Header*			header;
	Relation		VariableRelation = RelationNameGetRelation(VariableRelationName,DEFAULTDBOID);


/*	LockRelation(VariableRelation,ExclusiveLock);   */
        if ( IsMultiuser() ) {
            char* pre = GetProperty("transaction_prefetch");
            char* oid = GetProperty("objectid_prefetch");
            if ( pre != NULL ) {
                xid_prefetch = atoi(pre);
            } else {
                xid_prefetch *= 1024;
            }
            if ( xid_prefetch <= 0 ) xid_prefetch = VAR_XID_PREFETCH * 1024;
            if ( oid != NULL ) {
                oid_prefetch = atoi(oid);
            } else {
                oid_prefetch *= VAR_OID_PREFETCH;  // square if not set and multiuser
            }
            if ( oid_prefetch <= 0 ) oid_prefetch = VAR_OID_PREFETCH;
        }
	first = ReadBuffer(VariableRelation,1);
        if (!BufferIsValid(first) ) elog(ERROR,"bad buffer read in variable logging");
	fb = BufferGetBlock(first);
	header = (Header*)fb;
	
	ShmemVariableCache->xid_low_water_mark = header->baseline;
	
	ReleaseBuffer(VariableRelation,first);

/*
	*
	*  Now need to get the checkpoint which is the next xid at startup
	*   this help`s ferret out charsed xids in time tests
*/

	first = ReadBuffer(VariableRelation, 0);
    if (!BufferIsValid(first) ) elog(ERROR,"bad buffer read in variable logging");

	var = (VariableRelationContents) BufferGetBlock(first);

	ShmemVariableCache->xid_checkpoint = var->nextXidData;

	ReleaseBuffer(VariableRelation,first);

/*  initialize access to the oid mutex  */
	pthread_mutex_init(&oid_access,NULL);
	
/*	UnlockRelation(LogRelation,ExclusiveLock);    */
	
	RelationClose(VariableRelation);
}

TransactionId
GetCheckpointId(void) {
    TransactionId xid;
	xid = ShmemVariableCache->xid_checkpoint;
        return xid;
}

/****
*
*	SInvalLock should be held when setting this 
*
*
**/
void
SetCheckpointId(TransactionId xid) {
	ShmemVariableCache->xid_checkpoint = xid;
}

bool
TransactionIdBeforeCheckpoint(TransactionId xid) {
        bool before = false;
        
        if ( !TransactionIdIsValid(xid) ) elog(ERROR,"testing invalid id for checkpoint");

	before = (xid < ShmemVariableCache->xid_checkpoint);

        return before;
}


TransactionId 
GetTransactionRecoveryCheckpoint()
{
	Buffer 			header;
	Block 			hb;
	Header*			theader;
	TransactionId  recover;

	Relation		VariableRelation = RelationNameGetRelation(VariableRelationName,DEFAULTDBOID);

	header = ReadBuffer(VariableRelation,1);
                if (!BufferIsValid(header) ) elog(ERROR,"bad buffer read in variable logging");
	LockBuffer((VariableRelation),header,BUFFER_LOCK_EXCLUSIVE);
        
	hb = BufferGetBlock(header);
	theader = (Header*)hb;
	recover = ( theader->checkpoint > theader->baseline ) ? theader->checkpoint : theader->baseline;
	elog(DEBUG,"transaction recovery checkpoint is %llu",recover);

        LockBuffer((VariableRelation),header,BUFFER_LOCK_UNLOCK);
        ReleaseBuffer(VariableRelation,header);
	RelationClose(VariableRelation);	
	return recover;
}


void 
SetTransactionRecoveryCheckpoint(TransactionId recover)
{
	Buffer 			header;
	Block 			hb;
	Header*			theader;

	Relation		VariableRelation = RelationNameGetRelation(VariableRelationName,DEFAULTDBOID);

	header = ReadBuffer(VariableRelation,1);
                if (!BufferIsValid(header) ) elog(ERROR,"bad buffer read in variable logging");
	LockBuffer((VariableRelation),header,BUFFER_LOCK_EXCLUSIVE);
        hb = BufferGetBlock(header);
	theader = (Header*)hb;
	theader->checkpoint = recover;
	elog(DEBUG,"recording transaction recovery checkpoint at %llu",recover);

		
        LockBuffer((VariableRelation),header,BUFFER_LOCK_UNLOCK);
	FlushBuffer(VariableRelation, header);
	
	RelationClose(VariableRelation);	
}


void 
SetTransactionLowWaterMark(TransactionId lowwater)
{
	Buffer 			header;
	Block 			hb;
	LogBase*		area;
	Header*			theader;
	Oid  dbid = GetDatabaseId();

	Relation		VariableRelation = RelationNameGetRelation(VariableRelationName,DEFAULTDBOID);
	
/*	MasterWriteLock();	*/
/*	LockRelation(VariableRelation,ExclusiveLock);     */
/*  first we scan the first page of pg_log for the DatabaseId or 0 we do seq. scan 
	b/c this op should not happen often
*/
	header = ReadBuffer(VariableRelation,1);
                if (!BufferIsValid(header) ) elog(ERROR,"bad buffer read in variable logging");
	LockBuffer((VariableRelation),header,BUFFER_LOCK_EXCLUSIVE);
        hb = BufferGetBlock(header);
	theader = (Header*)hb;

	for (area=&theader->databases;area->database != GetDatabaseId();area++ ) {
		if ( (char*)area > (char*)hb + BLCKSZ ) {
                        LockBuffer((VariableRelation),header,BUFFER_LOCK_UNLOCK);
			elog(FATAL,"Log cannot hold database info");
			return;
		}
		if ( area->init == false ) {
			area->database = dbid;
			area->init = true;
			break;
		}
	}
	
	area->lowwater = lowwater;
	elog(DEBUG,"recording transaction low water mark for db %lu at %llu",dbid,lowwater);

		
/*	UnlockRelation(VariableRelation,ExclusiveLock);    */
        LockBuffer((VariableRelation),header,BUFFER_LOCK_UNLOCK);
	FlushBuffer(VariableRelation,header);
	
	RelationClose(VariableRelation);	
/*	MasterUnLock();			*/

}

TransactionId
GetTransactionLowWaterMark(void)
{
	TransactionId tid = InvalidTransactionId;
 /*       SpinAcquire(OidGenLockId);  */
        tid = ShmemVariableCache->xid_low_water_mark;
/*        SpinRelease(OidGenLockId);    */
        return tid;
}

void 
VacuumTransactionLog()
{
	Buffer 			first; 
	Block 			fb;
	TransactionId  		low = ~0ULL;

	BlockNumber    		base;

	Buffer 			zeroblock;
	Buffer 			newzero;
	Block 			zb;
	Block 			bb;

	LogBase*		area;
	
	Relation 		datar;
	HeapScanDesc		scan;
	HeapTuple 		dbtuple;
	
	Header*			header;
	
	Relation		VariableRelation =  RelationNameGetRelation(VariableRelationName,DEFAULTDBOID);
	Relation		LogRelation =  RelationNameGetRelation(LogRelationName,DEFAULTDBOID);

	
	
	first = ReadBuffer(VariableRelation,1);
        if (!BufferIsValid(first) ) elog(ERROR,"bad buffer read in variable logging");
	
        fb = BufferGetBlock(first);
	
	datar = heap_openr(DatabaseRelationName,NoLock);
	if ( datar == NULL )
	{
                ReleaseBuffer(VariableRelation,first);
		return;
	} else {
		header = (Header*)fb;
		scan = heap_beginscan(datar,SnapshotNow,0,NULL);
		dbtuple = heap_getnext(scan);
		while ( HeapTupleIsValid(dbtuple) ) {
			Oid   			did = dbtuple->t_data->t_oid;
			TransactionId		dbmin = 0ULL;
			for (area=&header->databases;area->init == true;area++ ) {
				if ( area->database == did ) {
					dbmin = area->lowwater;
				}
			}
			if ( low > dbmin ) low = dbmin;
			dbtuple = heap_getnext(scan);
		}
		heap_endscan(scan);
		heap_close(datar,NoLock);
	}
/*  
	We need to compute block number before 
	we set the ShmemVariableCache
	so that it computes the block properly 
*/	
   base = TransComputeBlockNumber(LogRelation,low);
   elog(DEBUG,"Initializing transaction log - current checkpoint %llu",ShmemVariableCache->xid_low_water_mark);
   elog(DEBUG,"Initializing transaction log - current startup id %llu",ShmemVariableCache->xid_checkpoint);

	if ( low > ShmemVariableCache->xid_low_water_mark ) {
		elog(DEBUG,"moving transaction checkpoint from %llu to %llu",ShmemVariableCache->xid_low_water_mark,low);
		ShmemVariableCache->xid_low_water_mark = low;   
		header->baseline = low;
	}
		
        FlushBuffer(VariableRelation, first);

	if ( base != 0 ) {                
		zeroblock = ReadBuffer(LogRelation,0);
                if (!BufferIsValid(zeroblock) ) elog(ERROR,"bad buffer read in variable logging");
		newzero = ReadBuffer(LogRelation,base);
                if (!BufferIsValid(newzero) ) elog(ERROR,"bad buffer read in variable logging");

		zb = BufferGetBlock(zeroblock);
		bb = BufferGetBlock(newzero);
			
		memcpy(zb,bb,BLCKSZ);
	
		FlushBuffer(LogRelation, zeroblock);    
		FlushBuffer(LogRelation, newzero);     
		/*
		i = FlushRelationBuffers(LogRelation,1);
		if (i < 0)
			elog(FATAL, "VACUUM (vc_repair_frag): FlushRelationBuffers returned %d", i);
                */
                FlushAllDirtyBuffers(true);
		smgrtruncate(LogRelation->rd_smgr, 1);
                base = 1;
	} else {
                elog(DEBUG,"No change made to log");
	}

        elog(DEBUG,"Done initializing transaction log");
	
	RelationClose(VariableRelation);
	RelationClose(LogRelation);
}
