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
#include <fcntl.h>
#include <sys/file.h>

#include "postgres.h"
#include "env/env.h"

#include "catalog/catalog.h"
#include "miscadmin.h"
#include "storage/smgr.h"
#include "storage/smgr_spi.h"
#include "utils/relcache.h"
#include "env/connectionutil.h"
#include "utils/lzf.h"

#undef DIAGNOSTIC

static File     log_file;

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
        bool    compressed;
        pthread_t   owner;
    } LogHeader;
    char        block[BLCKSZ];
} LogBuffer;

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


/*  block cache space, created in the init script  */

static char*        scratch_space;
static int          scratch_loc = 0;
static bool         compress_log = FALSE;
static bool         log_index = TRUE;

/* routines declared here */
static BlockNumber _vfdnblocks(File file, Size blcksz);
static int _vfddumplogtodisk(void);

static bool _vfdreplaylogfile(File logfile, bool indexonly);
static long _vfdreplaysegment(File logfile,bool indexingonly, bool compressed);

static File _openlogfile(char* path, bool replay);

static void  vfd_log(char* pattern, ...);




File
_openlogfile(char* logfile_path, bool replay) {
      File  file;
      char path[256];
      int fileflags = (replay) ? (O_RDONLY) : (O_WRONLY | O_CREAT);
      char* datadir = DataDir;
      
        if ( *logfile_path == SEP_CHAR ) {
            sprintf(path,"%s",logfile_path);
        } else {
            sprintf(path,"%s%c%s",datadir,SEP_CHAR,logfile_path);
        }

#ifdef LINUX        
    if ( !replay && GetBoolProperty("vfdoptimize_log") ) {
        fileflags |= O_DIRECT;
    }
#endif      
    file = PathNameOpenFile(path, fileflags , 0600);
     if ( file < 0 ) {
        elog(FATAL,"unable to access %s",logfile_path);
    }
    if ( !replay && GetBoolProperty("vfdoptimize_log") ) {
           FileOptimize(file);  
    }
 
    FilePin(file,0);
    FileSeek(file,0,SEEK_SET);
    FileUnpin(file,0);
    
    return file;
}

int
vfdinit()
{    
      char* logfile_path = GetProperty("vfdlogfile");

      if ( logfile_path == NULL ) {
          logfile_path = "pg_shadowlog";
      }
      
      if ( PropertyIsValid("vfdlogindex") ) {
          log_index = GetBoolProperty("vfdlogindex");
      }
     
      if ( PropertyIsValid("vfdcompress_log") ) {
          compress_log = GetBoolProperty("vfdcompress_log");
      }  
      
    log_file = _openlogfile(logfile_path, false);

    max_blocks = ((sizeof(SegmentStore) - MAXALIGN((char*)&SegmentStore - (char*)&SegmentStore.header.blocks)) / sizeof(SmgrData));
    log_count = 0;
        
    scratch_space = os_malloc((BLCKSZ + 4) * (max_blocks + 1));
    
    log_pos = 0;
    
    return SM_SUCCESS;
}

int
vfdshutdown()
{
    if ( log_file > 0 ) {
        FilePin(log_file,0);
        LogBuffer.LogHeader.header_magic = HEADER_MAGIC;
        LogBuffer.LogHeader.log_id = log_count;
        LogBuffer.LogHeader.completed = false;
        LogBuffer.LogHeader.compressed = compress_log;
        LogBuffer.LogHeader.segments = 0;  

        log_pos = FileSeek(log_file,0,SEEK_END);

        FileWrite(log_file,LogBuffer.block,BLCKSZ);
        FileUnpin(log_file,0);
        FileClose(log_file);
    }
    
    os_free(scratch_space);
    return SM_SUCCESS;
}

int
vfdcreate(SmgrInfo info)
{
	int         fd;
	char	   *path;

	path = relpath_blind(NameStr(info->dbname),NameStr(info->relname),info->dbid,info->relid);
	fd = FileNameOpenFile(path, O_RDWR | O_CREAT | O_EXCL| O_LARGEFILE,  0600);

	if (fd < 0)
	{
		fd = FileNameOpenFile(path, O_RDWR| O_LARGEFILE, 0600);

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
        Assert(strcmp(path,FileGetName(fd)) == 0);
	info->unlinked = false;
        info->fd = fd;

	pfree(path);

	return SM_SUCCESS;
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


	seekpos = (long) (BLCKSZ * (blocknum));

        if ( fd < 0 ) {
            elog(NOTICE,"File not valid");
            return SM_FAIL;
        }

        FilePin(fd, 3);
        Assert(strstr(FileGetName(fd),NameStr(info->relname))!=NULL);
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
                MemSet(buffer, 0, BLCKSZ);
                if ( seekpos >= checkpos ) {
                    if ( seekpos > checkpos ) {
                        elog(NOTICE,"read past end of file filename: %s, rel: %s %ld %ld",FileGetName(fd), NameStr(info->relname),seekpos,checkpos);
                    }
                } else {
                    if (FileSeek(fd, seekpos, SEEK_SET) != seekpos) {
                        elog(NOTICE,"read past end of file filename: %s, rel: %s %ld %ld",FileGetName(fd), NameStr(info->relname),seekpos,checkpos);
                        status = SM_FAIL_SEEK;
                    }
                }
            } else if ( blit != BLCKSZ ) {
                elog(NOTICE,"bad read %d filename:%s,db:%s,rel:%s,blk no.:%llu,read length:%d",errno,FileGetName(fd),NameStr(info->dbname),NameStr(info->relname),blocknum,blit);
                status = SM_FAIL_BASE;
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
	if (FileWrite(fd, buffer, BLCKSZ) == BLCKSZ ) {
            if (FileSync(fd) < 0) {
		status = SM_FAIL;
            }
        } else {
            status = SM_FAIL;
        }
            
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
        
        FileBaseSync(fd,nblocks * BLCKSZ);
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
    LogBuffer.LogHeader.compressed = compress_log;
    LogBuffer.LogHeader.segments = 0;  
    FilePin(log_file,0); 
    log_pos = FileSeek(log_file,0,SEEK_END);
        
    FileWrite(log_file,LogBuffer.block,BLCKSZ);
    FileSync(log_file);
    
    SegmentStore.header.count = 0;
    
    FileUnpin(log_file,0); 
    return SM_SUCCESS;
}

int
vfdlog(SmgrInfo info,BlockNumber block, char* buffer) {
/*
    int32 put = BLCKSZ;
*/
    ulong put = BLCKSZ;
    
    if ( SegmentStore.header.count == max_blocks ) {
        _vfddumplogtodisk();
    }
    
    info->nblocks = block;
    memmove(SegmentStore.header.blocks + SegmentStore.header.count,info,sizeof(SmgrData)); 
    if ( compress_log ) {
        put = lzf_compress(buffer,BLCKSZ,scratch_space + scratch_loc + 4,BLCKSZ-1);
        if ( !put ) {
            put = BLCKSZ;
            *(int32*)(scratch_space + scratch_loc) = put;
            memmove(scratch_space + scratch_loc + 4,buffer,BLCKSZ);
        } else {
            *(int32*)(scratch_space + scratch_loc) = put;
        }
        scratch_loc += put + 4;
    } else {
        memmove(scratch_space + scratch_loc,buffer,BLCKSZ);
        scratch_loc += BLCKSZ;
    }
    SegmentStore.header.count += 1;
    
    return SM_SUCCESS;    
}
    
int
_vfddumplogtodisk() {
    int ret = 0;
    
    if ( SegmentStore.header.count == 0 ) return 0;
    
    SegmentStore.header.segment_magic = SEGMENT_MAGIC;
    SegmentStore.header.seg_id = LogBuffer.LogHeader.segments++;
    FilePin(log_file,0);
    ret += FileWrite(log_file,SegmentStore.data,BLCKSZ);
    ret += FileWrite(log_file,scratch_space,scratch_loc);
    scratch_loc = 0;
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

    char newname[1024];
    memset(newname,0x00,1024);
    int len = strlen(FileGetName(log_file));
    
    strncpy(newname,FileGetName(log_file),len);
    strncpy(newname + len,".old",4);
    
    FileRename(log_file,newname);
    newname[len] = 0x00;
    log_file = _openlogfile(newname,false);
}

int
vfdreplaylogs() {
    bool logged = false;
        
      char* logfile_path = GetProperty("vfdlogfile");
      
      if ( logfile_path == NULL ) {
        logfile_path = "pg_shadowlog";
      }
        
    File logfile = _openlogfile(logfile_path,true);
    logged = _vfdreplaylogfile(logfile,false);
    log_count = LogBuffer.LogHeader.log_id + 1;
    FileClose(logfile);
    
    if ( !logged ) {
        char newname[1024];
       
        memset(newname,0x00,1024);
        int len = strlen(logfile_path);
    
        strncpy(newname,logfile_path,len);
        strncpy(newname + len,".old",4);
    
        logfile = _openlogfile(newname,true);
        _vfdreplaylogfile(logfile,true);
        FileClose(logfile);
    }
    
    return SM_SUCCESS;
}

static bool
_vfdreplaylogfile(File logfile, bool indexonly) {
    int      count;
    long read = 0;
    long total = 0;
    long end = 0;
    long id = 0;
    int result = SM_SUCCESS;
    bool logged = false;
    
    vfd_log("--- Replaying VFD storage manager log ---");
   	 
    if ( logfile < 0 ) {
        vfd_log("Log File not valid. exiting.");
        return SM_SUCCESS;
    }
    FilePin(logfile,0);
    end = FileSeek(logfile,0,SEEK_END);
    FileSeek(logfile,0,SEEK_SET);
    
    while (total < end) {
        
        read = FileRead(logfile,LogBuffer.block,BLCKSZ);

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
            long add =  _vfdreplaysegment(logfile,indexonly,LogBuffer.LogHeader.compressed);
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
    FileUnpin(logfile,0);
    
    return logged;    
}

static long
_vfdreplaysegment(File logfile,bool indexingonly, bool compressed) {
        int count = 0;
        long ret = 0;
        long total = 0;
        File fd = -1;
        Oid crel = 0,cdb = 0;
        char* read_block = scratch_space;
        char* write_block = scratch_space + BLCKSZ;
       
                
        ret = FileRead(logfile,SegmentStore.data,BLCKSZ);
        
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

            if ( compressed ) {
                int32  get = BLCKSZ;
                FileRead(logfile,(char*)&get,sizeof(get));
                FileRead(logfile,(char*)read_block,get);

               if ( get != BLCKSZ ) {
                   write_block = read_block + BLCKSZ;
                   ret = lzf_decompress(read_block,get,write_block,BLCKSZ);
               } else {
                   write_block = read_block;
                   ret = BLCKSZ;
               }
            } else {
                ret = FileRead(logfile,read_block,BLCKSZ);
                write_block = read_block;
            }
            
            if ( indexingonly ) {
                if (info->relkind == RELKIND_INDEX ) {
                        smgraddrecoveredpage(NameStr(info->dbname),info->dbid,info->relid,info->nblocks);
                }
            } else if ( ret == BLCKSZ ) {
                total += ret;
                if ( cdb != info->dbid || crel != info->relid) {
                    char* path = relpath_blind(NameStr(info->dbname),NameStr(info->relname),info->dbid,info->relid);
                    if ( fd > 0 ) {
                        FileUnpin(fd,0);
                        FileClose(fd);
                    }
                    fd = FileNameOpenFile(path, O_WRONLY | O_LARGEFILE, 0600);
                    if ( fd >= 0 ) {
                        FilePin(fd,0);
                        cdb = info->dbid;
                        crel = info->relid;
                        pfree(path);
                    }
                }

                if ( fd > 0 ) {
                    FileSeek(fd,info->nblocks * BLCKSZ,SEEK_SET);
                    FileWrite(fd,write_block,BLCKSZ);
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

    va_start(args, pattern);
    vsprintf(msg,pattern,args);
    elog(DEBUG,msg);
    va_end(args);
}

