/*-------------------------------------------------------------------------
 *
 * localbuf.c
 *	  local buffer manager. Fast buffer manager for temporary tables
 *	  or special cases when the operation is not visible to other backends.
 *
 *	  When a relation is being created, the descriptor will have rd_islocal
 *	  set to indicate that the local buffer manager should be used. During
 *	  the same transaction the relation is being created, any inserts or
 *	  selects from the newly created relation will use the local buffer
 *	  pool. rd_islocal is reset at the end of a transaction (commit/abort).
 *	  This is useful for queries like SELECT INTO TABLE and create index.
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994-5, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  $Header: /cvs/weaver/mtpgsql/src/backend/storage/buffer/localbuf.c,v 1.2 2007/04/22 23:16:49 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */
#include <sys/types.h>
#include <sys/file.h>
#include <math.h>
#include <signal.h>

#include "postgres.h"

#include "env/env.h"

#include "env/freespace.h"
#include "storage/localbuf.h"

#include "executor/execdebug.h"
#include "storage/smgr.h"
#include "utils/relcache.h"
#include "utils/memutils.h"

const int			NLocBuffer = 64;

typedef struct LoclBufferEnv {
	int 			LocalBufferFlushCount;
	BufferDesc *		LocalBufferDescriptors;
	long	   *		LocalRefCount;
	int			nextFreeLocalBuf;
} LocalBufferEnv;


static SectionId   local_buffer_section_id = SECTIONID("LBUF");


#ifdef TLS
TLS LocalBufferEnv* localbuffer_globals = NULL;
#else
#define localbuffer_globals GetEnv()->localbuffer_globals
#endif

static LocalBufferEnv* GetLocalBufferEnv(void);

/*#define LBDEBUG*/

BufferDesc*
LocalBufferSpecialAlloc(Relation reln, BlockNumber blockNum) {
/*  the first local buffer is for direct IO on blobs  */
        LocalBufferEnv* env = GetLocalBufferEnv();
	BufferDesc* bufHdr = &env->LocalBufferDescriptors[0];
	env->LocalRefCount[0]++;

	if (bufHdr->ioflags & BM_DIRTY)
	{
            elog(ERROR,"special local buffer is for read only operations");
	}

	bufHdr->tag.relId.relId = RelationGetRelid(reln);
	bufHdr->tag.blockNum = blockNum;
	bufHdr->ioflags &= ~BM_DIRTY;

	/*
	 * lazy memory allocation. (see MAKE_PTR for why we need to do
	 * MAKE_OFFSET.)
	 */
	if (bufHdr->data == NULL)
	{
		MemoryContext oldcxt = MemoryContextSwitchTo(MemoryContextGetTopContext());
		char	   *data = (char *) palloc(BLCKSZ);

		bufHdr->data = data;
		MemoryContextSwitchTo(oldcxt);
	}

	return bufHdr;
}

/*
 * LocalBufferAlloc -
 *	  allocate a local buffer. We do round robin allocation for now.
 */
BufferDesc *
LocalBufferAlloc(Relation reln, BlockNumber blockNum, bool *foundPtr)
{
	int			i;
	BufferDesc *bufHdr = (BufferDesc *) NULL;
        LocalBufferEnv* env = GetLocalBufferEnv();

	if (blockNum == P_NEW)
	{
		blockNum = RelationGetNumberOfBlocks(reln);
                reln->rd_nblocks = blockNum + 1;
	}

	/* a low tech search for now -- not optimized for scans */
	for (i = 1; i < NLocBuffer; i++)
	{
            if (env->LocalBufferDescriptors[i].tag.relId.relId == RelationGetRelid(reln) &&
                    env->LocalBufferDescriptors[i].tag.blockNum == blockNum)
            {

#ifdef LBDEBUG
                fprintf(stderr, "LB ALLOC (%u,%d) %d\n",
                                RelationGetRelid(reln), blockNum, -i - 1);
#endif
                env->LocalRefCount[i]++;
                *foundPtr = TRUE;
                return &env->LocalBufferDescriptors[i];
            }
	}

#ifdef LBDEBUG
	fprintf(stderr, "LB ALLOC (%u,%d) %d\n",
			RelationGetRelid(reln), blockNum, -env->nextFreeLocalBuf - 1);
#endif

	/* need to get a new buffer (round robin for now) */
	for (i = 0; i < NLocBuffer; i++)
	{
		int			b = (env->nextFreeLocalBuf + i) % NLocBuffer;

                if ( b == 0 ) continue;  /* 0 is the special buffer, need to skip it */
		if (env->LocalRefCount[b] == 0)
		{
			bufHdr = &env->LocalBufferDescriptors[b];
			env->LocalRefCount[b]++;
			env->nextFreeLocalBuf = (b + 1) % NLocBuffer;
			break;
		}
	}
	if (bufHdr == NULL)
		elog(ERROR, "no empty local buffer.");

	/*
	 * this buffer is not referenced but it might still be dirty (the last
	 * transaction to touch it doesn't need its contents but has not
	 * flushed it).  if that's the case, write it out before reusing it!
	 */
	if (bufHdr->ioflags & BM_DIRTY)
	{
		Relation	bufrel = RelationIdCacheGetRelation(bufHdr->tag.relId.relId,DEFAULTDBOID);

		Assert(bufrel != NULL);
/*   NOT NEEDED, only one thread sees a local buffer MKS  5/15/08
		pthread_mutex_lock(&bufHdr->io_in_progress_lock.guard);
*/
                bufHdr->ioflags &= ~BM_DIRTY;
                if ( bufrel->rd_rel->relkind != RELKIND_SPECIAL )  
                    PageInsertChecksum((Page)(bufHdr->data));
		/* flush this page */
		smgrwrite(bufrel->rd_smgr, bufHdr->tag.blockNum,
				  (char *)(bufHdr->data));
/*   NOT NEEDED, only one thread sees a local buffer MKS  5/15/08
		pthread_mutex_unlock(&bufHdr->io_in_progress_lock.guard);
 */
		env->LocalBufferFlushCount++;

		/*
		 * drop relcache refcount incremented by
		 * RelationIdCacheGetRelation
		 */
		RelationDecrementReferenceCount(bufrel);
	}

	/*
	 * it's all ours now.
	 */
	bufHdr->tag.relId.relId = RelationGetRelid(reln);
	bufHdr->tag.blockNum = blockNum;
	bufHdr->ioflags &= ~BM_DIRTY;

	/*
	 * lazy memory allocation. (see MAKE_PTR for why we need to do
	 * MAKE_OFFSET.)
	 */
	if (bufHdr->data == NULL)
	{
		MemoryContext oldcxt = MemoryContextSwitchTo(MemoryContextGetTopContext());
		char	   *data = (char *) palloc(BLCKSZ);

		bufHdr->data = data;
		MemoryContextSwitchTo(oldcxt);
	}

	*foundPtr = FALSE;
	return bufHdr;
}

/*
 * WriteLocalBuffer -
 *	  writes out a local buffer
 */
int
WriteLocalBuffer(Buffer buffer, bool release)
{
	int			bufid;
        LocalBufferEnv* env = GetLocalBufferEnv();

	Assert(BufferIsLocal(buffer));

#ifdef LBDEBUG
	fprintf(stderr, "LB WRITE %d\n", buffer);
#endif

	bufid = -(buffer + 1);
        if ( bufid == 0 ) {
            elog(ERROR,"tried to write a read only buffer");
        } else {
            env->LocalBufferDescriptors[bufid].ioflags |= BM_DIRTY;
        }

	if (release)
	{
		Assert(env->LocalRefCount[bufid] > 0);
		env->LocalRefCount[bufid]--;
	}

	return true;
}

/*
 * FlushLocalBuffer -
 *	  flushes a local buffer
 */
int
FlushLocalBuffer(Buffer buffer)
{
	int			bufid;
	Relation	bufrel;
	BufferDesc *bufHdr;
        LocalBufferEnv* env = GetLocalBufferEnv();

	Assert(BufferIsLocal(buffer));

#ifdef LBDEBUG
	fprintf(stderr, "LB FLUSH %d\n", buffer);
#endif
        
	bufid = -(buffer + 1);
	bufHdr = &env->LocalBufferDescriptors[bufid];
/*  NOT NEEDED all local buffers should be thread specific  
	pthread_mutex_lock(&bufHdr->io_in_progress_lock.guard);
*/
                
        if ( bufHdr->ioflags & BM_READONLY ) {
            elog(ERROR,"trying to flush a read only buffer");
        }
        
	bufHdr->ioflags &= ~BM_DIRTY;
	bufrel = RelationIdCacheGetRelation(bufHdr->tag.relId.relId,DEFAULTDBOID);

	Assert(bufrel != NULL);
    if ( bufrel->rd_rel->relkind != RELKIND_SPECIAL )   
            PageInsertChecksum((Page)(bufHdr->data));
    smgrflush(bufrel->rd_smgr, bufHdr->tag.blockNum,
			  (char *)(bufHdr->data));
/*
    pthread_mutex_unlock(&bufHdr->io_in_progress_lock.guard);
*/
	env->LocalBufferFlushCount++;

	/* drop relcache refcount incremented by RelationIdCacheGetRelation */
	RelationDecrementReferenceCount(bufrel);

        env->LocalRefCount[bufid]--;

	return true;
}

void
IncrLocalBufferRefCount(Buffer buffer) {
    GetLocalBufferEnv()->LocalRefCount[buffer]++;
}

BufferDesc *
GetLocalBufferDescriptor(Buffer buffer) {
    return &GetLocalBufferEnv()->LocalBufferDescriptors[buffer];
}

void 
DecrLocalRefCount(Buffer buffer) {
    GetLocalBufferEnv()->LocalRefCount[buffer]--;
}

void 
ReleaseLocalBuffer(Buffer buffer) {
    LocalBufferEnv* env = GetLocalBufferEnv();
    env->LocalBufferDescriptors[buffer].ioflags &= ~(BM_DIRTY);
    env->LocalRefCount[buffer] = 0;
    env->LocalBufferDescriptors[buffer].tag.relId.relId = InvalidOid;
}

int
GetLocalRefCount(Buffer buffer) {
    return GetLocalBufferEnv()->LocalRefCount[buffer];
}   

LocalBufferEnv*
GetLocalBufferEnv(void)
{
	int			i;

        LocalBufferEnv* env = localbuffer_globals;
        if ( env == NULL ) {
            env = AllocateEnvSpace(local_buffer_section_id,sizeof(LocalBufferEnv));

            MemoryContext oldcxt = MemoryContextSwitchTo(MemoryContextGetTopContext());
            env->LocalBufferDescriptors =
                    (BufferDesc *) palloc(sizeof(BufferDesc) * NLocBuffer);
            MemSet(env->LocalBufferDescriptors, 0, sizeof(BufferDesc) * NLocBuffer);
            env->nextFreeLocalBuf = 0;

            for (i = 0; i < NLocBuffer; i++)
            {
                    BufferDesc *buf = &env->LocalBufferDescriptors[i];

                    /*
                     * negative to indicate local buffer. This is tricky: shared
                     * buffers start with 0. We have to start with -2. (Note that the
                     * routine BufferDescriptorGetBuffer adds 1 to buf_id so our first
                     * buffer id is -1.)
                     */
                    buf->buf_id = -i - 2;
            }

            env->LocalRefCount = (long *) palloc(sizeof(long) * NLocBuffer);
            MemSet(env->LocalRefCount, 0, sizeof(long) * NLocBuffer);
            MemoryContextSwitchTo(oldcxt);

            localbuffer_globals = env;
        }
        return env;
}

/*
 * LocalBufferSync -
 *	  flush all dirty buffers in the local buffer cache. Since the buffer
 *	  cache is only used for keeping relations visible during a transaction,
 *	  we will not need these buffers again.
 */
void
LocalBufferSync(void)
{
	int			i;
        LocalBufferEnv* env = GetLocalBufferEnv();

	for (i = 1; i < NLocBuffer; i++)
	{
		BufferDesc *buf = &env->LocalBufferDescriptors[i];
		Relation	bufrel;

		if (buf->ioflags & BM_DIRTY)
		{
#ifdef LBDEBUG
			fprintf(stderr, "LB SYNC %d\n", -i - 1);
#endif
			bufrel = RelationIdCacheGetRelation(buf->tag.relId.relId,DEFAULTDBOID);

			Assert(bufrel != NULL);
/*  NOT NEEDED, this thread is the only one to see the buffer  MKS  5/15/08
            pthread_mutex_lock(&buf->io_in_progress_lock.guard);
*/
            if ( bufrel->rd_rel->relkind != RELKIND_SPECIAL )  
                    PageInsertChecksum((Page)(buf->data));
			smgrwrite(bufrel->rd_smgr, buf->tag.blockNum,
					  (char *)(buf->data));
			env->LocalBufferFlushCount++;

			/* drop relcache refcount from RelationIdCacheGetRelation */
			RelationDecrementReferenceCount(bufrel);

			buf->tag.relId.relId = InvalidOid;
			buf->ioflags &= ~BM_DIRTY;
/*  NOT NEEDED, this thread is the only one to see the buffer MKS  5/15/08
            pthread_mutex_unlock(&buf->io_in_progress_lock.guard);
*/
		}
	}

	MemSet(env->LocalRefCount, 0, sizeof(long) * NLocBuffer);
	env->nextFreeLocalBuf = 1;
}

void
ResetLocalBufferPool(void)
{
	int			i;
        LocalBufferEnv* env = GetLocalBufferEnv();

	for (i = 0; i < NLocBuffer; i++)
	{
		BufferDesc *buf = &env->LocalBufferDescriptors[i];

		buf->tag.relId.relId = InvalidOid;
		buf->ioflags &= ~BM_DIRTY;
		buf->buf_id = -i - 2;
	}

	MemSet(env->LocalRefCount, 0, sizeof(long) * NLocBuffer);
	env->nextFreeLocalBuf = 1;
}
