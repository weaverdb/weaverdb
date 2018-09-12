/*-------------------------------------------------------------------------
 *
 * xact.h
 *	  postgres transaction system header
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * $Id: xact.h,v 1.1.1.1 2006/08/12 00:22:10 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */
#ifndef XACT_H
#define XACT_H

#include "access/transam.h"
#include "utils/nabstime.h"

/* ----------------
 *		transaction state structure
 * ----------------
 */
typedef struct TransactionStateData {
	TransactionId   transactionIdData;
	CommandId	commandId;
	CommandId	scanCommandId;
	AbsoluteTime    startTime;
	int             state;
	int             blockState;
} TransactionStateData;


typedef TransactionStateData *TransactionState;

typedef struct TransactionInfoStruct {
	TransactionState	CurrentTransactionState;
	int			XactIsoLevel;   
        bool 			SharedBufferChanged;
        bool 			CommitTime;
/*  transam.h */
	unsigned char   	cachedTestXidStatus; 
	TransactionId 		cachedTestXid;
	int			RecoveryCheckingEnableState;
	bool			CommandIdCounterOverflowFlag;
        int               backupState;
 /*  Log Relation Cache  */       
 	Relation		LogRelation;
} TransactionInfo;
/*
 * Xact isolation levels
 */
#define XACT_DIRTY_READ			0		/* not implemented */
#define XACT_READ_COMMITTED		1
#define XACT_REPEATABLE_READ	2		/* not implemented */
#define XACT_SERIALIZABLE		3
#define XACT_ALL		4
#define XACT_USER		5

extern int	DefaultXactIsoLevel;
extern TransactionId DisabledTransactionId;

/* ----------------
 *		transaction states
 * ----------------
 */
#define TRANS_DEFAULT			0
#define TRANS_START				1
#define TRANS_INPROGRESS		2
#define TRANS_COMMIT			3
#define TRANS_ABORT				4

/* ----------------
 *		transaction block states
 * ----------------
 */
#define TBLOCK_DEFAULT			0
/*
#define TBLOCK_BEGIN			1
#define TBLOCK_INPROGRESS		2
#define TBLOCK_END				3
#define TBLOCK_ABORT			4
#define TBLOCK_ENDABORT			5
*/
#define TBLOCK_AUTO				6
#define TBLOCK_MANUAL				7
#define TBLOCK_ABORT				8
#define TBLOCK_COMMIT				9
#define TBLOCK_ABORTONLY				10

#define TransactionIdIsValid(xid) ((bool) (xid != NullTransactionId))


/* ----------------------------------------------------------------
 *		TransactionIdEquals
 * ----------------------------------------------------------------
 */
#define TransactionIdEquals(id1, id2) \
( \
	((bool) ((id1) == (id2))) \
)


/* ----------------
 *		 extern definitions
 * ----------------
 */
 #ifdef __cplusplus
extern  "C" {
 #endif

PG_EXTERN bool IsAbortedTransactionBlockState(void);
PG_EXTERN bool IsTransactionSystemDisabled(void);

PG_EXTERN TransactionId GetCurrentTransactionId(void);
PG_EXTERN CommandId GetCurrentCommandId(void);
PG_EXTERN CommandId GetScanCommandId(void);
PG_EXTERN void SetScanCommandId(CommandId);
PG_EXTERN AbsoluteTime GetCurrentTransactionStartTime(void);
PG_EXTERN bool TransactionIdIsCurrentTransactionId(TransactionId xid);
PG_EXTERN bool CommandIdIsCurrentCommandId(CommandId cid);
PG_EXTERN bool CommandIdGEScanCommandId(CommandId cid);
PG_EXTERN void CommandCounterIncrement(void);
PG_EXTERN void InitializeTransactionSystem(void);
PG_EXTERN bool CurrentXactInProgress(void);
PG_EXTERN void StartTransactionCommand(void);
PG_EXTERN void CommitTransactionCommand(void);
PG_EXTERN void AbortTransactionBlock(void);
PG_EXTERN void SetAbortOnly(void);
PG_EXTERN void BeginTransactionBlock(void);
PG_EXTERN void CommitTransactionBlock(void);
PG_EXTERN bool IsTransactionBlock(void);

PG_EXTERN void StartTransaction(void);
PG_EXTERN void CloneParentTransaction(void);
PG_EXTERN void CloseSubTransaction(void);
PG_EXTERN void AbortTransaction(void);
PG_EXTERN void CommitTransaction(void);

/* defined in xid.c */
PG_EXTERN TransactionId xidin(char *representation);
PG_EXTERN char *xidout(TransactionId* transactionId);
PG_EXTERN bool xideq(TransactionId* xid1, TransactionId* xid2);
PG_EXTERN void TransactionIdAdd(TransactionId *xid, int value);

PG_EXTERN bool xidint8_equals(TransactionId* xid,int64 comp);
PG_EXTERN bool xidint8_lt(TransactionId* xid,int64 comp);
PG_EXTERN bool xidint8_gt(TransactionId* xid,int64 comp);
PG_EXTERN bool xidint8_lteq(TransactionId* xid,int64 comp);
PG_EXTERN bool xidint8_gteq(TransactionId* xid,int64 comp);
PG_EXTERN bool xidint8_noteq(TransactionId* xid,int64 comp);

PG_EXTERN bool xidint4_equals(TransactionId* xid,int32 comp);
PG_EXTERN bool xidint4_lt(TransactionId* xid,int32 comp);
PG_EXTERN bool xidint4_gt(TransactionId* xid,int32 comp);
PG_EXTERN bool xidint4_lteq(TransactionId* xid,int32 comp);
PG_EXTERN bool xidint4_gteq(TransactionId* xid,int32 comp);
PG_EXTERN bool xidint4_noteq(TransactionId* xid,int32 comp);

PG_EXTERN TransactionInfo* GetTransactionInfo(void);

 #ifdef __cplusplus
 }
 #endif

#endif	 /* XACT_H */
