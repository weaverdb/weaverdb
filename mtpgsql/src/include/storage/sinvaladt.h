/*-------------------------------------------------------------------------
 *
 * sinvaladt.h
 *	  POSTGRES shared cache invalidation segment definitions.
 *
 *
 * Portions Copyright (c) 2000-2024, Myron Scott  <myron@weaverdb.org>
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 *
 *-------------------------------------------------------------------------
 */
#ifndef SINVALADT_H
#define SINVALADT_H

#include "storage/ipc.h"
#include "storage/itemptr.h"
#include "storage/shmem.h"

/*
 * The shared cache invalidation manager is responsible for transmitting
 * invalidation messages between backends.	Any message sent by any backend
 * must be delivered to all already-running backends before it can be
 * forgotten.
 *
 * Conceptually, the messages are stored in an infinite array, where
 * maxMsgNum is the next array subscript to store a submitted message in,
 * minMsgNum is the smallest array subscript containing a message not yet
 * read by all backends, and we always have maxMsgNum >= minMsgNum.  (They
 * are equal when there are no messages pending.)  For each active backend,
 * there is a nextMsgNum pointer indicating the next message it needs to read;
 * we have maxMsgNum >= nextMsgNum >= minMsgNum for every backend.
 *
 * In reality, the messages are stored in a circular buffer of MAXNUMMESSAGES
 * entries.  We translate MsgNum values into circular-buffer indexes by
 * computing MsgNum % MAXNUMMESSAGES (this should be fast as long as
 * MAXNUMMESSAGES is a constant and a power of 2).	As long as maxMsgNum
 * doesn't exceed minMsgNum by more than MAXNUMMESSAGES, we have enough space
 * in the buffer.  If the buffer does overflow, we reset it to empty and
 * force each backend to "reset", ie, discard all its invalidatable state.
 *
 * We would have problems if the MsgNum values overflow an integer, so
 * whenever minMsgNum exceeds MSGNUMWRAPAROUND, we subtract MSGNUMWRAPAROUND
 * from all the MsgNum variables simultaneously.  MSGNUMWRAPAROUND can be
 * large so that we don't need to do this often.  It must be a multiple of
 * MAXNUMMESSAGES so that the existing circular-buffer entries don't need
 * to be moved when we do it.
 */


/*
 * Configurable parameters.
 *
 * MAXNUMMESSAGES: max number of shared-inval messages we can buffer.
 * Must be a power of 2 for speed.
 *
 * MSGNUMWRAPAROUND: how often to reduce MsgNum variables to avoid overflow.
 * Must be a multiple of MAXNUMMESSAGES.  Should be large.
 */

#define MAXNUMMESSAGES 4096
#define MSGNUMWRAPAROUND (MAXNUMMESSAGES * 4096)

/* The content of one shared-invalidation message */
typedef struct SharedInvalidData
{
	int			cacheId;		/* XXX */
	Index		hashIndex;
	ItemPointerData pointerData;
} SharedInvalidData;

typedef SharedInvalidData *SharedInvalid;

/* Per-backend state in shared invalidation structure */
typedef struct ProcState
{
	/* nextMsgNum is -1 in an inactive ProcState array entry. */
	int			nextMsgNum;		/* next message number to read, or -1 */
	int                    resetState;		/* true, if backend has to reset its state */
	int			tag;			/* backend tag received from postmaster */
	SHMEM_OFFSET procStruct;	/* location of backend's PROC struct */
} ProcState;

/* Shared cache invalidation memory segment */
typedef struct SISeg
{

	/*
	 * General state information
	 */
	int			minMsgNum;		/* oldest message still needed */
	int			maxMsgNum;		/* next message number to be assigned */
	int			maxBackends;	/* size of procState array */
	
	int 			nextBackendTag;
	/*
	 * Circular buffer holding shared-inval messages
	 */
	SharedInvalidData buffer[MAXNUMMESSAGES];

	/*
	 * Per-backend state info.
	 *
	 * We declare procState as 1 entry because C wants a fixed-size array,
	 * but actually it is maxBackends entries long.
	 */
	ProcState	procState[1];	/* reflects the invalidation state */
} SISeg;


extern SISeg *shmInvalBuffer;	/* pointer to the shared buffer segment,
								 * set by SISegmentAttach() */


/*
 * prototypes for functions in sinvaladt.c
 */
  #ifdef __cplusplus
extern "C" {
#endif
 
PG_EXTERN int SISegmentInit(bool createNewSegment, IPCKey key,
			  int maxBackends);
PG_EXTERN int	SIBackendInit(SISeg *segP);
PG_EXTERN bool  SIResetProcState(SISeg *segP);
PG_EXTERN bool SIInsertDataEntry(SISeg *segP, SharedInvalidData *data);
PG_EXTERN int SIGetDataEntry(SISeg *segP, int backendId,
			   SharedInvalidData *data);
PG_EXTERN void SIDelExpiredDataEntries(SISeg *segP);
PG_EXTERN void CallableCleanupInvalidationState(void); 
 
PG_EXTERN int  CallableInitInvalidationState(void);

  #ifdef __cplusplus
}
#endif

#endif	 /* SINVALADT_H */
