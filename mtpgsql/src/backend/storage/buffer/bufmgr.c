/*-------------------------------------------------------------------------
 *
 * bufmgr.c
 *	  buffer manager interface routines
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  $Header: /cvs/weaver/mtpgsql/src/backend/storage/buffer/bufmgr.c,v 1.13 2007/05/23 15:39:23 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */
/*
 *
 * BufferAlloc() -- lookup a buffer in the buffer table.  If
 *		it isn't there add it, but do not read data into memory.
 *		This is used when we are about to reinitialize the
 *		buffer so don't care what the current disk contents are.
 *		BufferAlloc() also pins the new buffer in memory.
 *
 * ReadBuffer() -- like BufferAlloc() but reads the data
 *		on a buffer cache miss.
 *
 * ReleaseBuffer() -- unpin the buffer
 *
 * WriteNoReleaseBuffer() -- mark the buffer contents as "dirty"
 *		but don't unpin.  The disk IO is delayed until buffer
 *		replacement.
 *
 * WriteBuffer() -- WriteNoReleaseBuffer() + ReleaseBuffer()
 *
 * FlushBuffer() -- Write buffer immediately.  Can unpin, or not,
 *		depending on parameter.
 *
 * BufferSync() -- flush all dirty buffers in the buffer pool.
 *
 * InitBufferPool() -- Init the buffer module.
 *
 * See other files:
 *		freelist.c -- chooses victim for buffer replacement
 *		buf_table.c -- manages the buffer lookup table
 */

#include <sys/sdt.h>
#include <sys/types.h>
#include <sys/file.h>
#include <math.h>
#include <signal.h>
#include <sys/sdt.h>

#include "postgres.h"

#include "env/env.h"
#include "env/dbwriter.h"
#include "env/poolsweep.h"
#include "env/connectionutil.h"

#include "executor/execdebug.h"
#include "miscadmin.h"
#ifdef SPIN_IS_MUTEX
#include "storage/m_lock.h"
#else
#include "storage/s_lock.h"
#endif
#include "storage/smgr.h"
#include "utils/relcache.h"
#include "storage/bufmgr.h"
#include "storage/bufpage.h"
#include "storage/localbuf.h"
#include "utils/memutils.h"

pthread_t lockowner;

extern SPINLOCK BufMgrLock;
extern SLock *SLockArray;

static BufferDesc *BufferAlloc(Relation reln, BlockNumber blockNum, bool *foundPtr);
static bool SyncShadowPage(BufferDesc* bufHdr);

static void UnpinBuffer(BufferCxt cxt, BufferDesc *buf);
static int PinBuffer(BufferCxt cxt, BufferDesc *buf);
static void InvalidateBuffer(BufferCxt cxt, BufferDesc* buf);

static void BufferHit(Buffer buf, Oid relid, Oid dbid, char* name);
static void BufferMiss(Oid relid, Oid dbid, char* name);
static void BufferReplaceMiss(Oid relid, Oid dbid, char* name);
static void BufferPinInvalid(Buffer buf, Oid relid, Oid dbid, char* name);
static void BufferPinMiss(Buffer buf, Oid relid, Oid dbid, char* name);

static int DirectWriteBuffer(Relation rel, Buffer buffer, bool release);
static bits8 UnlockIndividualBuffer(bits8 buflock, BufferDesc* buffer);

static bool InboundBufferIO(BufferDesc* header);
static bool CancelInboundBufferIO(BufferDesc* header);
static bool ClearBufferIO(BufferDesc* header);
static void ShowBufferIO(int id, int io);
static bool DirtyBufferIO(BufferDesc* header);
static bool WaitBufferIO(bool write_mode, BufferDesc *buf);

static SectionId   buffer_section_id = SECTIONID("BMGR");

#ifdef TLS
TLS BufferEnv* buffers_global = NULL;
#else
#define buffers_global GetEnv()->buffers_global
#endif

typedef struct bufenv {
    /*  buffer_init.c   */
    long                       guard;
    long	   *		PrivateRefCount;	/* also used in freelist.c */
    bits8	   *		BufferLocks;		/* flag bits showing locks I have set */
    BufferTag  *		BufferTagLastDirtied;		/* tag buffer had when last
     * dirtied by me */
    BufferBlindId *		BufferBlindLastDirtied;	/* and its BlindId too */
    int                         total_pins;
    bool                        DidWrite;
} BufferEnv;

void		PrintBufferDescs(void);

/*
 * ReadBuffer -- returns a buffer containing the requested
 *		block of the requested relation.  If the blknum
 *		requested is P_NEW, extend the relation file and
 *		allocate a new block.
 *
 * Returns: the buffer number for the buffer containing
 *		the block read or NULL on an error.
 *
 * Assume when this function is called, that reln has been
 *		opened already.
 */

#undef ReadBuffer				/* conflicts with macro when BUFMGR_DEBUG
 * defined */
 
/*
 * ReadBuffer
 *
 */
Buffer
ReadBuffer(Relation reln, BlockNumber blockNum) {
    BufferDesc *bufHdr;
    int			extend;			/* extending the file by one block */
    int			status;
    bool		found;
    bool		isLocalBuf;
    BlockNumber         check = blockNum;
    BufferEnv* bufenv = RelationGetBufferCxt(reln);
    IOStatus            iostatus;
    
    extend = (blockNum == P_NEW);
    isLocalBuf = reln->rd_myxactonly;
    
    if ( extend ) {
        union {
            double align;
            char   data[BLCKSZ];
        } buffer;
        
        Assert ( reln->rd_rel->relkind != RELKIND_RELATION && reln->rd_rel->relkind != RELKIND_UNCATALOGED );
        
        MemSet(buffer.data, 0x00, BLCKSZ);
        PageInit((Page)buffer.data, BLCKSZ, 0);
        PageInsertChecksum((Page)buffer.data);
        
        reln->rd_nblocks = smgrextend(reln->rd_smgr, buffer.data, 1);
        if ( reln->rd_nblocks <= 0 ) return InvalidBuffer;
        blockNum = reln->rd_nblocks - 1;
    }
    
    if (isLocalBuf) {
        bufHdr = LocalBufferAlloc(reln, blockNum, &found);
    } else {
        /*
         * lookup the buffer.  IO_IN_PROGRESS is set if the requested
         * block is not currently in memory.
         */
        bufHdr = BufferAlloc(reln, blockNum, &found);
    }
    
    if (!bufHdr)
        return InvalidBuffer;
    
    /* if it's already in the buffer pool, we're done */
    if (found) {
        return BufferDescriptorGetBuffer(bufHdr);
    }
    
    if ( reln->readtrigger != NULL ) {
        BufferTrigger*  trigger = reln->readtrigger;
        trigger->call(trigger->args);
    }
    
    if ( !isLocalBuf ) {
        iostatus = ReadBufferIO(bufHdr);
        if ( !iostatus )  {
            elog(DEBUG, "read buffer failed in io start bufid:%d dbid:%d relid:%d blk:%d\n",
                bufHdr->buf_id,
                bufHdr->tag.relId.dbId,
                bufHdr->tag.relId.relId,
                bufHdr->tag.blockNum);
           InvalidateBuffer(bufenv,bufHdr);
           UnpinBuffer(bufenv, bufHdr);
           return InvalidBuffer;
        }
    }
    
    status = smgrread(reln->rd_smgr, blockNum, (char *) MAKE_PTR(bufHdr->data));
    
    if ( !isLocalBuf && status == SM_SUCCESS ) {
        if ( reln->rd_rel->relkind == RELKIND_INDEX ) {
            if ( !PageIsNew((Page)MAKE_PTR(bufHdr->data)) && !PageConfirmChecksum((Page)MAKE_PTR(bufHdr->data)) ) {
                char* index = GetProperty("index_corruption");
                if ( index == NULL || (strcasecmp(index,"IGNORE") != 0)  ) {
                    AddReindexRequest(RelationGetRelationName(reln),GetDatabaseName(),bufHdr->tag.relId.relId,bufHdr->tag.relId.dbId);
                    status = SM_FAIL;
                }
                elog(NOTICE, "Index Page is corrupted name:%s page:%d check:%d\n", reln->rd_rel->relname, blockNum,check);
                elog(NOTICE, "checksum=%lld\n", ((PageHeader)MAKE_PTR(bufHdr->data))->checksum);
            }
        } else if ( reln->rd_rel->relkind == RELKIND_RELATION ) {
            if ( !PageIsNew((Page)MAKE_PTR(bufHdr->data)) && !PageConfirmChecksum((Page)MAKE_PTR(bufHdr->data)) ) {
                char* heap = GetProperty("heap_corruption");

                if ( heap != NULL && (strcasecmp(heap,"IGNORE") == 0)  ) {
                    PageInsertChecksum((Page)MAKE_PTR(bufHdr->data));
                    SetBufferCommitInfoNeedsSave(BufferDescriptorGetBuffer(bufHdr));
                } else {
                    PageInit((Page)MAKE_PTR(bufHdr->data),BLCKSZ,0);
                    status = SM_FAIL;
                }
                elog(NOTICE, "Heap Page is corrupted name:%s page:%ld", reln->rd_rel->relname, blockNum);
           }
        }
    }
    /* lock buffer manager again to update IO IN PROGRESS */
    
    if (status == SM_FAIL) {
        if ( !isLocalBuf ) {
            elog(DEBUG, "read buffer failed bufid:%d dbid:%d relid:%d blk:%d\n",
                bufHdr->buf_id,
                bufHdr->tag.relId.dbId,
                bufHdr->tag.relId.relId,
                bufHdr->tag.blockNum);
            ErrorBufferIO(iostatus,bufHdr);
            InvalidateBuffer(bufenv,bufHdr);
            UnpinBuffer(bufenv, bufHdr);
        }
        return InvalidBuffer;
    }
    
    /* copy read in data to shadow page */
    SyncShadowPage(bufHdr);
    if ( !isLocalBuf ) {
    /* If anyone was waiting for IO to complete, wake them up now */
        TerminateBufferIO(iostatus,bufHdr);
    }

    return BufferDescriptorGetBuffer(bufHdr);
}

/*
 * BufferAlloc -- Get a buffer from the buffer pool but dont
 *		read it.
 *
 * Returns: descriptor for buffer
 *
 * When this routine returns, the BufMgrLock is guaranteed NOT be held.
 */
static BufferDesc *
BufferAlloc(Relation reln, BlockNumber blockNum, bool *foundPtr) {
    BufferDesc 	*buf = NULL;		/* identity of requested block */
    BufferTag	newTag;			/* identity of requested block */
    BufferEnv*  bufenv = RelationGetBufferCxt(reln);
    
    /* create a new tag so we can lookup the buffer */
    /* assume that the relation is already open */
    AssertArg(blockNum != P_NEW);
    
    INIT_BUFFERTAG(&newTag, reln, blockNum);
    
    while ( buf == NULL ) {
        BufferDesc*  freebuffer = NULL;
        /* see if the block is in the buffer pool already */
        buf = BufTableLookup(reln->rd_rel->relkind, &newTag);
        /*   if buf != NULL we have a buffer but we have
         no idea what state it's in until we pin it    */
        if ( buf != NULL ) {
            if ( !PinBuffer(bufenv, buf) ) {
                BufferPinInvalid(buf->buf_id, buf->tag.relId.relId, buf->tag.relId.dbId, buf->blind.relname);
                buf = NULL;
            } else {
                /*  wait for the buffer IO to complete if not done already  */
                /*  make sure that we have the right one from the pin       */
                if (buf->tag.blockNum == blockNum &&
                    buf->tag.relId.relId == reln->rd_lockInfo.lockRelId.relId &&
                    buf->tag.relId.dbId == reln->rd_lockInfo.lockRelId.dbId ) {
                        if ( WaitBufferIO(false, buf) ) {
                            *foundPtr = true;
                            BufferHit(buf->buf_id, buf->tag.relId.relId, buf->tag.relId.dbId, buf->blind.relname);
                            return buf;
                        } else {
                            UnpinBuffer(bufenv, buf);
                            buf = NULL;
                        }
                } else {
                    BufferPinMiss(buf->buf_id, buf->tag.relId.relId, buf->tag.relId.dbId, buf->blind.relname);
                    UnpinBuffer(bufenv, buf);
                    buf = NULL;
                }
            }
        } else {
            freebuffer = GetFreeBuffer(reln);

            Assert ( freebuffer != NULL );
            InboundBufferIO(freebuffer);
            
            if ( BufTableReplace(freebuffer, reln, blockNum) ) {
                strcpy(freebuffer->blind.dbname, GetDatabaseName());
                strcpy(freebuffer->blind.relname, RelationGetPhysicalRelationName(reln));
                bufenv->PrivateRefCount[freebuffer->buf_id] = 1;
                bufenv->total_pins++;
                *foundPtr = FALSE;
                BufferMiss(newTag.relId.relId, newTag.relId.dbId, RelationGetRelationName(reln));
                buf = freebuffer;
                return buf;
            } else {
                freebuffer->blind.dbname[0] = '\0';
                freebuffer->blind.relname[0] = '\0';
                /*  a valid buffer was found during replace, clean up and cycle around again */
                BufferReplaceMiss(newTag.relId.relId, newTag.relId.dbId, RelationGetRelationName(reln));
                CancelInboundBufferIO(freebuffer);
                PutFreeBuffer(freebuffer);
                buf = NULL;
            }
        }
    }
    return buf;
}

static void BufferMiss(Oid relid, Oid dbid, char* name) {
    DTRACE_PROBE3(mtpg, buffer__miss, relid, dbid, name);
}

static void BufferHit(Buffer bufid, Oid relid, Oid dbid, char* name) {
    DTRACE_PROBE4(mtpg, buffer__hit, bufid, relid, dbid, name);
}

static void BufferReplaceMiss(Oid relid, Oid dbid, char* name) {
    DTRACE_PROBE3(mtpg, buffer__replacemiss, relid, dbid, name);
}


static void BufferPinMiss(Buffer buf, Oid relid, Oid dbid, char* name) {
    DTRACE_PROBE3(mtpg, buffer__pinmiss, relid, dbid, name);
}


static void BufferPinInvalid(Buffer buf, Oid relid, Oid dbid, char* name) {
    DTRACE_PROBE3(mtpg, buffer__pininvalid, relid, dbid, name);
}
/*
 * WriteBuffer
 *
 *		Marks buffer contents as dirty (actual write happens later).
 *
 * Assume that buffer is pinned.  Assume that reln is
 *		valid.
 *
 * Side Effects:
 *		Pin count is decremented.
 */

#undef WriteBuffer

int
WriteBuffer(Relation rel, Buffer buffer) {
    BufferDesc *bufHdr;
    BufferEnv* bufenv = RelationGetBufferCxt(rel);

    if (BufferIsLocal(buffer))
        return WriteLocalBuffer(buffer, TRUE);
    
    if (BAD_BUFFER_ID(buffer))
        return FALSE;
    
    bufHdr = &BufferDescriptors[buffer - 1];
    
    bufenv->DidWrite = true;
    GetTransactionInfo()->SharedBufferChanged = true;
    
    /*  What we are doing here is registering the buffer for a write with the DBWriter thread  	*/
    /*  when the transaction commits or bfrmgr runs out of buffers, the DBWriter writes some   	*/
    /*  buffers out and commits transactions that need to be commited and unpins the buffer there  	*/
    
    /*  this passes control of the pin to the DBWriter so reset PrivateRef to zero   */
    /* doing this processing in RegisterBufferWrite code so we only have to grab the
        io mutex once
     */

    Assert ( DirtyBufferIO(bufHdr) );
    
    /*  we are manually unpinning the buffer by decrementing PrivateRefCount by one.
     *	if PrivateRefCount hits zero, past control of unpinning the shared ref count to
     *	DBWriter thread to Decrement the shared buffer count by one when the
     *	thread is finished writing.
     */
    /*
        unlock the io guard before registering the write
        b/c the Register command locks the buffer on its own
     */

    RegisterBufferWrite(bufHdr, bufenv->PrivateRefCount[bufHdr->buf_id] == 1);
    bufenv->PrivateRefCount[bufHdr->buf_id]--;
    bufenv->total_pins--;
    
    return TRUE;
}

int
SyncRelation(Relation rel) {
    return smgrsync(rel->rd_smgr);
}

int
FlushBuffer(Relation rel, Buffer buffer, bool release) {
    BufferDesc *bufHdr;
   
    if (BufferIsLocal(buffer))
        return FlushLocalBuffer(buffer, release) ? STATUS_OK : STATUS_ERROR;
    else 
        bufHdr = &BufferDescriptors[buffer - 1];
    
    if (BAD_BUFFER_ID(buffer))
        return STATUS_ERROR;

    return DirectWriteBuffer(rel, buffer, release);
}

int
DirectWriteBuffer(Relation rel, Buffer buffer, bool release) {
    BufferDesc *bufHdr;
    Oid			bufdb;
    Oid			relId;
    BufferEnv* bufenv = RelationGetBufferCxt(rel);
    int			status = STATUS_OK;
    IOStatus            iostatus;
        
        bufenv->DidWrite = true;
        
        bufHdr = &BufferDescriptors[buffer - 1];
        /*  rely on the fact that the buffer is already pinned so
we don't have to lock  */
        
        bufdb = bufHdr->tag.relId.dbId;
        relId = bufHdr->tag.relId.relId;
        
        Assert ( relId == RelationGetRelid(rel) );
        
        bufenv->DidWrite = true;
        GetTransactionInfo()->SharedBufferChanged = true;
        
        /*
         * Grab a read lock on the buffer to ensure that no
         * other backend changes its contents while we write it;
         * see comments in BufferSync().
         */
        iostatus = WriteBufferIO(bufHdr,true);
        if ( iostatus ) {
            if ( rel->rd_rel->relkind != RELKIND_SPECIAL )  {
                PageInsertChecksum((Page)MAKE_PTR(bufHdr->data));
            }

            status = smgrflush(rel->rd_smgr, bufHdr->tag.blockNum,
            (char *) MAKE_PTR(bufHdr->data));

            if (status == SM_FAIL) {
                ErrorBufferIO(iostatus,bufHdr);
                elog(NOTICE, "FlushBuffer: cannot flush block %u of the relation %s",
                bufHdr->tag.blockNum, bufHdr->blind.relname);
            } else {
                /* copy new page to shadow */
                SyncShadowPage(bufHdr);
                TerminateBufferIO(iostatus,bufHdr);
            }
        }
        
        if (release) {
            UnpinBuffer(bufenv,bufHdr);
        }
        
        return ( status == SM_FAIL ) ? STATUS_ERROR : STATUS_OK;
}

/*
 * WriteNoReleaseBuffer -- like WriteBuffer, but do not unpin the buffer
 *						   when the operation is complete.
 */
int
WriteNoReleaseBuffer(Relation rel, Buffer buffer) {
    BufferDesc *bufHdr;

    if (BufferIsLocal(buffer))
        return WriteLocalBuffer(buffer, FALSE);
    
    if (BAD_BUFFER_ID(buffer))
        return STATUS_ERROR;
    
    bufHdr = &BufferDescriptors[buffer - 1];
    
    RelationGetBufferCxt(rel)->DidWrite = true;
    GetTransactionInfo()->SharedBufferChanged = true;
    
    /*   See WriterBuffer for why we did this   */
    /*  mark this dirty in the RegisterBufferWrite code so we only have to grab the
        mutex once  */
    
    DirtyBufferIO(bufHdr);
    
    /*
        unlock the io guard before registering the write
        b/c the Register command locks the buffer on its own
     */
    RegisterBufferWrite(bufHdr, false);
    
    return STATUS_OK;
}

#undef ReleaseAndReadBuffer
/*
 * ReleaseAndReadBuffer -- combine ReleaseBuffer() and ReadBuffer()
 *		so that only one semop needs to be called.
 *
 */
Buffer
ReleaseAndReadBuffer(Buffer buffer,
Relation relation,
BlockNumber blockNum) {
    BufferDesc *bufHdr;
    Buffer		retbuf;
    
    if (BufferIsValid(buffer)) {
        if (BufferIsLocal(buffer)) {
            bufHdr = GetLocalBufferDescriptor((-buffer) - 1);
            if (bufHdr->tag.relId.relId == RelationGetRelid(relation) &&
            bufHdr->tag.blockNum == blockNum) {
                return buffer;
            } else {
                DecrLocalRefCount(-buffer - 1);
            }
        } else {            
            bufHdr = &BufferDescriptors[buffer - 1];
            if ( CheckBufferId(bufHdr, blockNum, relation->rd_id, GetDatabaseId()) ) {
                if ( WaitBufferIO(false, bufHdr) ) {
                    return buffer;
                }
            }
            UnpinBuffer(RelationGetBufferCxt(relation),bufHdr);
        }
    }
    return ReadBuffer(relation, blockNum);
}

/* ----------------------------------------------
 *		ResetBufferPool
 *
 *		This routine is supposed to be called when a transaction aborts.
 *		it will release all the buffer pins held by the transaction.
 *		Currently, we also call it during commit if BufferPoolCheckLeak
 *		detected a problem --- in that case, isCommit is TRUE, and we
 *		only clean up buffer pin counts.
 *
 * During abort, we also forget any pending fsync requests.  Dirtied buffers
 * will still get written, eventually, but there will be no fsync for them.
 *
 * ----------------------------------------------
 */
void
ResetBufferPool(bool isCommit) {
    int			i;
    BufferEnv* env = GetBufferCxt();
    
    /*	printf("reseting buffer pool\n");   */
    for (i = 0; i < NBuffers; i++) {
        if (env->PrivateRefCount[i] != 0) {
            BufferDesc *buf;
            buf = &BufferDescriptors[i];
            
            ManualUnpin(buf, true);
            lockowner = (pthread_t)NULL;
        }
        env->PrivateRefCount[i] = 0;
    }
    ResetLocalBufferPool();
    
    if (!isCommit) {
        smgrabort();
    }
}

/* -----------------------------------------------
 *		BufferPoolCheckLeak
 *
 *		check if there is buffer leak
 *
 * -----------------------------------------------
 */
int
BufferPoolCheckLeak() {
    int			i;
    int			result = 0;
    BufferEnv* env = GetBufferCxt();
    
    for (i = 0; i < NBuffers; i++) {
        if (env->PrivateRefCount[i] != 0) {
            BufferDesc *buf = &(BufferDescriptors[i]);
            
            elog(NOTICE,
            "Buffer Leak: [%03d] (freeNext=%ld, \
            relname=%s, blockNum=%d, flags=0x%x, refCount=%d %ld)",
            i, buf->freeNext,
            buf->blind.relname, buf->tag.blockNum, buf->ioflags,
            buf->refCount, env->PrivateRefCount[i]);
            result++;
        }
    }
    return result;
}

int
BufferPoolCountHolds() {
    int			i;
    int			result = 0;
    BufferEnv* env = GetBufferCxt();
    
    for (i = 0; i < NBuffers; i++) {
        if (env->PrivateRefCount[i] != 0) {
               result++;
        }
    }
    return result;
}

/* ------------------------------------------------
 *		FlushBufferPool
 *
 *		flush all dirty blocks in buffer pool to disk
 *
 * ------------------------------------------------
 */
#ifdef NOTUSED
void
FlushBufferPool(void) {
    /*
        FlushAllDirtyBuffers();
     */
}
#endif
/*
 * BufferGetBlockNumber
 *		Returns the block number associated with a buffer.
 *
 * Note:
 *		Assumes that the buffer is valid.
 */
BlockNumber
BufferGetBlockNumber(Buffer buffer) {
    Assert(BufferIsValid(buffer));
    
    /* XXX should be a critical section */
    if (BufferIsLocal(buffer))
        return GetLocalBufferDescriptor((-buffer) - 1)->tag.blockNum;
    else
        return BufferDescriptors[buffer - 1].tag.blockNum;
}

/* ---------------------------------------------------------------------
 *		ReleaseRelationBuffers
 *
 *		This function removes all the buffered pages for a relation
 *		from the buffer pool.  Dirty pages are simply dropped, without
 *		bothering to write them out first.  This is used when the
 *		relation is about to be deleted.  We assume that the caller
 *		holds an exclusive lock on the relation, which should assure
 *		that no new buffers will be acquired for the rel meanwhile.
 *
 *		XXX currently it sequentially searches the buffer pool, should be
 *		changed to more clever ways of searching.
 * --------------------------------------------------------------------
 */

void
InvalidateRelationBuffers(Relation rel) {
    Oid			relid = RelationGetRelid(rel);
    int			i;
    BufferDesc *buf;
    Oid			dbid = GetDatabaseId();
    
    BufferCxt   bufcxt = RelationGetBufferCxt(rel);
    
    if (rel->rd_myxactonly) {
        for (i = 0; i < NLocBuffer; i++) {
            buf = GetLocalBufferDescriptor(i);
            if (buf->tag.relId.relId == relid) {
                ReleaseLocalBuffer(i);
            }
        }
        return;
    }
    
    FlushAllDirtyBuffers();
    
    for (i = 1; i <= NBuffers; i++) {
        buf = &BufferDescriptors[i - 1];
        
        if ( PinBuffer(bufcxt, buf) ) {
            /* Now we can do what we came for */
            if ( CheckBufferId(buf, InvalidBlockNumber, relid, dbid) ) {
                InvalidateBuffer(bufcxt,buf);
            }
            UnpinBuffer(bufcxt, buf);
        }
    }
}


/* ---------------------------------------------------------------------
 *		DropBuffers
 *
 *		This function removes all the buffers in the buffer cache for a
 *		particular database.  Dirty pages are simply dropped, without
 *		bothering to write them out first.  This is used when we destroy a
 *		database, to avoid trying to flush data to disk when the directory
 *		tree no longer exists.	Implementation is pretty similar to
 *		ReleaseRelationBuffers() which is for destroying just one relation.
 * --------------------------------------------------------------------
 */
void
DropBuffers(Oid dbid) {
    int			i;
    BufferDesc *buf;
    BufferCxt    bufcxt = GetBufferCxt();
    
    FlushAllDirtyBuffers();
    for (i = 1; i <= NBuffers; i++) {
        buf = &BufferDescriptors[i - 1];
        
        if ( PinBuffer(bufcxt, buf) ) {
            /* Now we can do what we came for */
            if ( CheckBufferId(buf, InvalidBlockNumber, -1, dbid) ) {
                InvalidateBuffer(bufcxt,buf);
            }
            UnpinBuffer(bufcxt, buf);
        }
    }
}

/* -----------------------------------------------------------------
 *		PrintBufferDescs
 *
 *		this function prints all the buffer descriptors, for debugging
 *		use only.
 * -----------------------------------------------------------------
 */
void
PrintBufferDescs() {
    int			i;
    BufferDesc *buf = BufferDescriptors;
    BufferEnv* env = GetBufferCxt();
    
    if (IsMultiuser()) {
        lockowner = pthread_self();
        for (i = 0; i < NBuffers; ++i, ++buf) {
            elog(DEBUG, "[%02d] (freeNext=%ld, relname=%s, \
            blockNum=%d, flags=0x%x, refCount=%d %ld)",
            i, buf->freeNext,
            buf->blind.relname, buf->tag.blockNum, buf->ioflags,
            buf->refCount, env->PrivateRefCount[i]);
        }
    } else {
        /* interactive backend */
        for (i = 0; i < NBuffers; ++i, ++buf) {
            printf("[%-2d] (%s, %d) flags=0x%x, refcnt=%d %ld)\n",
            i, buf->blind.relname, buf->tag.blockNum,
            buf->ioflags, buf->refCount, env->PrivateRefCount[i]);
        }
    }
}

void
PrintPinnedBufs() {
    int			i;
    BufferDesc *buf = BufferDescriptors;
    BufferEnv* env = GetBufferCxt();
    
    lockowner = pthread_self();
    for (i = 0; i < NBuffers; ++i, ++buf) {
        if (env->PrivateRefCount[i] > 0)
            elog(NOTICE, "[%02d] (freeNext=%ld, relname=%s, \
            blockNum=%d, flags=0x%x, refCount=%d %ld)\n",
            i, buf->freeNext, buf->blind.relname,
            buf->tag.blockNum, buf->ioflags,
            buf->refCount, env->PrivateRefCount[i]);
    }
}


#undef ReleaseBuffer

/*
 * ReleaseBuffer -- remove the pin on a buffer without
 *		marking it dirty.
 *
 */
int
ReleaseBuffer(Relation rel, Buffer buffer) {
    BufferDesc *bufHdr;
    BufferEnv* env = RelationGetBufferCxt(rel);
    
    if (BufferIsLocal(buffer)) {
        Assert(GetLocalRefCount(-buffer - 1) > 0);
        DecrLocalRefCount(-buffer - 1);
        return STATUS_OK;
    }
    
    if (BAD_BUFFER_ID(buffer))
        return STATUS_ERROR;
    
    bufHdr = &BufferDescriptors[buffer - 1];
    
    UnpinBuffer(env,bufHdr);

    return STATUS_OK;
}

int
BiasBuffer(Relation rel, Buffer buffer) {
    BufferEnv* env = RelationGetBufferCxt(rel);
    BufferDesc *bufHdr;
    
    if (BufferIsLocal(buffer)) {
        return STATUS_ERROR;
    }
    
    if (BAD_BUFFER_ID(buffer))
        return STATUS_ERROR;
    
    bufHdr = &BufferDescriptors[buffer - 1];
    
    if ( env->PrivateRefCount[buffer - 1] >= 0 ) {
        BiasPinned(bufHdr);
        return STATUS_OK;
    }
    
    return STATUS_ERROR;
}

/*
 * SetBufferCommitInfoNeedsSave
 *
 *	Mark a buffer dirty when we have updated tuple commit-status bits in it.
 *
 * This is similar to WriteNoReleaseBuffer, except that we do not set
 * SharedBufferChanged or BufferDirtiedByMe, because we have not made a
 * critical change that has to be flushed to disk before xact commit --- the
 * status-bit update could be redone by someone else just as easily.  The
 * buffer will be marked dirty, but it will not be written to disk until
 * there is another reason to write it.
 *
 * This routine might get called many times on the same page, if we are making
 * the first scan after commit of an xact that added/deleted many tuples.
 * So, be as quick as we can if the buffer is already dirty.
 */
void
SetBufferCommitInfoNeedsSave(Buffer buffer) {
    BufferDesc *bufHdr;
    
    if (BufferIsLocal(buffer))
        return;
    
    if (BAD_BUFFER_ID(buffer))
        return;
    
    bufHdr = &BufferDescriptors[buffer - 1];
    
    DirtyBufferIO(bufHdr);
}


void
UnlockBuffers(void) {
    BufferDesc *buf;
    bits8	   *buflock;
    int			i;
    BufferEnv* bufenv = GetBufferCxt();
    
    for (i = 0; i < NBuffers; i++) {
        if (bufenv->BufferLocks[i] == 0)
            continue;
        
        Assert(BufferIsValid(i + 1));
        buf = &(BufferDescriptors[i]);
        
        pthread_mutex_lock(&(buf->cntx_lock.guard));
        
        bufenv->BufferLocks[i] = UnlockIndividualBuffer(bufenv->BufferLocks[i], buf);
        
        pthread_mutex_unlock(&(buf->cntx_lock.guard));
        bufenv->BufferLocks[i] = 0;
    }
}

int
LockBuffer(Relation rel, Buffer buffer, int mode) {
    BufferDesc     *buf;
    bits8	    buflock = 0;
    int             locking_error = 0;

    Assert(BufferIsValid(buffer));

    if (BufferIsLocal(buffer))
        return locking_error;

    if ( rel != NULL ) {
        buflock = RelationGetBufferCxt(rel)->BufferLocks[buffer - 1];
    } else {

    }

    buf = &(BufferDescriptors[buffer - 1]);
    
    pthread_mutex_lock(&(buf->cntx_lock.guard));
    
    switch (mode) {
        case BUFFER_LOCK_UNLOCK:
            if ( rel == NULL ) {
                buflock |= BL_R_LOCK;
            }
            buflock = UnlockIndividualBuffer(buflock, buf);
            break;
        case BUFFER_LOCK_REF_EXCLUSIVE:
            /*
                    don't need the guard b/c once we get out of the
                    while loop, the only reference to the buffer
                    is the one we got,  broadcast after we set the
                    lock so that any blocked ManualPin will become unblocked
             */
            Assert(!(BL_R_LOCK & buflock));
            Assert(!(BL_W_LOCK & buflock));
            while ((buf->pageaccess) > (buf->e_waiting + 1)) {
                buf->e_waiting++;
                pthread_cond_wait(&buf->cntx_lock.gate, &(buf->cntx_lock.guard));
                buf->e_waiting--;
            }
            buf->w_lock = true;
            buf->e_lock = true;
            buflock |= BL_W_LOCK;
            break;
        case BUFFER_LOCK_WRITEIO:
/*  if the owner of this write lock is blocked and waiting for a
 *  write so buffers can be freed then there is no danger of changing the 
 *  buffer during a write.  if that is not the case just do what SHARE lock does
 */
            Assert((rel == NULL));
            if ( buf->w_lock && IsWaitingForFlush(buf->w_owner) ) {
               locking_error = BL_NOLOCK;
            } else {
                /* only wait for a write lock to finish, anything else would be problematic due to the test above  */
                while( buf->w_lock ) {
                    buf->r_waiting++;
                    pthread_cond_wait(&(buf->cntx_lock.gate), &(buf->cntx_lock.guard));
                    buf->r_waiting--;
                }
                (buf->r_locks)++;
                buflock |= BL_R_LOCK;
            }
            break;
        case BUFFER_LOCK_SHARE:
            /*  don't wait for e_waiting b/c it is useless unless pins wait for buf->e-waiting  */
            Assert(!(BL_R_LOCK & buflock));
            Assert(!(BL_W_LOCK & buflock));
            while( buf->w_lock || buf->w_waiting > 0 ) {
                buf->r_waiting++;
                pthread_cond_wait(&(buf->cntx_lock.gate), &(buf->cntx_lock.guard));
                buf->r_waiting--;
                if ( !buf->w_lock ) break;
            }
            (buf->r_locks)++;
            buflock |= BL_R_LOCK;
            break;
        case BUFFER_LOCK_EXCLUSIVE:
            Assert(!(BL_R_LOCK & buflock));
            Assert(!(BL_W_LOCK & buflock));
            while ( (buf->r_locks > 0) || buf->w_lock ) {
                buf->w_waiting++;
                pthread_cond_wait(&(buf->cntx_lock.gate), &(buf->cntx_lock.guard));
                buf->w_waiting--;
            }
            buf->w_owner = GetEnv()->eid;
            buf->w_lock = true;
            buflock |= BL_W_LOCK;
            break;
        default:
            elog(ERROR, "LockBuffer: unknown lock mode %d", mode);
    }
    pthread_mutex_unlock(&(buf->cntx_lock.guard));
    
    if ( rel != NULL ) RelationGetBufferCxt(rel)->BufferLocks[buffer-1] = buflock;
    
    return locking_error;
    
}

bits8 UnlockIndividualBuffer(bits8 buflock, BufferDesc * buf) {
    bool  signal = false;

    if (buflock & BL_R_LOCK) {
        (buf->r_locks)--;
        buflock &= ~BL_R_LOCK;
        if ( buf->r_locks == 0 ) {
            signal = true;
        }
    } else if (buflock & BL_W_LOCK) {
        if (buf->e_lock ) {
            Assert(buf->pageaccess <= buf->e_waiting + 1);
            if ( buf->p_waiting > 0 ) {
                pthread_cond_broadcast(&buf->cntx_lock.gate);
            } else if ( buf->e_waiting > 0 ) {
                pthread_cond_signal(&buf->cntx_lock.gate);
            }
            signal = false;
        } else  {
            signal = true;
        }
        buf->e_lock = false;
        buf->w_lock = false;
        buf->w_owner = 0;
        buflock &= ~BL_W_LOCK;
    }

    if ( signal ) {
        if ( buf->r_waiting > 0 ) {
            pthread_cond_broadcast(&buf->cntx_lock.gate);
        } else if ( (buf->w_waiting + buf->e_waiting) > 0 ) {
            pthread_cond_signal(&buf->cntx_lock.gate);
        }
    }
    
    return buflock;
}

bool
BufferHasError(Buffer buf) {
    int err = 0;
    
    if ( BufferIsLocal(buf) ) return false;

    BufferDesc* bufHdr = &BufferDescriptors[buf - 1];
    pthread_mutex_lock(&bufHdr->io_in_progress_lock.guard);
    err = ( bufHdr->ioflags & BM_IO_ERROR );
    pthread_mutex_unlock(&bufHdr->io_in_progress_lock.guard);
    return ( err ) ? true : false;
}

bool
WaitBufferIO(bool write_mode, BufferDesc *buf) {
    bool valid = true;
    int mask = ( write_mode ) ? (BM_IOOP_MASK) : (BM_READ_IN_PROGRESS | BM_INBOUND);
    
    pthread_mutex_lock(&buf->io_in_progress_lock.guard);
    
    while ( buf->ioflags & mask )  {
        pthread_cond_wait(&buf->io_in_progress_lock.gate, &buf->io_in_progress_lock.guard);
    }
    valid = !(buf->ioflags & BM_IO_ERROR);

    pthread_mutex_unlock(&buf->io_in_progress_lock.guard);

    DTRACE_PROBE4(mtpg,buffer__waitbufferio,buf->tag.relId.dbId,buf->tag.relId.relId,buf->tag.blockNum,valid);    

    return valid;
}

bool
InboundBufferIO(BufferDesc *buf) {  
    bool dirty = false;
    pthread_mutex_lock(&buf->io_in_progress_lock.guard);

    Assert( !(buf->ioflags & BM_IOOP_MASK) );
    
    buf->ioflags = 0;
    buf->ioflags |= (BM_INBOUND);
   
    pthread_mutex_unlock(&buf->io_in_progress_lock.guard);

     DTRACE_PROBE4(mtpg,buffer__inboundbufferio,buf->tag.relId.dbId,buf->tag.relId.relId,buf->tag.blockNum,dirty);    
   
    return dirty;  
}

bool
CancelInboundBufferIO(BufferDesc *buf) {  /*  clears the inbound flag  */
    bool dirty = false;
    pthread_mutex_lock(&buf->io_in_progress_lock.guard);

    Assert(buf->ioflags & BM_INBOUND);
    
    buf->ioflags &= ~(BM_INBOUND);
    buf->ioflags |= BM_IO_ERROR;
        
    pthread_cond_broadcast(&buf->io_in_progress_lock.gate);
        
    pthread_mutex_unlock(&buf->io_in_progress_lock.guard);
    
    return dirty;  
}

IOStatus
ReadBufferIO(BufferDesc *buf) {  /*  clears the inbound flag  */
    IOStatus iostatus = BL_NOLOCK;
    
    pthread_mutex_lock(&buf->io_in_progress_lock.guard);
	/* we would not be reading in the buffer is some other io is occuring */
    Assert( !(buf->ioflags & BM_IOOP_MASK) );
    
    Assert( BufferIsLocal(BufferDescriptorGetBuffer(buf)) || (buf->ioflags & BM_INBOUND) );
    
    if ( !( buf->ioflags & BM_IO_ERROR ) ) {
        buf->ioflags &= ~( BM_INBOUND );
        buf->ioflags |= (BM_READ_IN_PROGRESS);
    } else {
        iostatus = 0;
    }
    
    pthread_mutex_unlock(&buf->io_in_progress_lock.guard);

    DTRACE_PROBE4(mtpg,buffer__readbufferio,buf->tag.relId.dbId,buf->tag.relId.relId,buf->tag.blockNum,iostatus);

    return iostatus;
}

IOStatus
LogBufferIO(BufferDesc *buf) {  /*  clears the inbound flag  */
    int dirty = 0;
    IOStatus  iostatus = BL_NOLOCK;
    
    if ( 0 == LockBuffer(NULL,BufferDescriptorGetBuffer(buf),BUFFER_LOCK_WRITEIO) ) {
        iostatus = BL_R_LOCK;
    }

    pthread_mutex_lock(&buf->io_in_progress_lock.guard);

    while ( buf->ioflags & BM_IOOP_MASK ) {
        pthread_cond_wait(&buf->io_in_progress_lock.gate, &buf->io_in_progress_lock.guard);
    }

    if ( (buf->ioflags & BM_IO_ERROR) ) {
        pthread_mutex_unlock(&buf->io_in_progress_lock.guard);
        LockBuffer(NULL,BufferDescriptorGetBuffer(buf),BUFFER_LOCK_UNLOCK);
        iostatus = 0;
        return iostatus;
    }

    dirty = (buf->ioflags & BM_DIRTY);

    if ( dirty ) {
        buf->ioflags |= (BM_LOG_IN_PROGRESS);
        buf->ioflags &= ~(BM_DIRTY);
    }

    pthread_mutex_unlock(&buf->io_in_progress_lock.guard);
    
    DTRACE_PROBE4(mtpg,buffer__logbufferio,buf->tag.relId.dbId,buf->tag.relId.relId,buf->tag.blockNum,dirty);
    
    if ( !dirty ) {
        LockBuffer(NULL,BufferDescriptorGetBuffer(buf),BUFFER_LOCK_UNLOCK);
        iostatus = 0;
    }
    
    return iostatus;
}

IOStatus
WriteBufferIO(BufferDesc *buf, bool flush) {  /*  clears the inbound flag  */
    int dirty = 0;
    IOStatus  iostatus = BL_NOLOCK;
    
    if ( 0 == LockBuffer(NULL,BufferDescriptorGetBuffer(buf),BUFFER_LOCK_WRITEIO) ) {
        iostatus = BL_R_LOCK;
    } 

    pthread_mutex_lock(&buf->io_in_progress_lock.guard);

    while ( buf->ioflags & BM_IOOP_MASK ) {
        pthread_cond_wait(&buf->io_in_progress_lock.gate, &buf->io_in_progress_lock.guard);
    }

    if ( (buf->ioflags & BM_IO_ERROR) ) {
        pthread_mutex_unlock(&buf->io_in_progress_lock.guard);
        LockBuffer(NULL,BufferDescriptorGetBuffer(buf),BUFFER_LOCK_UNLOCK);
        iostatus = 0;
        return iostatus;
    }

/*  flushes are always dirty  */
    if ( flush ) {
        dirty = 1;
/*  file will be flushed  */
        buf->ioflags &= ~(BM_LOGGED);
    } else {
        if ( !IsMultiuser() ) {
    /*  in single user mode, no logging is done  
        check for dirty */
            dirty = (buf->ioflags & BM_DIRTY);  
            buf->ioflags &= ~(BM_DIRTY);
        } else {
            dirty = (buf->ioflags & BM_LOGGED);  
            buf->ioflags &= ~(BM_LOGGED);
        }
    }
    
    if ( dirty ) {
 /*  logging is skipped in single user mode  */
        buf->ioflags |= BM_WRITE_IN_PROGRESS;
    }

    pthread_mutex_unlock(&buf->io_in_progress_lock.guard);
    
    DTRACE_PROBE4(mtpg,buffer__writebufferio,buf->tag.relId.dbId,buf->tag.relId.relId,buf->tag.blockNum,dirty);
    
    if ( !dirty ) {
        LockBuffer(NULL,BufferDescriptorGetBuffer(buf),BUFFER_LOCK_UNLOCK);
        iostatus = 0;
    }
    
    return iostatus;
}

bool
DirtyBufferIO(BufferDesc *buf) {  /*  clears the inbound flag  */
    bool dirty = false;
    pthread_mutex_lock(&buf->io_in_progress_lock.guard);
    
    if ( !(buf->ioflags & BM_IO_ERROR) ) {
        buf->ioflags |= (BM_DIRTY); 
        dirty = true;
    }
    
    pthread_mutex_unlock(&buf->io_in_progress_lock.guard);
    
    return dirty;
}


bool
IsDirtyBufferIO(BufferDesc *buf) {  /*  clears the inbound flag  */
    bool dirty = false;
    pthread_mutex_lock(&buf->io_in_progress_lock.guard);
    
    if ( !(buf->ioflags & BM_IO_ERROR) && (buf->ioflags & BM_DIRTY) ) {
        dirty = true;
    }
    
    pthread_mutex_unlock(&buf->io_in_progress_lock.guard);
    return dirty;
}

void
ErrorBufferIO(IOStatus iostatus, BufferDesc *buf) {

    pthread_mutex_lock(&buf->io_in_progress_lock.guard);

    buf->ioflags |= BM_IO_ERROR;
    buf->ioflags &= ~(BM_IOOP_MASK);

    pthread_cond_broadcast(&buf->io_in_progress_lock.gate);

    pthread_mutex_unlock(&buf->io_in_progress_lock.guard);

    if ( iostatus & BL_R_LOCK ) {
        Assert(iostatus == BL_R_LOCK);
        LockBuffer(NULL,BufferDescriptorGetBuffer(buf),BUFFER_LOCK_UNLOCK);
    }
}

bool 
ClearBufferIO(BufferDesc *buf) {
    pthread_mutex_lock(&buf->io_in_progress_lock.guard);

    if ( (buf->ioflags & BM_IO_ERROR) ) {
        pthread_mutex_unlock(&buf->io_in_progress_lock.guard);
        return false;
    }
    
    while ( buf->ioflags & (BM_IOOP_MASK) )  {
        pthread_cond_wait(&buf->io_in_progress_lock.gate, &buf->io_in_progress_lock.guard);
    }

    buf->ioflags |= BM_IO_ERROR;

    pthread_cond_broadcast(&buf->io_in_progress_lock.gate);    

    pthread_mutex_unlock(&buf->io_in_progress_lock.guard);
    
    return false;
}

/*
 * Function:TerminateBufferIO
 *	(Assumptions)
 *	My process is executing IO for the buffer
 *	BufMgrLock is held
 *	The buffer is Pinned
 *
 */
void
TerminateBufferIO(IOStatus iostatus, BufferDesc *buf) {
    pthread_mutex_lock(&buf->io_in_progress_lock.guard);
    
    ShowBufferIO(buf->buf_id,buf->ioflags);

    if ( buf->ioflags & (BM_IOOP_MASK) ) {
/*  if IO is happening alert waiters they need to check state  */
        if ( buf->ioflags & BM_LOG_IN_PROGRESS ) {
            buf->ioflags |= BM_LOGGED;
        } else if ( buf->ioflags & BM_WRITE_IN_PROGRESS ) {

        }
        buf->ioflags &= ~(BM_IOOP_MASK);
        pthread_cond_broadcast(&buf->io_in_progress_lock.gate);
    }
  
    pthread_mutex_unlock(&buf->io_in_progress_lock.guard);
    
    if ( iostatus & BL_R_LOCK ) {
        Assert(iostatus == BL_R_LOCK);
        LockBuffer(NULL,BufferDescriptorGetBuffer(buf),BUFFER_LOCK_UNLOCK);
    }    
}

void 
ShowBufferIO(int id, int flags) {
/*
 printf("buf:%d flags:%d\n",id,current);
*/
}

/*
 *	This function is called from ProcReleaseSpins().
 *	BufMgrLock isn't held when this function is called.
 *	BM_IO_ERROR is always set. If BM_IO_ERROR was already
 *	set in case of output,this routine would kill all
 *	backends and reset postmaster.
 */
void
AbortBufferIO(void) {

}

bool
CheckBufferId(BufferDesc* buf, BlockNumber block, Oid relid, Oid dbid) {
    bool  valid = true;
    pthread_mutex_lock(&(buf->cntx_lock.guard));

    Assert(buf->refCount > 0);
    
    if ( relid != -1 && buf->tag.relId.relId != relid ) valid = false;
    if ( dbid != (Oid)0 && buf->tag.relId.dbId != dbid ) valid = false;
    if (  BlockNumberIsValid(block) && buf->tag.blockNum != block ) valid = false;
    if ( !(buf->locflags & BM_VALID) ) valid = false;
    
    pthread_mutex_unlock(&(buf->cntx_lock.guard));
    
    return valid;
}

bool
SyncShadowPage(BufferDesc* bufHdr) {
    /*  copy into shadow */
#ifdef USE_SHADOW_PAGES
    memcpy((char*)MAKE_PTR(bufHdr->data), (char*)MAKE_PTR(bufHdr->shadow), BLCKSZ);
#endif
}

/*
 * BufferGetBlock
 *		Returns a reference to a disk page image associated with a buffer.
 *
 * Note:
 *		Assumes buffer is valid.
 */
Block BufferGetBlock(Buffer buffer) {
    Assert(BufferIsValid(buffer));
    if ( BufferIsLocal(buffer) ) {
        return (Block) MAKE_PTR(GetLocalBufferDescriptor((-buffer) - 1)->data);
    } else {
        return (Block) MAKE_PTR(BufferDescriptors[(buffer) - 1].data);
    }
}

/*
 * IncrBufferRefCount
 *		Increment the pin count on a buffer that we have *already* pinned
 *		at least once.
 *
 *		This macro cannot be used on a buffer we do not have pinned,
 *		because it doesn't change the shared buffer state.  Therefore the
 *		Assert checks are for refcount > 0.  Someone got this wrong once...
 */
void IncrBufferRefCount(Relation rel, Buffer buffer) {
    if ( BufferIsLocal(buffer) ) {
        IncrLocalBufferRefCount(-(buffer) - 1);
    } else {
        Assert(!BAD_BUFFER_ID(buffer));
	BufferCxt cxt = RelationGetBufferCxt(rel);
        Assert(cxt->PrivateRefCount[(buffer) - 1] > 0);
        cxt->PrivateRefCount[(buffer) - 1]++;
        cxt->total_pins++;
    }
}

/*
 * PinBuffer -- make buffer unavailable for replacement.
 */
int
PinBuffer(BufferCxt cxt, BufferDesc *buf) {
    int valid = 1;
    
    if (cxt->PrivateRefCount[buf->buf_id] == 0) {
        valid = ManualPin(buf, true);
    }
    if ( valid ) {
        cxt->PrivateRefCount[buf->buf_id]++;
        cxt->total_pins++;
    }

    return valid;
}

/*
 * UnpinBuffer -- make buffer available for replacement.
 */
void
UnpinBuffer(BufferCxt cxt, BufferDesc *buf) {
    if ( cxt->PrivateRefCount[buf->buf_id] == 0 ) {
        elog(DEBUG,"too many unpins");
    }
    cxt->PrivateRefCount[buf->buf_id]--;
    cxt->total_pins--;
    if (cxt->PrivateRefCount[buf->buf_id] == 0) {
        ManualUnpin(buf, true);
    }
}

void
InvalidateBuffer(BufferCxt cxt,BufferDesc *buf) {
    ClearBufferIO(buf);
    BufTableDelete(buf);
}

BufferCxt
GetBufferCxt() {
    /*  ignore cleanup, its done by the memory context  */
    BufferEnv* env = buffers_global;
    MemoryContext oldcxt;

    if ( env == NULL ) {
        env = AllocateEnvSpace(buffer_section_id, sizeof(BufferEnv));

        oldcxt = MemoryContextSwitchTo(MemoryContextGetTopContext());
        
        env->guard = 0xCAFEBABECAFEBABEL;
        
        env->PrivateRefCount = (long *)palloc(NBuffers*sizeof(long));
        memset(env->PrivateRefCount, 0, NBuffers*sizeof(long));
        env->total_pins = 0;
        
        env->BufferLocks = (bits8 *) palloc(NBuffers*sizeof(bits8));
        memset(env->BufferLocks, 0, NBuffers* sizeof(bits8));
        
        env->BufferTagLastDirtied = (BufferTag *) palloc(NBuffers*sizeof(BufferTag));
        memset(env->BufferTagLastDirtied, 0, NBuffers*sizeof(BufferTag));
        
        env->BufferBlindLastDirtied = (BufferBlindId *) palloc(NBuffers*sizeof(BufferBlindId));
        memset(env->BufferBlindLastDirtied , 0, NBuffers*sizeof(BufferBlindId));
        
        env->DidWrite    = false;
        
        MemoryContextSwitchTo(oldcxt);
        
        buffers_global = env;
    }

    return env;
}

