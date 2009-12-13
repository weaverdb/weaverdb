/*-------------------------------------------------------------------------
 *
 * vfd.c
 *	  This code manages relations that reside on magnetic disk.
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *  Logging code is NOT THREAD SAFE.  Make sure that only one thread is logging
 *  on the system
 *
 * IDENTIFICATION
 *	  $Header: /cvs/weaver/mtpgsql/src/backend/storage/smgr/vfd.c,v 1.4 2007/05/23 15:39:23 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */
#include <errno.h>
#include <unistd.h>
#include <thread.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/varargs.h>

#include "postgres.h"

#include "catalog/catalog.h"
#include "miscadmin.h"
#include "storage/smgr.h"
#include "storage/smgr_spi.h"
#include "utils/relcache.h"
#include "env/env.h"
#include "env/connectionutil.h"

#undef DIAGNOSTIC

static File     log_file;
static File     index_file;
static long     log_count;
static long     log_pos;

#define HEADER_MAGIC  0xCAFE08072006BABE
#define SEGMENT_MAGIC  0xABCDEF0123456789
#define INDEX_MAGIC  0x9876543210FEDCBA


static union  logbuffer {
    struct {
        long    header_magic;
        long    log_id;
        long    segments;
        bool    completed;
        pthread_t   owner;
    } LogHeader;
    char        block[BLCKSZ];
} LogBuffer;

typedef struct indexsegment {
    long        index_magic;
    int         count;
    SmgrData    blocks[1];
} IndexSegment;

static union indexstore {
    IndexSegment    header;
    char            data[BLCKSZ];
} IndexStore;

typedef struct logsegment {
    long        segment_magic;
    long        seg_id;
    short         count;
    SmgrData    blocks[1];
} LogSegment;

static union segmentstore {
    LogSegment    header;
    char          data[BLCKSZ];
} SegmentStore;

static int max_blocks;
static int max_index;

/*  block cache space, created in the init script  */
static char*        index_log;
static int          index_count;
static char*        scratch_space;
static int          scratch_size;

/* routines declared here */
static BlockNumber _vfdnblocks(File file, Size blcksz);
static int _vfddumplogtodisk(void);
static int _vfddumpindextomemory(void);
static int _vfddumpindextodisk();
static long _vfdreplaysegment(void);
static int _vfdreplayindexlog();

static void  vfd_log(char* pattern, ...);

int
vfdinit()
{    
      char path[256];
      char idxpath[256];
      char* logfile_path = GetProperty("vfdlogfile");
      char* index_path = GetProperty("vfdindexlog");
      char* datadir = DataDir;
      int count;
      
        max_index = ((sizeof(IndexStore) - MAXALIGN((char*)&IndexStore - (char*)&IndexStore.header.blocks)) / sizeof(SmgrData));
        max_blocks = ((sizeof(SegmentStore) - MAXALIGN((char*)&SegmentStore - (char*)&SegmentStore.header.blocks)) / sizeof(SmgrData));
        log_count = 0;

      if ( datadir == NULL ) datadir = getenv("PGDATA");
      
      if ( logfile_path == NULL ) {
        logfile_path = "pg_shadowlog";
      }
      
      if ( index_path == NULL ) {
        index_path = "pg_indexlog";
      }     
      
    if ( *logfile_path == SEP_CHAR ) {
        sprintf(path,"%s",logfile_path);
    } else {
        sprintf(path,"%s%c%s",datadir,SEP_CHAR,logfile_path);
    }
      
    if ( *index_path == SEP_CHAR ) {
        sprintf(idxpath,"%s",index_path);
    } else {
        sprintf(idxpath,"%s%c%s",datadir,SEP_CHAR,index_path);
    }
      
    log_file = PathNameOpenFile(path, O_RDWR | O_CREAT , 0600);
     if ( log_file < 0 ) {
        elog(FATAL,"unable to access vfd logfile");
    }
    FileOptimize(log_file);
    FilePin(log_file,0);
    FileSeek(log_file,0,SEEK_SET);
    
    index_file = PathNameOpenFile(idxpath, O_RDWR | O_CREAT , 0600);
     if ( index_file < 0 ) {
        elog(FATAL,"unable to access vfd logfile");
    }
    FileOptimize(index_file);
    FilePin(index_file,0);
    FileSeek(index_file,0,SEEK_SET);
    
    scratch_size = (BLCKSZ * max_blocks);
    scratch_space = os_malloc(scratch_size);
        
    count = FileRead(log_file,LogBuffer.block, BLCKSZ);
    if ( count == BLCKSZ ) {
        if ( LogBuffer.LogHeader.header_magic != HEADER_MAGIC ) {
            elog(FATAL,"vfd logfile is invalid");
        }
        log_count = LogBuffer.LogHeader.log_id;
    } else {
        log_count = 0;
    }
    
    log_pos = 0;
    
    count = FileRead(index_file,IndexStore.data, BLCKSZ);
    if ( count == BLCKSZ ) {
        if ( IndexStore.header.index_magic != INDEX_MAGIC ) {
            elog(FATAL,"vfd index logfile is invalid");
        }
    }   
 
    index_log = NULL;
    index_count = 0;

    FileUnpin(log_file,0);
    FileUnpin(index_file,0);
    
    return SM_SUCCESS;
}

int
vfdshutdown()
{
    FilePin(log_file,0);
    FilePin(index_file,0);
    if ( log_file > 0 ) {
        LogBuffer.LogHeader.header_magic = HEADER_MAGIC;
        LogBuffer.LogHeader.log_id = log_count;
        LogBuffer.LogHeader.completed = false;
        LogBuffer.LogHeader.segments = 0;  

        log_pos = FileSeek(log_file,0,SEEK_END);

        FileWrite(log_file,LogBuffer.block,BLCKSZ);
        FileClose(log_file);
    }
    
    if ( index_file > 0 ) {
        IndexStore.header.index_magic = INDEX_MAGIC;
        IndexStore.header.count = 0;  

        FileSeek(index_file,0,SEEK_END);

        FileWrite(index_file,IndexStore.data,BLCKSZ);
        FileClose(index_file);
    }    
    os_free(scratch_space);
    FileUnpin(log_file,0); 
    FileUnpin(index_file,0); 
    return SM_SUCCESS;
}

int
vfdcreate(SmgrInfo info)
{
	int         fd;
	char	   *path;

	path = relpath_blind(NameStr(info->dbname),NameStr(info->relname),info->dbid,info->relid);

	fd = FileNameOpenFile(path, O_RDWR | O_CREAT | O_EXCL | O_LARGEFILE, 0600);

	if (fd < 0)
	{
		fd = FileNameOpenFile(path, O_RDWR | O_LARGEFILE, 0600);

		if (fd < 0) return -1;
		if (!IsBootstrapProcessingMode())
		{
			bool	reuse = false;
                        int     len;

                        FilePin(fd,9);
			len = FileSeek(fd, 0L, SEEK_END);
                        FileUnpin(fd,9);

			if (len == 0)
				reuse = true;
                        /*
			else if (info->kind == RELKIND_INDEX &&
					 len == BLCKSZ * 2)
				reuse = true;
                        */
			if (!reuse)
			{
				FileClose(fd);
				return -1;
			}
		}
	}
	info->unlinked = false;
        info->fd = fd;

	pfree(path);

	return fd;
}


int
vfdunlink(SmgrInfo info)
{
	int			fd;

	/*
	 * If the relation is already unlinked,we have nothing to do any more.
	 */
	if (info->unlinked && info->fd < 0)
		return SM_SUCCESS;

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
	fd = info->fd;
	Assert(fd >= 0);				/* should not happen */

        FileBaseSync(fd,0);
        
        FilePin(fd, 0);
	FileTruncate(fd, 0);
        FileUnpin(fd, 0);

	FileUnlink(fd);

	/* be sure to mark relation closed && unlinked */
	info->fd = -1;
	info->unlinked = true;

	return SM_SUCCESS;
}

/*
 *	vfdextend() -- Add a block to the specified relation.
 *
 *		This routine returns SM_FAIL or SM_SUCCESS, with errno set as
 *		appropriate.
 */
int
vfdextend(SmgrInfo info, char *buffer, int count)
{
	off_t		pos;
        int             run;
        File                    fd;

	fd = info->fd;

        FilePin(fd, 1);

	pos = FileSeek(fd, 0L, SEEK_END);

        if ( pos < 0 ) {
            FileUnpin(fd, 1);
            return SM_FAIL;
        }

	if (pos % BLCKSZ != 0)		/* the last block is incomplete */
	{
            pos -= pos % BLCKSZ;
            if (FileSeek(fd, pos, SEEK_SET) < 0) {
                FileUnpin(fd, 1);
                    return SM_FAIL;
            }
	}

	for ( run=0;run < count;run++ ) {
            int nbytes = FileWrite(fd, buffer, BLCKSZ);
            if (nbytes != BLCKSZ)
            {
                elog(NOTICE,"file extend failed %d does not equal block size",nbytes);
                if (nbytes > 0)
                {
                    FileTruncate(fd, pos);
                    FileSeek(fd, pos, SEEK_SET);
                }
                FileUnpin(fd, 1);
                FileBaseSync(fd,pos);
                return SM_FAIL;
            }
        }
        
        FileUnpin(fd, 1);
        
        info->nblocks = (BlockNumber)((pos / BLCKSZ) + count);

	return SM_SUCCESS;
}

/*
 *	vfdopen() -- Open the specified relation.
 */
int
vfdopen(SmgrInfo info)
{
	char	   *path;
	int			fd;

        path = relpath_blind(NameStr(info->dbname),NameStr(info->relname),info->dbid,info->relid);

	fd = FileNameOpenFile(path, O_RDWR | O_LARGEFILE, 0600);

	if (fd < 0)
	{
		if (IsBootstrapProcessingMode())
		{
			fd = FileNameOpenFile(path, O_RDWR | O_CREAT | O_EXCL | O_LARGEFILE, 0600);
		}
		if (fd < 0)
		{
			elog(NOTICE, "vfdopen: couldn't open %s: %m", path);
			/* mark relation closed and unlinked */
			info->fd = -1;
			info->unlinked = true;
                        pfree(path);
			return SM_FAIL;
		}
	}
	info->unlinked = false;
        info->fd = fd;

	pfree(path);

	return fd;
}

/*
 *	vfdclose() -- Close the specified relation, if it isn't closed already.
 *
 *		AND FREE fd vector! It may be re-used for other relation!
 *		reln should be flushed from cache after closing !..
 *
 *		Returns SM_SUCCESS or SM_FAIL with errno set as appropriate.
 */
int
vfdclose(SmgrInfo info)
{
	int			fd;

	fd = info->fd;
	if (fd < 0)
		return SM_SUCCESS;		/* already closed, so no work */

         
	FileClose(fd);

	info->fd = -1;

	return SM_SUCCESS;
}


/*
 *	vfdread() -- Read the specified block from a relation.
 *
 *		Returns SM_SUCCESS or SM_FAIL.
 */
int
vfdread(SmgrInfo info, BlockNumber blocknum, char *buffer)
{
	long		seekpos;
	int		blit = 0;
        File            fd = info->fd;
        int status =  SM_SUCCESS;
	char*		msg = NULL;


	seekpos = (long) (BLCKSZ * (blocknum));

        if ( fd < 0 ) {
            elog(NOTICE,"File not valid");
            return SM_FAIL;
        }

        FilePin(fd, 3);
	if (FileSeek(fd, seekpos, SEEK_SET) != seekpos) {
            elog(NOTICE,"bad read seek filename:%s, %d db:%s,rel:%s,blk no.:%llu",FileGetName(fd),seekpos,NameStr(info->dbname),NameStr(info->relname),blocknum);
            status = SM_FAIL_SEEK;
        } else {
            blit = FileRead(fd, buffer, BLCKSZ);
            if (blit < 0) {
                elog(NOTICE,"bad read %d filename:%s, db:%s,rel:%s,blk no.:%llu",errno,FileGetName(fd),NameStr(info->dbname),NameStr(info->relname),blocknum);
                status = SM_FAIL_BASE;
            } else if ( blit == 0 ) {
                long checkpos = FileSeek(fd,0L,SEEK_END);
                if ( seekpos >= checkpos ) {
                    if ( seekpos > checkpos ) {
                        elog(NOTICE,"read past end of file filename: %s, rel: %s %ld %ld",FileGetName(fd), NameStr(info->relname),seekpos,checkpos);
                    }
                    MemSet(buffer, 0, BLCKSZ);
                } else {
                    if (FileSeek(fd, seekpos, SEEK_SET) != seekpos) {
                        status = SM_FAIL_SEEK;
                    }
                }
            }
        }

        FileUnpin(fd, 3);
	return status;
}

/*
 *	vfdwrite() -- Write the supplied block at the appropriate location.
 *
 *		Returns SM_SUCCESS or SM_FAIL.
 */
int
vfdwrite(SmgrInfo info, BlockNumber blocknum, char *buffer)
{
	int			status;
	long		seekpos;
        File            fd = info->fd;

	seekpos = (long) (BLCKSZ * (blocknum));

        FilePin(fd, 4);
	if (FileSeek(fd, seekpos, SEEK_SET) != seekpos) {
                FileUnpin(fd, 4);
		return SM_FAIL;
        }

	status = SM_SUCCESS;
	if (FileWrite(fd, buffer, BLCKSZ) != BLCKSZ)
		status = SM_FAIL;

        FileUnpin(fd, 4);
	return status;
}

/*
 *	vfdflush() -- Synchronously write a block to disk.
 *
 *		This is exactly like mdwrite(), but doesn't return until the file
 *		system buffer cache has been flushed.
 */
int
vfdflush(SmgrInfo info, BlockNumber blocknum, char *buffer)
{
	int			status;
	long		seekpos;
        File            fd = info->fd;


	seekpos = (long) (BLCKSZ * (blocknum));

        FilePin(fd, 5);
	if (FileSeek(fd, seekpos, SEEK_SET) != seekpos) {
            FileUnpin(fd, 5);
		return SM_FAIL;
        }

	/* write and sync the block */
	status = SM_SUCCESS;
	if (FileWrite(fd, buffer, BLCKSZ) != BLCKSZ
		|| FileSync(fd) < 0)
		status = SM_FAIL;
            
        FileUnpin(fd, 5);
	return status;
}

/*
 *	vfdmarkdirty() -- Mark the specified block "dirty" (ie, needs fsync).
 *
 *		Returns SM_SUCCESS or SM_FAIL.
 */
int
vfdmarkdirty(SmgrInfo info, BlockNumber blkno)
{
	File fd = info->fd;

	FileMarkDirty(fd);

	return SM_SUCCESS;
}

/*
 *	vfdnblocks() -- Get the number of blocks stored in a relation.
 *
 *		Important side effect: all segments of the relation are opened
 *		and added to the mdfd_chain list.  If this routine has not been
 *		called, then only segments up to the last one actually touched
 *		are present in the chain...
 *
 *		Returns # of blocks, elog's on error.
 */
int
vfdnblocks(SmgrInfo info)
{
	File        fd = info->fd;
        BlockNumber         count = InvalidBlockNumber;
        FilePin(fd, 6);
	count = _vfdnblocks(fd, BLCKSZ);
        info->nblocks = count;
        FileUnpin(fd, 6);
        return SM_SUCCESS;
}


/*
 *	vfdnblocks() -- Get the number of blocks stored in a relation.
 *
 *		Important side effect: all segments of the relation are opened
 *		and added to the mdfd_chain list.  If this routine has not been
 *		called, then only segments up to the last one actually touched
 *		are present in the chain...
 *
 *		Returns # of blocks, elog's on error.
 */
int
vfdsync(SmgrInfo info)
{
	File        fd = info->fd;

        FilePin(fd, 6);
	FileSync(fd);
        FileUnpin(fd, 6);
        
        return SM_SUCCESS;
}

/*
 *	vfdtruncate() -- Truncate relation to specified number of blocks.
 *
 *		Returns # of blocks or -1 on error.
 */
int
vfdtruncate(SmgrInfo info, long nblocks)
{
	BlockNumber			curnblk;
        File        fd = info->fd;

        FileBaseSync(fd,nblocks * BLCKSZ);
        
        FilePin(fd, 7);
        
	curnblk = _vfdnblocks(fd, BLCKSZ);
	if (nblocks < 0 || nblocks > curnblk) {
            FileUnpin(fd, 7);
		return -1;				/* bogus request */
        }
	if (nblocks == curnblk) {
            
            FileUnpin(fd, 7);
            return SM_SUCCESS;			/* no work */
        }


	if (FileTruncate(fd, nblocks * BLCKSZ) < 0) {
                FileUnpin(fd, 7);
		return -1;
        }
        
        info->nblocks = nblocks;

        FileUnpin(fd, 7);

	return SM_SUCCESS;

}	/* mdtruncate */

/*
 *	vfdcommit() -- Commit a transaction.
 *
 *		All changes to magnetic disk relations must be forced to stable
 *		storage.  This routine makes a pass over the private table of
 *		file descriptors.  Any descriptors to which we have done writes,
 *		but not synced, are synced here.
 *
 *		Returns SM_SUCCESS or SM_FAIL with errno set as appropriate.
 */
int
vfdcommit()
{
	return SM_SUCCESS;
}

/*
 *	mdabort() -- Abort a transaction.
 *
 *		Changes need not be forced to disk at transaction abort.  We mark
 *		all file descriptors as clean here.  Always returns SM_SUCCESS.
 */
int
vfdabort()
{

	/*
	 * We don't actually have to do anything here.  fd.c will discard
	 * fsync-needed bits in its AtEOXact_Files() routine.
	 */
	return SM_SUCCESS;
}


static BlockNumber
_vfdnblocks(File file, Size blcksz)
{
	long		len;

	len = FileSeek(file, 0L, SEEK_END);
        
	if (len < 0) {
            perror("FileSeek:");
            return 0;	/* on failure, assume file is empty */
        }
        
	return (BlockNumber) (len / blcksz);
}


int
vfdbeginlog() {        
    LogBuffer.LogHeader.header_magic = HEADER_MAGIC;
    LogBuffer.LogHeader.log_id = log_count++;
    LogBuffer.LogHeader.completed = false;
    LogBuffer.LogHeader.segments = 0;  
    FilePin(log_file,0); 
    log_pos = FileSeek(log_file,0,SEEK_END);
        
    FileWrite(log_file,LogBuffer.block,BLCKSZ);
    FileSync(log_file);
    
    SegmentStore.header.count = 0;
    IndexStore.header.count = 0;
    FileUnpin(log_file,0); 
    return SM_SUCCESS;
}

int
vfdlog(SmgrInfo info,BlockNumber block, char* buffer) {
        
    if ( SegmentStore.header.count == max_blocks ) {
        _vfddumplogtodisk();
    }
    
    info->nblocks = block;
    memmove(SegmentStore.header.blocks + SegmentStore.header.count,info,sizeof(SmgrData));
    memmove(scratch_space + (BLCKSZ * SegmentStore.header.count),buffer,BLCKSZ);
    SegmentStore.header.count += 1;
    
    if ( info->relkind == RELKIND_INDEX ) {
        if ( IndexStore.header.count + 1 >= max_index ) {
            _vfddumpindextomemory();
        }
        memmove(&IndexStore.header.blocks[IndexStore.header.count++],info,sizeof(SmgrData));
    }
    
    return SM_SUCCESS;    
}

int
_vfddumpindextomemory() {
    int ret = 0;
    
    if ( IndexStore.header.count == 0 ) return 0;
    IndexStore.header.index_magic = INDEX_MAGIC;
    if ( !index_log ) {
        index_log = os_malloc(BLCKSZ);
    } else {
        index_log = os_realloc(index_log,BLCKSZ * (index_count + 1));
    }
    memcpy(index_log + (BLCKSZ * index_count),IndexStore.data,BLCKSZ);
    index_count += 1;
    IndexStore.header.count = 0;
    
    return IndexStore.header.count;
}

int
_vfddumpindextodisk() {
    int ret = 0;
    FilePin(index_file,0);
    FileSeek(index_file,0,SEEK_SET); 
    if ( index_log ) {
        FileWrite(index_file,index_log,BLCKSZ * index_count);
        os_free(index_log);
        index_log = NULL;
        ret = index_count;
        index_count = 0;
    }
    
    if ( IndexStore.header.count != 0 ) {
        IndexStore.header.index_magic = INDEX_MAGIC;
        FileWrite(index_file,IndexStore.data,BLCKSZ);
        ret += 1;
        IndexStore.header.count = 0;
    }
    FileTruncate(index_file,ret * BLCKSZ);
    FileUnpin(index_file,0); 
    return ret;
}
    
int
_vfddumplogtodisk() {
    int ret = 0;
    
    if ( SegmentStore.header.count == 0 ) return 0;
    
    SegmentStore.header.segment_magic = SEGMENT_MAGIC;
    SegmentStore.header.seg_id = LogBuffer.LogHeader.segments++;
    FilePin(log_file,0);
    ret += FileWrite(log_file,SegmentStore.data,BLCKSZ);
    ret += FileWrite(log_file,scratch_space,BLCKSZ * SegmentStore.header.count);
    SegmentStore.header.count = 0;
    FileUnpin(log_file,0); 
    return SegmentStore.header.count;
}

int
vfdcommitlog() {
    
    _vfddumplogtodisk();
    FilePin(log_file,0);
    FileSync(log_file);
    
    LogBuffer.LogHeader.completed = true;
    FileSeek(log_file,log_pos,SEEK_SET);
    FileWrite(log_file,LogBuffer.block,BLCKSZ);
    FileSync(log_file);
    FileUnpin(log_file,0); 
    return SM_SUCCESS;
}

int
vfdexpirelogs() {    
    _vfddumpindextodisk();

    if ( index_log ) {
       os_free(index_log);
       index_log = NULL;
       index_count = 0;
    }
    IndexStore.header.count = 0;
    FilePin(log_file,0);
    FileTruncate(log_file,0);
    FileSeek(log_file,0,SEEK_SET);
    FileSync(log_file);
    FileUnpin(log_file,0); 
    log_pos = 0;
    return SM_SUCCESS;
}

int
vfdreplaylogs() {
    int      count;
    long read = 0;
    long total = 0;
    long end = 0;
    long id = 0;
    int result = SM_SUCCESS;
    bool logged = false;
    
    vfd_log("--- Replaying VFD storage manager log ---");
   	 
    if ( log_file < 0 ) {
        vfd_log("Log File not valid. exiting.");
        return SM_SUCCESS;
    }
    FilePin(log_file,0);
    end = FileSeek(log_file,0,SEEK_END);
    FileSeek(log_file,0,SEEK_SET);
    
    while (total < end) {
        read = FileRead(log_file,LogBuffer.block,BLCKSZ);

        if ( read != BLCKSZ ) {
            vfd_log("Log File not valid. exiting.");
            result = SM_FAIL;
            break;
        }
        total += read;
        if ( LogBuffer.LogHeader.header_magic != HEADER_MAGIC ) {
            vfd_log("VFD Log ID: %d invalid log file. exiting.",LogBuffer.LogHeader.log_id);
            result = SM_FAIL;
            break;
        }
        if ( !LogBuffer.LogHeader.completed ) {
            vfd_log("VFD Log ID: %d not completed. exiting.",LogBuffer.LogHeader.log_id);
            break;
        }
        if ( id != 0 && id+1 != LogBuffer.LogHeader.log_id ) {
            vfd_log("VFD Log ID: %d out of sequence. exiting.",LogBuffer.LogHeader.log_id);
            break;
        } else {
            id =  LogBuffer.LogHeader.log_id;
        }
        vfd_log("VFD Log ID: %d, complete: %s, segments: %d",LogBuffer.LogHeader.log_id,
            (LogBuffer.LogHeader.completed) ? "true":"false",LogBuffer.LogHeader.segments);   

        for ( count=0;count<LogBuffer.LogHeader.segments;count++) {
            long add =  _vfdreplaysegment();
            if ( add < 0 ) {
                vfd_log("exiting due to invalid segment");
                result = SM_FAIL;
                break;
            }
            total += add;
        }
/* there are valid logs, no need to replay index  */
        logged = true; 
    }
    log_count = LogBuffer.LogHeader.log_id + 1;
    FileUnpin(log_file,0);
    if ( !logged ) _vfdreplayindexlog();
    
    return SM_SUCCESS;
}

static int 
_vfdreplayindexlog() {
    int x =0;
    long count = BLCKSZ;
    FilePin(index_file,0);
    while ( count == BLCKSZ ) {
        if ( IndexStore.header.index_magic != INDEX_MAGIC ) break;

        for ( x=0;x<IndexStore.header.count;x++) {
            SmgrData* data = IndexStore.header.blocks + x;
            smgraddrecoveredpage(NameStr(data->dbname),data->dbid,data->relid,data->nblocks);
        }

        count = FileRead(index_file,IndexStore.data,BLCKSZ);
    }
    FileUnpin(index_file,0);
}

static long
_vfdreplaysegment() {
        int count = 0;
        long ret = 0;
        long total = 0;
        File fd = -1;
        Oid crel = 0,cdb = 0;
    
        ret = FileRead(log_file,SegmentStore.data,BLCKSZ);
        total += ret;
        if ( ret != BLCKSZ ) {
            return -1;
        }
        
        if ( SegmentStore.header.segment_magic != SEGMENT_MAGIC ) {
            vfd_log("VFD Seg ID: %d segment is invalid skipping",SegmentStore.header.seg_id);
            return -1;
        }
        
        vfd_log("VFD Seg ID: %d count: %d",SegmentStore.header.seg_id,SegmentStore.header.count);
            
        for (count=0;count<SegmentStore.header.count;count++) {
            SmgrInfo info = &SegmentStore.header.blocks[count];

            vfd_log("replay %s-%s relid:%d dbid:%d block:%d",NameStr(info->relname),
                NameStr(info->dbname),info->relid,info->dbid,info->nblocks);

            ret = FileRead(log_file,scratch_space,BLCKSZ);
            
            if ( ret == BLCKSZ ) {
                total += ret;
                if ( cdb != info->dbid || crel != info->relid) {
                    char* path = relpath_blind(NameStr(info->dbname),NameStr(info->relname),info->dbid,info->relid);
                    if ( fd > 0 ) {
                        FileUnpin(fd,0);
                        FileClose(fd);
                    }
                    fd = FileNameOpenFile(path, O_WRONLY | O_LARGEFILE, 0600);
                    FilePin(fd,0);
	            cdb = info->dbid;
                    crel = info->relid;
                    pfree(path);
                }

                if ( fd > 0 ) {
                    FileSeek(fd,info->nblocks * BLCKSZ,SEEK_SET);
                    FileWrite(fd,scratch_space,BLCKSZ);
                    if (info->relkind == RELKIND_INDEX ) {
                        smgraddrecoveredpage(NameStr(info->dbname),cdb,crel,info->nblocks);
                    }
                } else {
                    vfd_log("%s-%s not opened, no block written",NameStr(info->dbname),NameStr(info->relname));
                }
            }  
        }
        if ( fd > 0 ) {
            FileUnpin(fd,0);
            FileClose(fd);
        }
        
        return total; 
}

void  vfd_log(char* pattern, ...) {
    char            msg[256];
    va_list         args;
    /*
#ifdef SUNOS
    va_start(args, pattern);
    vsprintf(msg,pattern,args);
    DTRACE_PROBE3(mtpg,vfd__msg,msg);
    va_end(args);
#else
    elog(DEBUG,msg);
#endif
*/
    va_start(args, pattern);
    vsprintf(msg,pattern,args);
    elog(DEBUG,msg);
    va_end(args);
}

