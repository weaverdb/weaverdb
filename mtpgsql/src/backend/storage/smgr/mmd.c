/*-------------------------------------------------------------------------
 *
 * mmd.c
 *	  This code manages relations that reside on magnetic disk.
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  $Header: /cvs/weaver/mtpgsql/src/backend/storage/smgr/mmd.c,v 1.1.1.1 2006/08/12 00:21:31 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/file.h>

#include "postgres.h"

#include "env/env.h"
#include "catalog/catalog.h"
#include "catalog/catname.h"
#include "miscadmin.h"
#include "storage/smgr.h"
#include "utils/inval.h"		/* ImmediateSharedRelationCacheInvalidate()*/
#include "utils/memutils.h"
#include "utils/relcache.h"

#undef DIAGNOSTIC

/*
 *	The magnetic disk storage manager keeps track of open file descriptors
 *	in its own descriptor pool.  This happens for two reasons.	First, at
 *	transaction boundaries, we walk the list of descriptors and flush
 *	anything that we've dirtied in the current transaction.  Second, we want
 *	to support relations larger than the OS' file size limit (often 2GBytes).
 *	In order to do that, we break relations up into chunks of < 2GBytes
 *	and store one chunk in each of several files that represent the relation.
 *	See the BLCKSZ and RELSEG_SIZE configuration constants in include/config.h.
 *
 *	The file descriptor stored in the relation cache (see RelationGetFile())
 *	is actually an index into the Md_fdvec array.  -1 indicates not open.
 *
 *	When a relation is broken into multiple chunks, only the first chunk
 *	has its own entry in the Md_fdvec array; the remaining chunks have
 *	palloc'd MdfdVec objects that are chained onto the first chunk via the
 *	mmdfd_chain links.  All chunks except the last MUST have size exactly
 *	equal to RELSEG_SIZE blocks --- see mmdnblocks() and mmdtruncate().
 */
 typedef struct _MdfdVec
{
	int			mdfd_vfd;		/* fd number in vfd pool */
	int			mdfd_flags;		/* fd status flags */

/* these are the assigned bits in mdfd_flags: */
#define MDFD_FREE		(1 << 0)/* unused entry */

	BlockNumber			mdfd_lstbcnt;	/* most recent block count */
	int			mdfd_nextFree;	/* next free vector */
#ifndef LET_OS_MANAGE_FILESIZE
	struct _MdfdVec *mdfd_chain;/* for large relations */
#endif
} MdfdVec;

 
typedef struct file_system {
    MemoryContext        MmdContext;
	char*			filemap;
	int				Nfds;			/* initial/current size of Md_fdvec array */
	MdfdVec *		Md_fdvec;
	int				Md_Free	;		/* head of freelist of unused fdvec   */
	int				CurFd;			/* first never-used fdvec index */    
} FSMemory;

static SectionId  mmd_id = SECTIONID("MMDC");

static pthread_mutex_t*		mgate;

/* routines declared here */
static void mmdclose_fd(int fd);
static int	_mmdfd_getrelnfd(Relation reln);
static MdfdVec *_mmdfd_openseg(Relation reln, int segno, int oflags, FSMemory* mem);
static MdfdVec *_mmdfd_getseg(Relation reln, BlockNumber blkno, FSMemory* mem);
static int _mmdfd_blind_getseg(char *dbname, char *relname,
				   Oid dbid, Oid relid, BlockNumber blkno, FSMemory* mem);
static int	_fdvec_alloc(void);
static void _fdvec_free(int);
static BlockNumber _mmdnblocks(File file, Size blcksz);

static int _internal_mmdnblocks(Relation reln, FSMemory* mem);


/*
 *	mmdinit() -- Initialize private state for magnetic disk storage manager.
 *
 *		We keep a private table of all file descriptors.  Whenever we do
 *		a write to one, we mark it dirty in our table.	Whenever we force
 *		changes to disk, we mark the file descriptor clean.  At transaction
 *		commit, we force changes to disk for all dirty file descriptors.
 *		This routine allocates and initializes the table.
 *
 *		Returns SM_SUCCESS or SM_FAIL with errno set as appropriate.
 */
int
mmdinit()
{
	MemoryContext oldcxt;
	int			i;
	FSMemory*   fsm = (FSMemory*)AllocateEnvSpace(mmd_id,sizeof(FSMemory));
    
    
    fsm->Nfds = 100;
	fsm->Md_Free = -1;
		
    
	fsm->MmdContext =  AllocSetContextCreate(MemoryContextGetTopContext(),
												   "MmdMemoryContext",
												ALLOCSET_DEFAULT_MINSIZE,
											   ALLOCSET_DEFAULT_INITSIZE,
											   ALLOCSET_DEFAULT_MAXSIZE);
	if (fsm->MmdContext == (MemoryContext) NULL)
		return SM_FAIL;

	oldcxt = MemoryContextSwitchTo(fsm->MmdContext);
	fsm->Md_fdvec = (MdfdVec *) palloc(fsm->Nfds * sizeof(MdfdVec));
	MemoryContextSwitchTo(oldcxt);

	if (fsm->Md_fdvec == (MdfdVec *) NULL)
		return SM_FAIL;

	MemSet(fsm->Md_fdvec, 0, fsm->Nfds * sizeof(MdfdVec));

	/* Set free list */
	for (i = 0; i < fsm->Nfds; i++)
	{
		fsm->Md_fdvec[i].mdfd_nextFree = i + 1;
		fsm->Md_fdvec[i].mdfd_flags = MDFD_FREE;
	}
	fsm->Md_Free = 0;
	fsm->Md_fdvec[fsm->Nfds - 1].mdfd_nextFree = -1;
	

	return SM_SUCCESS;
}

int
mmdshutdown()
{
	FSMemory*   fsm = (FSMemory*)GetEnvSpace(mmd_id);
	int count = 0;
	
	for(count=0;count<fsm->Nfds;count++) {
		if (!(fsm->Md_fdvec[count].mdfd_flags & MDFD_FREE) )
 			mmdclose_fd(count);
	}
	
	return SM_SUCCESS;
}

int
mmdcreate(Relation reln)
{
	int			fd,
				vfd;
	char	   *path;
	FSMemory*   fsm = (FSMemory*)GetEnvSpace(mmd_id);
	MemoryContext   old = MemoryContextSwitchTo(fsm->MmdContext);

	Assert(reln->rd_unlinked && reln->rd_fd < 0);
	path = relpath(RelationGetPhysicalRelationName(reln));   
#ifndef __CYGWIN32__
	fd = FileNameOpenFile(path, O_RDWR | O_CREAT | O_EXCL, 0600);
#else
	fd = FileNameOpenFile(path, O_RDWR | O_CREAT | O_EXCL | O_BINARY, 0600);
#endif

	/*
	 * During bootstrap processing, we skip that check, because pg_time,
	 * pg_variable, and pg_log get created before their .bki file entries
	 * are processed.
	 *
	 * For cataloged relations,pg_class is guaranteed to have an unique
	 * record with the same relname by the unique index. So we are able to
	 * reuse existent files for new catloged relations. Currently we reuse
	 * them in the following cases. 1. they are empty. 2. they are used
	 * for Index relations and their size == BLCKSZ * 2.
	 */

	if (fd < 0)
	{
		if (!IsBootstrapProcessingMode() &&
			reln->rd_rel->relkind == RELKIND_UNCATALOGED) {
			pfree(path);
			MemoryContextSwitchTo(old);
			return -1;
		}

#ifndef __CYGWIN32__
		fd = FileNameOpenFile(path, O_RDWR, 0600);
#else
		fd = FileNameOpenFile(path, O_RDWR | O_BINARY, 0600);
#endif

		if (fd < 0) {
			pfree(path);
			MemoryContextSwitchTo(old);
			return -1;
		}
		if (!IsBootstrapProcessingMode())
		{
			bool		reuse = true;
			int			len;
			
			FilePin(fd,1);
			len = FileSeek(fd, 0L, SEEK_END);
			FileTruncate(fd, 0);
			FileUnpin(fd,1);
		}
	}
	reln->rd_unlinked = false;

	vfd = _fdvec_alloc();
	if (vfd < 0) {
		pfree(path);
		MemoryContextSwitchTo(old);
		return -1;
	}

	fsm->Md_fdvec[vfd].mdfd_vfd = fd;
	fsm->Md_fdvec[vfd].mdfd_flags = (uint16) 0;
#ifndef LET_OS_MANAGE_FILESIZE
	fsm->Md_fdvec[vfd].mdfd_chain = (MdfdVec *) NULL;
#endif
	fsm->Md_fdvec[vfd].mdfd_lstbcnt = 0;

	pfree(path);
	MemoryContextSwitchTo(old);

	return vfd;
}

/*
 *	mdunlink() -- Unlink a relation.
 */
int
mmdunlink(Relation reln)
{
	int			nblocks;
	int			fd;
	MdfdVec    *v;
	MemoryContext oldcxt;
	FSMemory*   fsm = (FSMemory*)GetEnvSpace(mmd_id);

	/*
	 * If the relation is already unlinked,we have nothing to do any more.
	 */
	if (reln->rd_unlinked && reln->rd_fd < 0)
		return SM_SUCCESS;

	/*
	 * Force all segments of the relation to be opened, so that we won't
	 * miss deleting any of them.
	 */
	nblocks = mmdnblocks(reln);

	/*
	 * Clean out the mdfd vector, letting fd.c unlink the physical files.
	 *
	 * NOTE: We truncate the file(s) before deleting 'em, because if other
	 * backends are holding the files open, the unlink will fail on some
	 * platforms (think Microsoft).  Better a zero-size file gets left
	 * around than a big file.	Those other backends will be forced to
	 * close the relation by cache invalidation, but that probably hasn't
	 * happened yet.
	 */
	fd = RelationGetFile(reln);
	if (fd < 0)		{			/* should not happen */
		elog(NOTICE, "mdunlink: mdnblocks didn't open relation");
		return SM_FAIL;
	}

	fsm->Md_fdvec[fd].mdfd_flags = (uint16) 0;

	oldcxt = MemoryContextSwitchTo(fsm->MmdContext);
#ifndef LET_OS_MANAGE_FILESIZE
	for (v = &fsm->Md_fdvec[fd]; v != (MdfdVec *) NULL;)
	{
		MdfdVec    *ov = v;

		FilePin(v->mdfd_vfd,2);
		FileTruncate(v->mdfd_vfd, 0);
		FileUnpin(v->mdfd_vfd,2);
		FileUnlink(v->mdfd_vfd);
		v = v->mdfd_chain;
		if (ov != &fsm->Md_fdvec[fd])
			pfree(ov);
	}
	fsm->Md_fdvec[fd].mdfd_chain = (MdfdVec *) NULL;
#else
	v = &fsm->Md_fdvec[fd];
	FilePin(v->mdfd_vfd,3);
	FileTruncate(v->mdfd_vfd, 0);
	FileUnpin(v->mdfd_vfd,3);
	FileUnlink(v->mdfd_vfd);
#endif

	MemoryContextSwitchTo(oldcxt);
	
	_fdvec_free(fd);

	/* be sure to mark relation closed && unlinked */
	reln->rd_fd = -1;
	reln->rd_unlinked = true;

	ImmediateSharedRelationCacheInvalidate(reln);  

	return SM_SUCCESS;
}

/*
 *	mdextend() -- Add a block to the specified relation.
 *
 *		This routine returns SM_FAIL or SM_SUCCESS, with errno set as
 *		appropriate.
 */
int
mmdextend(Relation reln, char *buffer)
{
	long		pos,
				nbytes;
	int			nblocks;
	MdfdVec    *v;
	FSMemory*   fsm = (FSMemory*)GetEnvSpace(mmd_id);

	v = _mmdfd_getseg(reln, 0, fsm);

	FilePin(v->mdfd_vfd,4);
	
        nblocks = _internal_mmdnblocks(reln, fsm);

	v = _mmdfd_getseg(reln, nblocks, fsm);

	if ((pos = FileSeek(v->mdfd_vfd, 0L, SEEK_END)) < 0) {
		elog(NOTICE,"bad seek");
		perror("bad seek");
		FileUnpin(v->mdfd_vfd,4);
		return SM_FAIL;
	}

	if (pos % BLCKSZ != 0)		/* the last block is incomplete */
	{
		elog(NOTICE,"bad end");
		pos -= pos % BLCKSZ;
		if (FileSeek(v->mdfd_vfd, pos, SEEK_SET) < 0) {
			FileUnpin(v->mdfd_vfd,4);
			return SM_FAIL;
		}
	}

	if ((nbytes = FileWrite(v->mdfd_vfd, buffer, BLCKSZ)) != BLCKSZ)
	{
		elog(NOTICE,"bad write");
		if (nbytes > 0)
		{
			FileTruncate(v->mdfd_vfd, pos);
			FileSeek(v->mdfd_vfd, pos, SEEK_SET);
		}
		FileUnpin(v->mdfd_vfd,4);
		return SM_FAIL;
	}
	/* try to keep the last block count current, though it's just a hint */
#ifndef LET_OS_MANAGE_FILESIZE
	if ((v->mdfd_lstbcnt = (++nblocks % RELSEG_SIZE)) == 0)
		v->mdfd_lstbcnt = RELSEG_SIZE;

#ifdef DIAGNOSTIC
	if (_mmdnblocks(v->mdfd_vfd, BLCKSZ) > RELSEG_SIZE
		|| v->mdfd_lstbcnt > RELSEG_SIZE) {
		FileUnpin(v->mdfd_vfd,4);
		elog(FATAL, "segment too big!");
	}
#endif
#else
	v->mdfd_lstbcnt = ++nblocks;
#endif
	FileUnpin(v->mdfd_vfd,4);

	return SM_SUCCESS;
}

/*
 *	mdopen() -- Open the specified relation.
 */
int
mmdopen(Relation reln)
{
	char	   *path;
	int			fd;
	int			vfd;
	FSMemory*   fsm = (FSMemory*)GetEnvSpace(mmd_id);
	MemoryContext  old = MemoryContextSwitchTo(fsm->MmdContext);

	errno = 0;
	
	Assert(reln->rd_fd < 0);
        
        path = relpath(RelationGetPhysicalRelationName(reln));    

#ifndef __CYGWIN32__
	fd = FileNameOpenFile(path, O_RDWR, 0600);
#else
	fd = FileNameOpenFile(path, O_RDWR | O_BINARY, 0600);
#endif

	
	if (fd < 0 )
	{
		
		/* in bootstrap mode, accept mdopen as substitute for mdcreate */
/*		try this anytime because add TransactionId cycle right before file creation or deletion */
		if (IsBootstrapProcessingMode())    
		{
#ifndef __CYGWIN32__
			fd = FileNameOpenFile(path, O_RDWR | O_CREAT | O_EXCL, 0600);
#else
			fd = FileNameOpenFile(path, O_RDWR | O_CREAT | O_EXCL | O_BINARY, 0600);
#endif
		} else {
			fd = FileNameOpenFile(path, O_RDWR | O_CREAT , 0600);
		}
		if (fd < 0)
		{
			elog(NOTICE, "mddopen: couldn't open %s: %m", path);
			/* mark relation closed and unlinked */
			reln->rd_fd = -1;
			reln->rd_unlinked = true;
			pfree(path);
			MemoryContextSwitchTo(old);
			return -1;
		}
	}
	reln->rd_unlinked = false;

	vfd = _fdvec_alloc();
	if (vfd < 0) {
		pfree(path);
		MemoryContextSwitchTo(old);

		return -1;
	}

	FilePin(fd,5);
	fsm->Md_fdvec[vfd].mdfd_vfd = fd;
	fsm->Md_fdvec[vfd].mdfd_flags = (uint16) 0;
	fsm->Md_fdvec[vfd].mdfd_lstbcnt = _mmdnblocks(fd, BLCKSZ);
	FileUnpin(fd,5);
#ifndef LET_OS_MANAGE_FILESIZE
	fsm->Md_fdvec[vfd].mdfd_chain = (MdfdVec *) NULL;

#ifdef DIAGNOSTIC
	if (fsm->Md_fdvec[vfd].mdfd_lstbcnt > RELSEG_SIZE)
		elog(FATAL, "segment too big on relopen!");
#endif
#endif

	pfree(path);
	MemoryContextSwitchTo(old);

	return vfd;
}

/*
 *	mdclose() -- Close the specified relation, if it isn't closed already.
 *
 *		AND FREE fd vector! It may be re-used for other relation!
 *		reln should be flushed from cache after closing !..
 *
 *		Returns SM_SUCCESS or SM_FAIL with errno set as appropriate.
 */
int
mmdclose(Relation reln)
{
	int			fd;

	fd = RelationGetFile(reln);
	if (fd < 0)
		return SM_SUCCESS;		/* already closed, so no work */

	mmdclose_fd(fd);

	reln->rd_fd = -1;

	return SM_SUCCESS;
}

static void
mmdclose_fd(int fd)
{
	MdfdVec    *v;
	MemoryContext oldcxt;
	FSMemory*   fsm = (FSMemory*)GetEnvSpace(mmd_id);

	oldcxt = MemoryContextSwitchTo(fsm->MmdContext);
#ifndef LET_OS_MANAGE_FILESIZE
	for (v = &fsm->Md_fdvec[fd]; v != (MdfdVec *) NULL;)
	{
		MdfdVec    *ov = v;

		/* if not closed already */
		if (v->mdfd_vfd >= 0)
		{

			/*
			 * We sync the file descriptor so that we don't need to reopen
			 * it at transaction commit to force changes to disk.  (This
			 * is not really optional, because we are about to forget that
			 * the file even exists...)
			 */
                         /* this is done by close as well 
                        FilePin(v->mdfd_vfd,6);
			FileSync(v->mdfd_vfd);
                        FileUnpin(v->mdfd_vfd,6);
                        */
			FileClose(v->mdfd_vfd);
		}
		/* Now free vector */
		v = v->mdfd_chain;
		if (ov != &fsm->Md_fdvec[fd])
			pfree(ov);
	}

	fsm->Md_fdvec[fd].mdfd_chain = (MdfdVec *) NULL;
#else
	v = &fsm->Md_fdvec[fd];
	if (v != (MdfdVec *) NULL)
	{
		if (v->mdfd_vfd >= 0)
		{

			/*
			 * We sync the file descriptor so that we don't need to reopen
			 * it at transaction commit to force changes to disk.  (This
			 * is not really optional, because we are about to forget that
			 * the file even exists...)
			 */
                         /* this is done by close as well  
                        FilePin(v->mdfd_vfd,6);
			FileSync(v->mdfd_vfd);
                        FileUnpin(v->mdfd_vfd,6);
                        */
			FileClose(v->mdfd_vfd);
		}
	}
#endif
	MemoryContextSwitchTo(oldcxt);

	_fdvec_free(fd);
}

/*
 *	mdread() -- Read the specified block from a relation.
 *
 *		Returns SM_SUCCESS or SM_FAIL.
 */
int
mmdread(Relation reln, BlockNumber blocknum, char *buffer)
{
	long                    seekpos;
	long			nbytes = 0;
        int                     zerot = 0;
	MdfdVec    *v;
	FSMemory*   fsm = (FSMemory*)GetEnvSpace(mmd_id);

	v = _mmdfd_getseg(reln, blocknum, fsm);
	FilePin(v->mdfd_vfd,6);


#ifndef LET_OS_MANAGE_FILESIZE
	seekpos = (long) (BLCKSZ * (blocknum % RELSEG_SIZE));

#ifdef DIAGNOSTIC
	if (seekpos >= BLCKSZ * RELSEG_SIZE) {
		FileUnpin(v->mdfd_vfd,6);		
		elog(FATAL, "seekpos too big!");
	}
#endif
#else
	seekpos = (long) (BLCKSZ * (blocknum));
#endif

	if (FileSeek(v->mdfd_vfd, seekpos, SEEK_SET) != seekpos) {
		FileUnpin(v->mdfd_vfd,6);
		return SM_FAIL;
	}
	
        while (nbytes < BLCKSZ) {
            long r = FileRead(v->mdfd_vfd, buffer, BLCKSZ-nbytes);
            if ( r < 0 ) {
        	FileUnpin(v->mdfd_vfd,6);
                elog(NOTICE,"read error %d rel:%s,db:%u,blk no.:%llu,rel size:%llu",errno,RelationGetRelationName(reln),GetDatabaseId(),blocknum,_internal_mmdnblocks(reln,fsm));
                return SM_FAIL;
            } else if ( r == 0 ) {
                if ( blocknum > _internal_mmdnblocks(reln,fsm) ) {
                    elog(NOTICE,"trying to read non-existant block rel:%s,db:%u,blk no.:%llu,rel size:%llu",RelationGetRelationName(reln),GetDatabaseId(),blocknum,_internal_mmdnblocks(reln,fsm));
                    FileUnpin(v->mdfd_vfd,6);
                    return SM_FAIL;
                }
                if ( zerot++ == 100 ) {
                    elog(NOTICE,"too many zero tries rel:%s,db:%u,blk no.:%llu",RelationGetRelationName(reln),GetDatabaseId(),blocknum);
                    return SM_FAIL;
                }
                elog(DEBUG,"partial read amt:%d,rel:%s,db:%u,blk no.:%llu",r,RelationGetRelationName(reln),GetDatabaseId(),blocknum);
            } else {
                nbytes += r;
                buffer += r;
                if ( r < BLCKSZ ) {
                    elog(NOTICE,"partial read: %d block rel:%s,db:%u,blk no.:%llu",r,RelationGetRelationName(reln),GetDatabaseId(),blocknum);
                }
            }
        }

	FileUnpin(v->mdfd_vfd,6);

	return SM_SUCCESS;
}

/*
 *	mdwrite() -- Write the supplied block at the appropriate location.
 *
 *		Returns SM_SUCCESS or SM_FAIL.
 */
int
mmdwrite(Relation reln, BlockNumber blocknum, char *buffer)
{
	int			status;
	long		seekpos;
	MdfdVec    *v;
	FSMemory*   fsm = (FSMemory*)GetEnvSpace(mmd_id);

	v = _mmdfd_getseg(reln, blocknum, fsm);
	FilePin(v->mdfd_vfd,7);

#ifndef LET_OS_MANAGE_FILESIZE
	seekpos = (long) (BLCKSZ * (blocknum % RELSEG_SIZE));
#ifdef DIAGNOSTIC
	if (seekpos >= BLCKSZ * RELSEG_SIZE) {
		FileUnpin(v->mdfd_vfd,7);
		elog(FATAL, "seekpos too big!");
	}
#endif
#else
	seekpos = (long) (BLCKSZ * (blocknum));
#endif
	
	if (FileSeek(v->mdfd_vfd, seekpos, SEEK_SET) != seekpos) {
		FileUnpin(v->mdfd_vfd,7);
		elog(DEBUG,"bad seek");
		return SM_FAIL;
	}

	status = SM_SUCCESS;
	if (FileWrite(v->mdfd_vfd, buffer, BLCKSZ) != BLCKSZ) {
		elog(DEBUG,"bad write");
		FileUnpin(v->mdfd_vfd,7);
		status = SM_FAIL;
	}

	FileUnpin(v->mdfd_vfd,7);
	return status;
}

/*
 *	mdflush() -- Synchronously write a block to disk.
 *
 *		This is exactly like mdwrite(), but doesn't return until the file
 *		system buffer cache has been flushed.
 */
int
mmdflush(Relation reln, BlockNumber blocknum, char *buffer)
{
	int			status;
	long		seekpos;
	MdfdVec    *v;
	FSMemory*   fsm = (FSMemory*)GetEnvSpace(mmd_id);

	v = _mmdfd_getseg(reln, blocknum, fsm);
	FilePin(v->mdfd_vfd,8);

#ifndef LET_OS_MANAGE_FILESIZE
	seekpos = (long) (BLCKSZ * (blocknum % RELSEG_SIZE));
#ifdef DIAGNOSTIC
	if (seekpos >= BLCKSZ * RELSEG_SIZE) {
		FileUnpin(v->mdfd_vfd,8);
		elog(FATAL, "seekpos too big!");
	}
#endif
#else
	seekpos = (long) (BLCKSZ * (blocknum));
#endif
	if (FileSeek(v->mdfd_vfd, seekpos, SEEK_SET) != seekpos) {
		FileUnpin(v->mdfd_vfd,8);
		return SM_FAIL;
	}

	/* write and sync the block */
	status = SM_SUCCESS;
	if (FileWrite(v->mdfd_vfd, buffer, BLCKSZ) != BLCKSZ
		|| FileSync(v->mdfd_vfd) < 0)
		status = SM_FAIL;

	FileUnpin(v->mdfd_vfd,8);
	return status;
}

/*
 *	mdblindwrt() -- Write a block to disk blind.
 *
 *		We have to be able to do this using only the name and OID of
 *		the database and relation in which the block belongs.  Otherwise
 *		this is much like mdwrite().  If dofsync is TRUE, then we fsync
 *		the file, making it more like mdflush().
 */
int
mmdblindwrt(char *dbname,
		   char *relname,
		   Oid dbid,
		   Oid relid,
		   BlockNumber blkno,
		   char *buffer,
		   bool dofsync)
{
	int			status;
	long		seekpos;
	int			fd;
	FSMemory*   fsm = (FSMemory*)GetEnvSpace(mmd_id);

	fd = _mmdfd_blind_getseg(dbname, relname, dbid, relid, blkno, fsm);

	if (fd < 0)
		return SM_FAIL;

#ifndef LET_OS_MANAGE_FILESIZE
	seekpos = (long) (BLCKSZ * (blkno % RELSEG_SIZE));
#ifdef DIAGNOSTIC
	if (seekpos >= BLCKSZ * RELSEG_SIZE)
		elog(FATAL, "seekpos too big!");
#endif
#else
	seekpos = (long) (BLCKSZ * (blkno));
#endif

	errno = 0;

	if (lseek(fd, seekpos, SEEK_SET) != seekpos)
	{
		elog(DEBUG, "mdblindwrt: lseek(%ld) failed: %m", seekpos);
		close(fd);
		return SM_FAIL;
	}

	status = SM_SUCCESS;

	/* write and optionally sync the block */
	if (write(fd, buffer, BLCKSZ) != BLCKSZ)
	{
		elog(DEBUG, "mdblindwrt: write() failed: %m");
		status = SM_FAIL;
	}
	else if (dofsync &&
			 pg_fsync(fd) < 0)
	{
		elog(DEBUG, "mdblindwrt: fsync() failed: %m");
		status = SM_FAIL;
	}

	if (close(fd) < 0)
	{
		elog(DEBUG, "mdblindwrt: close() failed: %m");
		status = SM_FAIL;
	}

	return status;
}

/*
 *	mdmarkdirty() -- Mark the specified block "dirty" (ie, needs fsync).
 *
 *		Returns SM_SUCCESS or SM_FAIL.
 */
int
mmdmarkdirty(Relation reln, BlockNumber blkno)
{
	MdfdVec    *v;
	FSMemory*   fsm = (FSMemory*)GetEnvSpace(mmd_id);

	v = _mmdfd_getseg(reln, blkno, fsm);

	FileMarkDirty(v->mdfd_vfd);

	return SM_SUCCESS;
}

/*
 *	mdblindmarkdirty() -- Mark the specified block "dirty" (ie, needs fsync).
 *
 *		We have to be able to do this using only the name and OID of
 *		the database and relation in which the block belongs.  Otherwise
 *		this is much like mdmarkdirty().  However, we do the fsync immediately
 *		rather than building md/fd datastructures to postpone it till later.
 */
int
mmdblindmarkdirty(char *dbname,
				 char *relname,
				 Oid dbid,
				 Oid relid,
				 BlockNumber blkno)
{
	int			status;
	int			fd;
	FSMemory*   fsm = (FSMemory*)GetEnvSpace(mmd_id);

	fd = _mmdfd_blind_getseg(dbname, relname, dbid, relid, blkno, fsm);

	if (fd < 0)
		return SM_FAIL;

	status = SM_SUCCESS;

	if (pg_fsync(fd) < 0)
		status = SM_FAIL;

	if (close(fd) < 0)
		status = SM_FAIL;

	return status;
}

/*
 *	mmdnblocks() -- Get the number of blocks stored in a relation.
 *
 *		Important side effect: all segments of the relation are opened
 *		and added to the mdfd_chain list.  If this routine has not been
 *		called, then only segments up to the last one actually touched
 *		are present in the chain...
 *
 *		Returns # of blocks, elog's on error.
 */
int
mmdnblocks(Relation reln)
{
	FSMemory*   fsm = (FSMemory*)GetEnvSpace(mmd_id);	
 	int	blockcount = 0;
 	MdfdVec    *v =	_mmdfd_getseg(reln,0, fsm);
 	FilePin(v->mdfd_vfd,9);
 	blockcount = _internal_mmdnblocks(reln,fsm);
 	FileUnpin(v->mdfd_vfd,9);
 	return blockcount;
 }
 
static int
_internal_mmdnblocks(Relation reln, FSMemory* fsm)
{
	int			fd;
	MdfdVec    *v;
	int			nblocks;

#ifndef LET_OS_MANAGE_FILESIZE
	int			segno;

#endif

	fd = _mmdfd_getrelnfd(reln);
	v = &fsm->Md_fdvec[fd];

#ifndef LET_OS_MANAGE_FILESIZE
	segno = 0;
	for (;;)
	{
		nblocks = _mmdnblocks(v->mdfd_vfd, BLCKSZ);
		if (nblocks > RELSEG_SIZE)
			elog(FATAL, "segment too big in mdnblocks!");
		v->mdfd_lstbcnt = nblocks;
		if (nblocks == RELSEG_SIZE)
		{
			segno++;

			if (v->mdfd_chain == (MdfdVec *) NULL)
			{
				v->mdfd_chain = _mmdfd_openseg(reln, segno, O_CREAT, fsm);
				if (v->mdfd_chain == (MdfdVec *) NULL) {
					elog(NOTICE, "cannot count blocks for %s -- open failed",
						 RelationGetRelationName(reln));
				}
			}

			v = v->mdfd_chain;
		}
		else {
			return (segno * RELSEG_SIZE) + nblocks;
		}
	}
#else
	nblocks = _mmdnblocks(v->mdfd_vfd, BLCKSZ);
	return nblocks;
#endif
}

/*
 *	mdtruncate() -- Truncate relation to specified number of blocks.
 *
 *		Returns # of blocks or -1 on error.
 */
int
mmdtruncate(Relation reln, int nblocks)
{
	int			curnblk;
	int			fd;
	MdfdVec    *v;
	FSMemory*   fsm = (FSMemory*)GetEnvSpace(mmd_id);

#ifndef LET_OS_MANAGE_FILESIZE
	MemoryContext oldcxt;
	int			priorblocks;

#endif

	/*
	 * NOTE: mdnblocks makes sure we have opened all existing segments, so
	 * that truncate/delete loop will get them all!
	 */
	fd = _mmdfd_getrelnfd(reln);
	v = &fsm->Md_fdvec[fd];
	FilePin(v->mdfd_vfd,10);

	curnblk = _internal_mmdnblocks(reln,fsm);
	if (nblocks < 0 || nblocks > curnblk) {
		FileUnpin(v->mdfd_vfd,10);
		return -1;	
	}				/* bogus request */
	if (nblocks == curnblk) {
		FileUnpin(v->mdfd_vfd,10);
		return nblocks;	
	}					/* no work */
	FileUnpin(v->mdfd_vfd,10);

#ifndef LET_OS_MANAGE_FILESIZE
	oldcxt = MemoryContextSwitchTo(fsm->MmdContext);
	priorblocks = 0;
	while (v != (MdfdVec *) NULL)
	{
		MdfdVec    *ov = v;

		FilePin(v->mdfd_vfd,11);
		if (priorblocks > nblocks)
		{

			/*
			 * This segment is no longer wanted at all (and has already
			 * been unlinked from the mdfd_chain). We truncate the file
			 * before deleting it because if other backends are holding
			 * the file open, the unlink will fail on some platforms.
			 * Better a zero-size file gets left around than a big file...
			 */
			FileTruncate(v->mdfd_vfd, 0);
			FileUnpin(v->mdfd_vfd,11);
			FileUnlink(v->mdfd_vfd);
			v = v->mdfd_chain;
			Assert(ov != &fsm->Md_fdvec[fd]);		/* we never drop the 1st
												 * segment */
			pfree(ov);
		}
		else if (priorblocks + RELSEG_SIZE > nblocks)
		{

			/*
			 * This is the last segment we want to keep. Truncate the file
			 * to the right length, and clear chain link that points to
			 * any remaining segments (which we shall zap). NOTE: if
			 * nblocks is exactly a multiple K of RELSEG_SIZE, we will
			 * truncate the K+1st segment to 0 length but keep it. This is
			 * mainly so that the right thing happens if nblocks=0.
			 */
			int			lastsegblocks = nblocks - priorblocks;
			
			if (FileTruncate(v->mdfd_vfd, lastsegblocks * BLCKSZ) < 0) {
				FileUnpin(v->mdfd_vfd,11);
				return -1;
			}

			ov->mdfd_chain = (MdfdVec *) NULL;
			v->mdfd_lstbcnt = lastsegblocks;
			
			FileUnpin(v->mdfd_vfd,11);
			v = v->mdfd_chain;

		}
		else
		{
			FileUnpin(v->mdfd_vfd,11);
			/*
			 * We still need this segment and 0 or more blocks beyond it,
			 * so nothing to do here.
			 */
			v = v->mdfd_chain;
		}
		priorblocks += RELSEG_SIZE;
	}
	MemoryContextSwitchTo(oldcxt);
#else
	FilePin(v->mdfd_vfd,12);
	if (FileTruncate(v->mdfd_vfd, nblocks * BLCKSZ) < 0) {
		FileUnpin(v->mdfd_vfd,12);
		return -1;
	}
	v->mdfd_lstbcnt = nblocks;
	FileUnpin(v->mdfd_vfd,12);
#endif
	return nblocks;

}	/* mdtruncate */

/*
 *	mdcommit() -- Commit a transaction.
 *
 *		All changes to magnetic disk relations must be forced to stable
 *		storage.  This routine makes a pass over the private table of
 *		file descriptors.  Any descriptors to which we have done writes,
 *		but not synced, are synced here.
 *
 *		Returns SM_SUCCESS or SM_FAIL with errno set as appropriate.
 */
int
mmdcommit()
{
	FSMemory*   fsm = (FSMemory*)GetEnvSpace(mmd_id);
	int			i;
	MdfdVec    *v;

	for (i = 0; i < fsm->CurFd; i++)
	{
		v = &fsm->Md_fdvec[i];
		if (v->mdfd_flags & MDFD_FREE)
			continue;
		/* Sync the file entry */
#ifndef LET_OS_MANAGE_FILESIZE
		for (; v != (MdfdVec *) NULL; v = v->mdfd_chain)
#else
		if (v != (MdfdVec *) NULL)
#endif
		{
                        bool  failed = false;
                        FilePin(v->mdfd_vfd,13);
			if (FileSync(v->mdfd_vfd) < 0)
				failed = true;
                        FileUnpin(v->mdfd_vfd,13);
                        if ( failed ) return SM_FAIL;
                         
		}
	}

	return SM_SUCCESS;
}

/*
 *	mdabort() -- Abort a transaction.
 *
 *		Changes need not be forced to disk at transaction abort.  We mark
 *		all file descriptors as clean here.  Always returns SM_SUCCESS.
 */
int
mmdabort()
{
	
	/*
	 * We don't actually have to do anything here.  fd.c will discard
	 * fsync-needed bits in its AtEOXact_Files() routine.
	 */
	return SM_SUCCESS;
}

/*
 *	_fdvec_alloc () -- grab a free (or new) md file descriptor vector.
 *
 */
static
int
_fdvec_alloc()
{
	FSMemory*   fsm = (FSMemory*)GetEnvSpace(mmd_id);
	MdfdVec    *nvec;
	int			fdvec,
				i;
	MemoryContext oldcxt;
	
	
	if (fsm->Md_Free >= 0)			/* get from free list */
	{
		fdvec = fsm->Md_Free;
		fsm->Md_Free =fsm->Md_fdvec[fdvec].mdfd_nextFree;
		Assert(fsm->Md_fdvec[fdvec].mdfd_flags == MDFD_FREE);
		fsm->Md_fdvec[fdvec].mdfd_flags = 0;
		if (fdvec >= fsm->CurFd)
		{
			Assert(fdvec == fsm->CurFd);
			fsm->CurFd++;
		}
		return fdvec;
	}

	/* Must allocate more room */

	if (fsm->Nfds != fsm->CurFd)
		elog(FATAL, "_fdvec_alloc error");

	fsm->Nfds *= 2;

	oldcxt = MemoryContextSwitchTo(fsm->MmdContext);

	nvec = (MdfdVec *) palloc(fsm->Nfds * sizeof(MdfdVec));
	MemSet(nvec, 0, fsm->Nfds * sizeof(MdfdVec));
	memmove(nvec, (char *) fsm->Md_fdvec, fsm->CurFd * sizeof(MdfdVec));
	pfree(fsm->Md_fdvec);

	MemoryContextSwitchTo(oldcxt);

	fsm->Md_fdvec = nvec;

	/* Set new free list */
	for (i = fsm->CurFd; i < fsm->Nfds; i++)
	{
		fsm->Md_fdvec[i].mdfd_nextFree = i + 1;
		fsm->Md_fdvec[i].mdfd_flags = MDFD_FREE;
	}
	fsm->Md_fdvec[fsm->Nfds - 1].mdfd_nextFree = -1;
	fsm->Md_Free = fsm->CurFd + 1;

	fdvec = fsm->CurFd;
	fsm->CurFd++;
	fsm->Md_fdvec[fdvec].mdfd_flags = 0;
	
	return fdvec;
}

/*
 *	_fdvec_free () -- free md file descriptor vector.
 *
 */
static
void
_fdvec_free(int fdvec)
{

	FSMemory*   fsm = (FSMemory*)GetEnvSpace(mmd_id);
	Assert(fsm->Md_Free < 0 || fsm->Md_fdvec[Md_Free].mdfd_flags == MDFD_FREE);
	Assert(fsm->Md_fdvec[fsm->fdvec].mdfd_flags != MDFD_FREE);
	fsm->Md_fdvec[fdvec].mdfd_nextFree = fsm->Md_Free;
	fsm->Md_fdvec[fdvec].mdfd_flags = MDFD_FREE;
	fsm->Md_Free = fdvec;
}

static MdfdVec *
_mmdfd_openseg(Relation reln, int segno, int oflags, FSMemory* fsm)
{
	MemoryContext oldcxt = MemoryContextSwitchTo(fsm->MmdContext);
	MdfdVec    *v;
	int			fd;
	char	   *path,
			   *fullpath;

	path = relpath(reln);  

	if (segno > 0)
	{
		fullpath = (char *) palloc(strlen(path) + 12);
		sprintf(fullpath, "%s.%d", path, segno);
		pfree(path);
	}
	else
		fullpath = path;

	/* open the file */
#ifndef __CYGWIN32__
	fd = FileNameOpenFile(fullpath, O_RDWR | oflags, 0600);
#else
	fd = FileNameOpenFile(fullpath, O_RDWR | O_BINARY | oflags, 0600);
#endif

	pfree(fullpath);
	MemoryContextSwitchTo(oldcxt);

	if (fd < 0) {
		return (MdfdVec *) NULL;
	}

	/* allocate an mdfdvec entry for it */
	oldcxt = MemoryContextSwitchTo(fsm->MmdContext);
	v = (MdfdVec *) palloc(sizeof(MdfdVec));
	MemoryContextSwitchTo(oldcxt);

	/* fill the entry */
	v->mdfd_vfd = fd;
	v->mdfd_flags = (uint16) 0;

	v->mdfd_lstbcnt = _mmdnblocks(fd, BLCKSZ);

#ifndef LET_OS_MANAGE_FILESIZE
	v->mdfd_chain = (MdfdVec *) NULL;

#ifdef DIAGNOSTIC
	if (v->mdfd_lstbcnt > RELSEG_SIZE)
		elog(FATAL, "segment too big on open!");
#endif
#endif

	/* all done */
	return v;
}

/* Get the fd for the relation, opening it if it's not already open */

static int
_mmdfd_getrelnfd(Relation reln)
{
	int			fd;

	fd = RelationGetFile(reln);
	if (fd < 0)
	{
		if ((fd = mmdopen(reln)) < 0) {
			elog(NOTICE, "cannot open relation %s",
				 RelationGetRelationName(reln));
			fd = SM_FAIL;
		} else {
			reln->rd_fd = fd;
		}
	}
	return fd;
}

/* Find the segment of the relation holding the specified block */

static MdfdVec *
_mmdfd_getseg(Relation reln, BlockNumber blkno, FSMemory*   fsm)
{
	MdfdVec    *v;
	int			segno;
	int			fd;
	int			i;

	fd = _mmdfd_getrelnfd(reln);

#ifndef LET_OS_MANAGE_FILESIZE
	for (v = &fsm->Md_fdvec[fd], segno = blkno / RELSEG_SIZE, i = 1;
		 segno > 0;
		 i++, segno--)
	{

		if (v->mdfd_chain == (MdfdVec *) NULL)
		{
			v->mdfd_chain = _mmdfd_openseg(reln, i, O_CREAT, fsm);

			if (v->mdfd_chain == (MdfdVec *) NULL) 
				elog(NOTICE, "cannot open segment %d of relation %s",
					 i, RelationGetRelationName(reln));
		}
		v = v->mdfd_chain;
	}
#else
	v = &fsm->Md_fdvec[fd];
#endif

	return v;
}

/*
 * Find the segment of the relation holding the specified block.
 *
 * This performs the same work as _mdfd_getseg() except that we must work
 * "blind" with no Relation struct.  We assume that we are not likely to
 * touch the same relation again soon, so we do not create an FD entry for
 * the relation --- we just open a kernel file descriptor which will be
 * used and promptly closed.  The return value is the kernel descriptor,
 * or -1 on failure.
 */

static int
_mmdfd_blind_getseg(char *dbname, char *relname, Oid dbid, Oid relid,
				   BlockNumber blkno, FSMemory*   fsm)
{
	char	   *path;
	int			fd;
	MemoryContext	old = MemoryContextSwitchTo(fsm->MmdContext);

#ifndef LET_OS_MANAGE_FILESIZE
	int			segno;

#endif

	/* construct the path to the relation */
	path = relpath_blind(dbname, relname, dbid, relid);
	elog(DEBUG,"blind path %s",path);
#ifndef LET_OS_MANAGE_FILESIZE
	/* append the '.segno', if needed */
	segno = blkno / RELSEG_SIZE;
	if (segno > 0)
	{
		char	   *segpath = (char *) palloc(strlen(path) + 12);

		sprintf(segpath, "%s.%d", path, segno);
		pfree(path);
		path = segpath;
	}
#endif

#ifndef __CYGWIN32__
	fd = open(path, O_RDWR, 0600);
#else
	fd = open(path, O_RDWR | O_BINARY, 0600);
#endif

	if (fd < 0)
		elog(DEBUG, "_mmdfd_blind_getseg: couldn't open %s: %m", path);

	pfree(path);
	MemoryContextSwitchTo(old);

	return fd;
}

static BlockNumber
_mmdnblocks(File file, Size blcksz)
{
	long		len;
	
	len = FileSeek(file, 0L, SEEK_END);
	if (len < 0)
		return 0;				/* on failure, assume file is empty */
	return (BlockNumber) (len / blcksz);
}
