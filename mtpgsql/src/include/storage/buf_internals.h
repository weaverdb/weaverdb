/*-------------------------------------------------------------------------
 *
 * buf_internals.h
 *	  Internal definitions for buffer manager.
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * $Id: buf_internals.h,v 1.4 2007/04/11 00:11:14 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */
#ifndef BUFMGR_INTERNALS_H
#define BUFMGR_INTERNALS_H

#include "storage/buf.h"
#include "storage/lmgr.h"

/* Buf Mgr constants */
/* in bufmgr.c */
extern int	NBuffers;
extern int	Data_Descriptors;
extern int	Free_List_Descriptor;
extern int	Lookup_List_Descriptor;
extern int	Num_Descriptors;

/*
 * Flags for buffer descriptors
 */
/* Location and Locking Flags */
#define BM_USED 				(1 << 0)/* 1 */
#define BM_VALID				(1 << 1)/* 2 */
#define BM_DELETED				(1 << 2)/* 4 */
#define BM_FREE					(1 << 3)/* 8 */
#define BM_WRITELOCK				(1 << 4)/* 16 */
#define BM_EXCLUSIVE				(1 << 5)/* 32 */
#define BM_CRITICAL                             (1 << 6)/*64*/
#define BM_WRITEIO                             (1 << 7)/*128*/
#define BM_CRITICALMASK                          (BM_WRITELOCK | BM_CRITICAL)
#define BM_EXCLUSIVEMASK                          (BM_WRITELOCK | BM_EXCLUSIVE | BM_CRITICAL)
#define BM_REMOVEWRITEMASK                          ~(BM_WRITELOCK | BM_EXCLUSIVE | BM_CRITICAL)

/* IO Flags */
#define BM_DIRTY				(1 << 0) /* 1 */
#define BM_LOGGED                               (1 << 1)/* 2 */

#define BM_IO_ERROR				(1 << 5)/* 32 */
#define BM_INBOUND				(1 << 6)/* 64 */
#define BM_READONLY                             (1 << 7) /*  128  */

#define BM_READ_IN_PROGRESS			(1 << 8)
#define BM_LOG_IN_PROGRESS			(1 << 9)
#define BM_WRITE_IN_PROGRESS			(1 << 10)
#define BM_FLUSH_IN_PROGRESS                    (1 << 11)

#define BM_IOOP_MASK                       0x0F00

typedef bits16 BufFlags;

/* long * so alignment will be correct */
typedef long **BufferBlock;

typedef struct BufferTable {
        pthread_mutex_t     lock;
        HTAB*               table;
} BufferTable;

typedef struct buftag
{
	LockRelId	relId;
	BlockNumber blockNum;		/* blknum relative to begin of reln */
} BufferTag;

typedef struct lookup
{
	BufferTag	key;
	Buffer		id;
} BufferLookupEnt;

#define CLEAR_BUFFERTAG(a) \
( \
	(a)->relId.dbId = InvalidOid, \
	(a)->relId.relId = InvalidOid, \
	(a)->blockNum = InvalidBlockNumber \
)

#define INIT_BUFFERTAG(a,xx_reln,xx_blockNum) \
( \
	(a)->blockNum = (xx_blockNum), \
	(a)->relId.relId = (xx_reln)->rd_lockInfo.lockRelId.relId, \
	(a)->relId.dbId = (xx_reln)->rd_lockInfo.lockRelId.dbId \
)

/* If we have to write a buffer "blind" (without a relcache entry),
 * the BufferTag is not enough information.  BufferBlindId carries the
 * additional information needed.
 */
typedef struct bufblindid
{
	char		dbname[NAMEDATALEN];	/* name of db in which buf belongs */
	char		relname[NAMEDATALEN];	/* name of reln */
}			BufferBlindId;

#define BAD_BUFFER_ID(bid) ((bid) < 1 || (bid) > NBuffers)
#define INVALID_DESCRIPTOR (-3)
#define DETACHED_DESCRIPTOR (-4)

typedef struct iogate {
	pthread_mutex_t		guard;
	pthread_cond_t		gate;
} IOGate;


/*
 *	BufferDesc -- shared buffer cache metadata for a single
 *				  shared buffer descriptor.
 *
 *		We keep the name of the database and relation in which this
 *		buffer appears in order to avoid a catalog lookup on cache
 *		flush if we don't have the reldesc in the cache.  It is also
 *		possible that the relation to which this buffer belongs is
 *		not visible to all backends at the time that it gets flushed.
 *		Dbname, relname, dbid, and relid are enough to determine where
 *		to put the buffer, for all storage managers.
 */
typedef struct sbufdesc
{
	Buffer		freeNext;		/* links for freelist chain */
	SHMEM_OFFSET 	data;			/* pointer to data in buf pool */
	SHMEM_OFFSET 	shadow;			/* pointer to data in buf pool */

	/* tag and id must be together for table lookup to work */
	BufferTag	tag;			/* file/block identifier */
        char            kind;
	int		buf_id;			/* maps global desc to local desc */

	BufFlags	locflags;			/* see bit definitions above */
	unsigned	refCount;		/* # of times buffer is pinned */
	unsigned	pageaccess;		/* # of pins that actually access the data on the page */
        
	BufFlags	ioflags;			/* see bit definitions above */
	IOGate		io_in_progress_lock;
	IOGate		cntx_lock;		/* to lock access to page context */

        unsigned        w_owner;
	unsigned	r_locks;		/* # of shared locks */
        unsigned	e_waiting;              /*  waiting for exclusive lock   */
        unsigned	w_waiting;              /*  waiting for write lock   */
        unsigned	r_waiting;              /*  waiting for read lock   */
        unsigned	p_waiting;              /*  waiting for page exclusive lock   */

        unsigned        bias;

	BufferBlindId blind;		/* extra info to support blind write */
} BufferDesc;

/*
 * Each backend has its own BufferLocks[] array holding flag bits
 * showing what locks it has set on each buffer.
 *
 * We have to free these locks in elog(ERROR)...
 */
#define BL_IO_IN_PROGRESS	(1 << 0)	/* unimplemented */
#define BL_R_LOCK			(1 << 1)
#define BL_RI_LOCK			(1 << 2)
#define BL_W_LOCK			(1 << 3)
#define BL_NOLOCK               (1 << 4)
#define BL_CRITICAL               (1 << 5)

typedef         bits8          IOStatus;
/*
 *	mao tracing buffer allocation
 */

/*#define BMTRACE*/
#ifdef BMTRACE

typedef struct _bmtrace
{
	int			bmt_pid;
	long		bmt_buf;
	long		bmt_dbid;
	long		bmt_relid;
	int			bmt_blkno;
	int			bmt_op;

#define BMT_NOTUSED		0
#define BMT_ALLOCFND	1
#define BMT_ALLOCNOTFND 2
#define BMT_DEALLOC		3

}			bmtrace;

#endif	 /* BMTRACE */


/*
 * Bufmgr Interface:
 */

/* Internal routines: only called by buf.c */

/*freelist.c*/


PG_EXTERN int ManualPin(BufferDesc* buf, bool pageaccess);
PG_EXTERN int ManualUnpin(BufferDesc* buf, bool pageaccess);

PG_EXTERN int BiasPinned(BufferDesc* buf);

PG_EXTERN bool IsWaitingForFlush(unsigned owner);
PG_EXTERN BufferDesc *GetFreeBuffer(Relation rel);
PG_EXTERN void PutFreeBuffer(BufferDesc* bufHdr);
PG_EXTERN void InitFreeList(bool init);

/* buf_table.c */

PG_EXTERN void InitBufTable(int tables);
PG_EXTERN BufferDesc *BufTableLookup(char kind, BufferTag *tagPtr);
PG_EXTERN bool BufTableDelete(BufferDesc *buf);
PG_EXTERN bool  BufTableReplace(BufferDesc *buf, Relation rel, BlockNumber block);
/* bufmgr.c */
PG_EXTERN BufferDesc *BufferDescriptors;
PG_EXTERN BufferBlock BufferBlocks;

PG_EXTERN SPINLOCK HeapBufLock;
PG_EXTERN SPINLOCK IndexBufLock;
PG_EXTERN SPINLOCK FreeBufMgrLock;

/* localbuf.c */
/*
PG_EXTERN long *LocalRefCount;
PG_EXTERN BufferDesc *LocalBufferDescriptors;
*/
PG_EXTERN const int	NLocBuffer;

PG_EXTERN BufferDesc *LocalBufferAlloc(Relation reln, BlockNumber blockNum,bool *foundPtr);
PG_EXTERN int	WriteLocalBuffer(Buffer buffer, bool release);
PG_EXTERN int	FlushLocalBuffer(Buffer buffer);

PG_EXTERN void LocalBufferSync(void);
PG_EXTERN void ResetLocalBufferPool(void);

#endif	 /* BUFMGR_INTERNALS_H */
