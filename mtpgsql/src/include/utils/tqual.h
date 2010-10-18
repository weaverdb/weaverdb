/*-------------------------------------------------------------------------
 *
 * tqual.h
 *	  POSTGRES "time" qualification definitions.
 *
 *	  Should be moved/renamed...	- vadim 07/28/98
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * $Id: tqual.h,v 1.2 2006/08/15 18:24:28 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */
#ifndef TQUAL_H
#define TQUAL_H


#include "access/htup.h"
#include "access/xact.h"

typedef struct SnapshotData
{
	TransactionId xmin;			/* XID < xmin are visible to me */
	TransactionId xmax;			/* XID >= xmax are invisible to me */
	uint32		xcnt;			/* # of xact below */
	bool		isUser;
	TransactionId *xip;			/* array of xacts in progress */
	ItemPointerData tid;		/* required for Dirty snapshot -:( */
} SnapshotData;

typedef SnapshotData * Snapshot;


typedef struct SnapshotHolder {
    Snapshot 		SnapshotDirty;    
    Snapshot            UserSnapshot;
    Snapshot 		QuerySnapshot;
    Snapshot 		SerializableSnapshot;
    bool                ReferentialIntegritySnapshotOverride;
} SnapshotHolder;



#define SnapshotNow					((Snapshot) 0x0)
#define SnapshotSelf				((Snapshot) 0x1)
#define SnapshotAny					((Snapshot) 0x2)

#define IsSnapshotNow(snapshot)		((Snapshot) (snapshot) == SnapshotNow)
#define IsSnapshotSelf(snapshot)	((Snapshot) (snapshot) == SnapshotSelf)
#define IsSnapshotAny(snapshot)		((Snapshot) (snapshot) == SnapshotAny)
#define IsSnapshotDirty(env,snapshot)	((Snapshot) (snapshot) == ((SnapshotHolder*)(env))->SnapshotDirty)

#define GetSnapshotDirty(env) ( ((SnapshotHolder*)(env))->SnapshotDirty )
#define GetSnapshotQuery(env) ( ((SnapshotHolder*)(env))->QuerySnapshot )
/*
 * HeapTupleSatisfiesVisibility
 *		True iff heap tuple satsifies a time qual.
 *
 * Notes:
 *		Assumes heap tuple is valid.
 *		Beware of multiple evaluations of arguments.
 */
#define HeapTupleSatisfiesVisibility(env, tuple, snapshot) \
( \
	TransactionIdEquals((tuple)->t_data->t_xmax, AmiTransactionId) ? \
		false \
	: \
	( \
		IsSnapshotAny(snapshot) ? \
			true \
		: \
			(IsSnapshotSelf(snapshot) ? \
				HeapTupleSatisfiesItself((tuple)->t_data) \
			: \
				(IsSnapshotDirty(env,snapshot) ? \
					HeapTupleSatisfiesDirty(env,(tuple)->t_data,snapshot) \
				: \
					(IsSnapshotNow(snapshot) ? \
						HeapTupleSatisfiesNow(env,(tuple)->t_data) \
					: \
						HeapTupleSatisfiesSnapshot(env,(tuple)->t_data, snapshot) \
					) \
			) \
		) \
	) \
)

#define HeapTupleMayBeUpdated		0
#define HeapTupleInvisible			1
#define HeapTupleSelfUpdated		2
#define HeapTupleUpdated			3
#define HeapTupleBeingUpdated		4

#ifdef __cplusplus
extern "C" {
#endif
PG_EXTERN SnapshotHolder* GetSnapshotHolder(void);
PG_EXTERN bool HeapTupleSatisfiesItself(HeapTupleHeader tuple);
PG_EXTERN bool HeapTupleSatisfiesNow(void* env,HeapTupleHeader tuple);
PG_EXTERN bool HeapTupleSatisfiesDirty(void* env,HeapTupleHeader tuple, Snapshot snapshot);
PG_EXTERN bool HeapTupleSatisfiesSnapshot(void* env,HeapTupleHeader tuple,
						   Snapshot snapshot);
PG_EXTERN int	HeapTupleSatisfiesUpdate(void* env,HeapTuple tuple, Snapshot snapshot);

PG_EXTERN Snapshot GetSnapshotData(bool serializable);
PG_EXTERN void SetQuerySnapshot(void);
PG_EXTERN void FreeXactSnapshot(void);
PG_EXTERN void TakeUserSnapshot(void);
PG_EXTERN void DropUserSnapshot(void);
PG_EXTERN void CopySnapshot(Snapshot source, Snapshot dest);

/* Result codes for HeapTupleSatisfiesVacuum */
typedef enum
{
	HEAPTUPLE_DEAD,				/* tuple is dead and deletable */
	HEAPTUPLE_LIVE,				/* tuple is live (committed, no deleter) */
	HEAPTUPLE_HARDENED,			/* tuple is live and older than oldest running transaction (committed, no deleter) */
	HEAPTUPLE_RECENTLY_DEAD,		/* tuple is dead, but not deletable yet */
	HEAPTUPLE_STILLBORN,
	HEAPTUPLE_INSERT_IN_PROGRESS,	/* inserting xact is still in  */
	HEAPTUPLE_DELETE_IN_PROGRESS		/* deleting xact is still in progress */
} HTSV_Result;

PG_EXTERN HTSV_Result HeapTupleSatisfiesVacuum(HeapTupleHeader tuple,
						 TransactionId OldestXmin);


#ifdef __cplusplus
}
#endif

#endif	 /* TQUAL_H */
