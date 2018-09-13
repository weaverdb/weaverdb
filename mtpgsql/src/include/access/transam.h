/*-------------------------------------------------------------------------
 *
 * transam.h
 *	  postgres transaction access method support code header
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * $Id: transam.h,v 1.2 2006/08/13 00:39:43 synmscott Exp $
 *
 *	 NOTES
 *		Transaction System Version 101 now support proper oid
 *		generation and recording in the variable relation.
 *
 *-------------------------------------------------------------------------
 */
#ifndef TRANSAM_H
#define TRANSAM_H


#include "storage/bufmgr.h"
#include "storage/block.h"
#include "storage/buf_internals.h"
#include "access/xact.h"

/* ----------------
 *		transaction system version id
 *
 *		this is stored on the first page of the log, time and variable
 *		relations on the first 4 bytes.  This is so that if we improve
 *		the format of the transaction log after postgres version 2, then
 *		people won't have to rebuild their databases.
 *
 *		TRANS_SYSTEM_VERSION 100 means major version 1 minor version 0.
 *		Two databases with the same major version should be compatible,
 *		even if their minor versions differ.
 * ----------------
 */
#define TRANS_SYSTEM_VERSION	200

/* ----------------
 *		transaction id status values
 *
 *		someday we will use "11" = 3 = XID_COMMIT_CHILD to mean the
 *		commiting of child xactions.
 * ----------------
 */
#define XID_COMMIT			3	/* transaction commited */
#define XID_ABORT			1	/* transaction aborted */
#define XID_INPROGRESS			0	/* transaction in progress */
#define XID_SOFT_COMMIT			2	

#define XID_COMMIT_TEST                 2      /*  check to see if the commit bit is set, soft or hard */
#define XID_SOFT_COMMIT_TEST            3      
#define XID_HARD_COMMIT_TEST           4
#define XID_ABORT_TEST                  1
#define XID_INPROGRESS_TEST            0




/*  need soft commit to mark a transaction as commited but not 
fsynced.  If the backend goes into recovery mode, all soft commits
are changed to aborts   MKS  2.13.2001  */

typedef unsigned char XidStatus;/* (2 bits) */

/* ----------
 *		note: we reserve the first 16384 object ids for internal use.
 *		oid's less than this appear in the .bki files.  the choice of
 *		16384 is completely arbitrary.
 * ----------
 */
#define BootstrapObjectIdData 16384

/* ----------------
 *		BitIndexOf computes the index of the Nth xid on a given block
 * ----------------
 */
#define BitIndexOf(N)	((N) * 2)

/* ----------------
 *		transaction page definitions
 * ----------------
 */
#define TP_DataSize				BLCKSZ
#define TP_NumXidStatusPerBlock (TP_DataSize * 4)

/* ----------------
 *		LogRelationContents structure
 *
 *		This structure describes the storage of the data in the
 *		first 128 bytes of the log relation.  This storage is never
 *		used for transaction status because transaction id's begin
 *		their numbering at 512.
 *
 *		The first 4 bytes of this relation store the version
 *		number of the transction system.
 * ----------------
 */
typedef struct LogRelationContentsData
{
	double			TransSystemVersion;
} LogRelationContentsData;

typedef LogRelationContentsData *LogRelationContents;

/* ----------------
 *		VariableRelationContents structure
 *
 *		The variable relation is a special "relation" which
 *		is used to store various system "variables" persistantly.
 *		Unlike other relations in the system, this relation
 *		is updated in place whenever the variables change.
 *
 *		The first 4 bytes of this relation store the version
 *		number of the transction system.
 *
 *		Currently, the relation has only one page and the next
 *		available xid, the last committed xid and the next
 *		available oid are stored there.
 * ----------------
 */
typedef struct VariableRelationContentsData
{
	double			TransSystemVersion;
	TransactionId 		nextXidData;
	TransactionId 		lastXidData;	/* unused */
	Oid			nextOid;
} VariableRelationContentsData;

typedef VariableRelationContentsData *VariableRelationContents;

/*
 * VariableCache is placed in shmem and used by backends to
 * get next available XID & OID without access to
 * variable relation. Actually, I would like to have two
 * different on-disk storages for next XID and OID...
 * But hoping that someday we will use per database OID
 * generator I leaved this as is.	- vadim 07/21/98
 */
typedef struct VariableCacheData
{
	int32		xid_count;
	TransactionId 	nextXid;
	int32		oid_count;		/* not implemented, yet */
	Oid		nextOid;
	int 		buffers;
	int 		maxbackends;
        int             numberOfLockTables;
        TransactionId   xid_low_water_mark;
	TransactionId   xid_checkpoint;
} VariableCacheData;

typedef VariableCacheData *VariableCache;



/* ----------------
 *		 extern  declarations
 * ----------------
 */

/*
 * prototypes for functions in transam/transam.c
 */
 #ifdef __cplusplus
extern "C" {
#endif

PG_EXTERN bool InitializeTransactionLog(void);
PG_EXTERN bool TransactionIdDidCommit(TransactionId transactionId);
PG_EXTERN bool TransactionIdDidSoftCommit(TransactionId transactionId);
PG_EXTERN bool TransactionIdDidHardCommit(TransactionId transactionId);
PG_EXTERN bool TransactionIdDidAbort(TransactionId transactionId);
PG_EXTERN bool TransactionIdDidCrash(TransactionId transactionId);

PG_EXTERN void SetRecoveryCheckingEnabled(bool state);

/* in transam/transsup.c */
PG_EXTERN void AmiTransactionOverride(bool flag);
PG_EXTERN BlockNumber TransComputeBlockNumber(Relation relation,
			  TransactionId transactionId);
PG_EXTERN XidStatus TransBlockNumberGetXidStatus(Relation relation,TransactionId xid, bool *failP);
PG_EXTERN void TransBlockNumberSetXidStatus(Relation relation, TransactionId xid, XidStatus xstatus);
PG_EXTERN void TransBlockSetXidStatus(Block tb,TransactionId transactionId, XidStatus xstatus);
/* in transam/varsup.c */
PG_EXTERN void VariableRelationPutNextXid(TransactionId xid);
PG_EXTERN TransactionId GetNewTransactionId(void);
PG_EXTERN TransactionId ReadNewTransactionId(void);
PG_EXTERN Oid GetNewObjectId(void);

PG_EXTERN Oid GetGenId(void);

PG_EXTERN bool TransactionIdBeforeCheckpoint(TransactionId xid);
PG_EXTERN void  SetCheckpointId(TransactionId xid);
PG_EXTERN TransactionId GetCheckpointId(void);
PG_EXTERN void InitTransactionLowWaterMark(void);
PG_EXTERN void SetTransactionLowWaterMark(TransactionId lowwater);
PG_EXTERN TransactionId GetTransactionLowWaterMark(void);
PG_EXTERN TransactionId GetTransactionRecoveryCheckpoint(void);
PG_EXTERN void SetTransactionRecoveryCheckpoint(TransactionId recover);
PG_EXTERN void VacuumTransactionLog(void);

#ifdef __cplusplus
}
#endif
/* ----------------
 *		global variable extern declarations
 * ----------------
 */

/* in transam.c */

extern TransactionId NullTransactionId;
extern TransactionId AmiTransactionId;
extern TransactionId FirstTransactionId;

/* in transsup.c */

extern bool AMI_OVERRIDE;

/* in varsup.c */

extern int	OidGenLockId;

#endif	 /* TRAMSAM_H */
