/*-------------------------------------------------------------------------
 *
 * sinvaladt.c
 *	  POSTGRES shared cache invalidation segment definitions.
 *
 * Portions Copyright (c) 2000-2024, Myron Scott  <myron@weaverdb.org>
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *
 *
 *-------------------------------------------------------------------------
 */
#include <signal.h>
#include <unistd.h>

#include "postgres.h"
#include "env/env.h"

#include "miscadmin.h"
#include "storage/backendid.h"
#include "storage/lmgr.h"
#include "storage/multithread.h"
#include "storage/sinval.h"
#include "storage/sinvaladt.h"
#include "utils/trace.h"

SISeg	   *shmInvalBuffer;


static void SISegmentAttach(IpcMemoryId shmid);
static void SISegInit(SISeg *segP, int maxBackends);
static void CleanupInvalidationState(int status, SISeg *segP);
static void SISetProcStateInvalid(SISeg *segP,int state);

/*
 * SISegmentInit
 *		Create a new SI memory segment, or attach to an existing one
 *
 * This is called with createNewSegment = true by the postmaster (or by
 * a standalone backend), and subsequently with createNewSegment = false
 * by backends started by the postmaster.
 *
 * Note: maxBackends param is only valid when createNewSegment is true
 */
int
SISegmentInit(bool createNewSegment, IPCKey key, int maxBackends)
{
	int			segSize;
	IpcMemoryId shmId;

	if (createNewSegment)
	{
		/* Kill existing segment, if any */
		IpcMemoryKill(key);

		/*
		 * Figure space needed. Note sizeof(SISeg) includes the first
		 * ProcState entry.
		 */
		segSize = sizeof(SISeg) + sizeof(ProcState) * (maxBackends - 1);

		/* Get a shared segment */
		shmId = IpcMemoryCreate(key, segSize, IPCProtection);
		if (shmId < 0)
		{
			perror("SISegmentInit: segment create failed");
			return -1;			/* an error */
		}

		/* Attach to the shared cache invalidation segment */
		/* sets the global variable shmInvalBuffer */
		SISegmentAttach(shmId);

		/* Init shared memory contents */
		SISegInit(shmInvalBuffer, maxBackends);
	}
	else
	{
		/* find existing segment */
		shmId = IpcMemoryIdGet(key, 0);
		if (shmId < 0)
		{
			perror("SISegmentInit: segment get failed");
			return -1;			/* an error */
		}

		/* Attach to the shared cache invalidation segment */
		/* sets the global variable shmInvalBuffer */
		SISegmentAttach(shmId);
	}
	return 1;
}

/*
 * SISegmentAttach
 *		Attach to specified shared memory segment
 */
static void
SISegmentAttach(IpcMemoryId shmid)
{
	shmInvalBuffer = (SISeg *) IpcMemoryAttach(shmid);

	if (shmInvalBuffer == IpcMemAttachFailed)
	{
		/* XXX use validity function */
		elog(FATAL, "SISegmentAttach: Could not attach segment: %m");
	}
}

/*
 * SISegInit
 *		Initialize contents of a new shared memory sinval segment
 */
static void
SISegInit(SISeg *segP, int maxBackends)
{
	int			i;

	/* Clear message counters, save size of procState array */
	segP->minMsgNum = 0;
	segP->maxMsgNum = 0;
	segP->maxBackends = maxBackends;

	/* The buffer[] array is initially all unused, so we need not fill it */

	/* Mark all backends inactive */
	for (i = 0; i < maxBackends; i++)
	{
		segP->procState[i].nextMsgNum = -1;		/* inactive */
		segP->procState[i].resetState = 0;
		segP->procState[i].tag = InvalidBackendTag;
		segP->procState[i].procStruct = INVALID_OFFSET;
	}
}

/*
 * SIBackendInit
 *		Initialize a new backend to operate on the sinval buffer
 *
 * NB: this routine, and all following ones, must be executed with the
 * SInvalLock spinlock held, since there may be multiple backends trying
 * to access the buffer.
 */
int
SIBackendInit(SISeg *segP)
{
	int			index =0;
	ProcState  *stateP = NULL;
	
	SetMyBackendTag(segP->nextBackendTag++);

	/* Look for a free entry in the procState array */
	for (index = 0; index < segP->maxBackends; index++)
	{
		if (segP->procState[index].tag == InvalidBackendTag)
		{
			stateP = &segP->procState[index];
			break;
		}
	}
        
        if ( index == segP->maxBackends ) return 0;

	SetMyBackendId(index + 1);

#ifdef	INVALIDDEBUG
	elog(DEBUG, "SIBackendInit: backend tag %d; backend id %d.",
		 GetEnv()->MyBackendTag, MyBackendId);
#endif	 /* INVALIDDEBUG */

	/* mark myself active, with all extant messages already read */
	stateP->nextMsgNum = segP->maxMsgNum;
	stateP->resetState = 0;
	stateP->tag = GetMyBackendTag();
	stateP->procStruct = MAKE_OFFSET(GetMyThread());

	/* register exit routine to mark my entry inactive at exit */
/*	on_shmem_exit(CleanupInvalidationState, (caddr_t) segP);  */
/*  do this on proc exit so it is cleaned up when the process ends  MKS  12.26.2000  */
	if ( !IsMultiuser() ) {
		on_proc_exit(CleanupInvalidationState, (caddr_t) segP);  
	}

	return 1;
}

/*
 * CleanupInvalidationState
 *		Mark the current backend as no longer active.
 *
 * This function is called via on_shmem_exit() during backend shutdown,
 * so the caller has NOT acquired the lock for us.
 */
void
CallableCleanupInvalidationState() {
	int status = 0;
	CleanupInvalidationState(status,shmInvalBuffer);
}
 
int 
CallableInitInvalidationState() {
    int result = 0;
	SpinAcquire(SInvalLock);
	result = SIBackendInit(shmInvalBuffer);
	SpinRelease(SInvalLock);
        return result;
}

static void
CleanupInvalidationState(int status,
						 SISeg *segP)
{
        BackendId  me = GetMyBackendId();

        Assert(PointerIsValid(segP));

	SpinAcquire(SInvalLock);

	segP->procState[me - 1].nextMsgNum = -1;
	segP->procState[me - 1].resetState = 0;
	segP->procState[me - 1].tag = InvalidBackendTag;
	segP->procState[me - 1].procStruct = INVALID_OFFSET;

	SpinRelease(SInvalLock);
}

/*
 * SIInsertDataEntry
 *		Add a new invalidation message to the buffer.
 *
 * If we are unable to insert the message because the buffer is full,
 * then clear the buffer and assert the "reset" flag to each backend.
 * This will cause all the backends to discard *all* invalidatable state.
 *
 * Returns true for normal successful insertion, false if had to reset.
 */
bool
SIInsertDataEntry(SISeg *segP, SharedInvalidData *data)
{
	int			numMsgs = segP->maxMsgNum - segP->minMsgNum;

	/* Is the buffer full? */
	if (numMsgs >= MAXNUMMESSAGES)
	{

		/*
		 * Don't panic just yet: slowest backend might have consumed some
		 * messages but not yet have done SIDelExpiredDataEntries() to
		 * advance minMsgNum.  So, make sure minMsgNum is up-to-date.
		 */
		SIDelExpiredDataEntries(segP);
		numMsgs = segP->maxMsgNum - segP->minMsgNum;
		if (numMsgs >= MAXNUMMESSAGES)
		{
			/* Yup, it's definitely full, no choice but to reset */
			SISetProcStateInvalid(segP,1);
			return false;
		}
	}

	/*
	 * Try to prevent table overflow.  When the table is 70% full send a
	 * SIGUSR2 (ordinarily a NOTIFY signal) to the postmaster, which will
	 * send it back to all the backends.  This will force idle backends to
	 * execute a transaction to look through pg_listener for NOTIFY
	 * messages, and as a byproduct of the transaction start they will
	 * read SI entries.
	 *
	 * This should never happen if all the backends are actively executing
	 * queries, but if a backend is sitting idle then it won't be starting
	 * transactions and so won't be reading SI entries.
	 *
	 * dz - 27 Jan 1998
	 */

	/*
	 * Insert new message into proper slot of circular buffer
	 */
	segP->buffer[segP->maxMsgNum % MAXNUMMESSAGES] = *data;
	segP->maxMsgNum++;

	return true;
}

bool 
SIResetProcState(SISeg *segP)
{
    /*  already have lock  */
   SISetProcStateInvalid(segP,2);

   return true;
}
/*
 * SISetProcStateInvalid
 *		Flush pending messages from buffer, assert reset flag for each backend
 *
 * This is used only to recover from SI buffer overflow.
 */
static void
SISetProcStateInvalid(SISeg *segP,int state)
{
	int			i;

	segP->minMsgNum = 0;
	segP->maxMsgNum = 0;

	for (i = 0; i < segP->maxBackends; i++)
	{
		if (segP->procState[i].nextMsgNum >= 0) /* active backend? */
		{
			if ( state < segP->procState[i].resetState ) 
                            segP->procState[i].resetState = state;
			segP->procState[i].nextMsgNum = 0;
		}
	}
}

/*
 * SIGetDataEntry
 *		get next SI message for specified backend, if there is one
 *
 * Possible return values:
 *	0: no SI message available
 *	1: next SI message has been extracted into *data
 *		(there may be more messages available after this one!)
 * -1: SI reset message extracted
 */
int
SIGetDataEntry(SISeg *segP, int backendId,
			   SharedInvalidData *data)
{
	ProcState  *stateP = &segP->procState[backendId - 1];

	if (stateP->resetState == 1)
	{

		/*
		 * Force reset.  We can say we have dealt with any messages added
		 * since the reset, as well...
		 */
		stateP->resetState = 0;
		stateP->nextMsgNum = segP->maxMsgNum;
		return -1;
	}
        if (stateP->resetState == 2 ) 
        {
		/*
		 * Force reset.  We can say we have dealt with any messages added
		 * since the reset, as well...
		 */
		stateP->resetState = 0;
		stateP->nextMsgNum = segP->maxMsgNum;
		return -2;                
        }
	if (stateP->nextMsgNum >= segP->maxMsgNum)
		return 0;				/* nothing to read */

	/*
	 * Retrieve message and advance my counter.
	 */
	*data = segP->buffer[stateP->nextMsgNum % MAXNUMMESSAGES];
	stateP->nextMsgNum++;

	/*
	 * There may be other backends that haven't read the message, so we
	 * cannot delete it here. SIDelExpiredDataEntries() should be called
	 * to remove dead messages.
	 */
	return 1;					/* got a message */
}

/*
 * SIDelExpiredDataEntries
 *		Remove messages that have been consumed by all active backends
 */
void
SIDelExpiredDataEntries(SISeg *segP)
{
	int			min,
				i,
				h;
	int			numMsgs = segP->maxMsgNum - segP->minMsgNum;


	min = segP->maxMsgNum;
	if (min == segP->minMsgNum)
		return;					/* fast path if no messages exist */

	/* Recompute minMsgNum = minimum of all backends' nextMsgNum */

	for (i = 0; i < segP->maxBackends; i++)
	{
		h = segP->procState[i].nextMsgNum;
		if (h >= 0)
		{						/* backend active */
			if (h < min)
				min = h;
		}
	}
	segP->minMsgNum = min;

	/*
	 * When minMsgNum gets really large, decrement all message counters so
	 * as to forestall overflow of the counters.
	 */
	if (min >= MSGNUMWRAPAROUND)
	{
		segP->minMsgNum -= MSGNUMWRAPAROUND;
		segP->maxMsgNum -= MSGNUMWRAPAROUND;
		for (i = 0; i < segP->maxBackends; i++)
		{
			if (segP->procState[i].nextMsgNum >= 0)
				segP->procState[i].nextMsgNum -= MSGNUMWRAPAROUND;
		}
	}
/*  this only gets called by the DBWriter so we don't need to check again  */
	if (numMsgs == (MAXNUMMESSAGES * 70 / 100) )
	{
		SpinRelease(SInvalLock);
                    DiscardAllInvalids();
                SpinAcquire(SInvalLock);
        }
}
