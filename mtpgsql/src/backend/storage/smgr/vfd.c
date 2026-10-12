/*-------------------------------------------------------------------------
 *
 * vfd.c
 *	  This code manages relations that reside on magnetic disk.
 *
 * Portions Copyright (c) 2000-2024, Myron Scott  <myron@weaverdb.org>
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *  Logging code is NOT THREAD SAFE.  Make sure that only one thread is logging
 *  on the system
 *
 *  Shadow logs live in DataDir/pg_shadowlog (or vfdlogfile) as uniquely
 *  named files log_000...N. Expire rotates to a new file and deletes all
 *  but the newest three. Recovery replays the remaining files in sequence.
 *
 * IDENTIFICATION
 *
 *
 *-------------------------------------------------------------------------
 */
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <execinfo.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <dirent.h>


#include "postgres.h"
#include "env/env.h"

#include "catalog/catalog.h"
#include "miscadmin.h"
#include "storage/smgr.h"
#include "storage/smgr_spi.h"
#include "utils/relcache.h"
#include "env/properties.h"
#include "utils/lzf.h"

#undef DIAGNOSTIC

static File     log_file = -1;

static long     log_count;
static long     log_pos;

#define HEADER_MAGIC  0xCAFE08072006BABE
#define SEGMENT_MAGIC  0xABCDEF0123456789
#define INDEX_MAGIC  0x9876543210FEDCBA

#define DEFAULT_SHADOWLOG_DIR	"pg_shadowlog"
#define SHADOWLOG_PREFIX	"log_"
#define SHADOWLOG_DIGITS	18
#define SHADOWLOG_KEEP		3

typedef struct shadowlogfile {
    long        seq;
    char        path[MAXPGPATH];
} ShadowLogFile;

static char     log_directory[MAXPGPATH];
static long     log_file_seq = 0;
static long     pid;

#ifdef MACOSX
#define O_LARGEFILE 0x0
#endif

static union  logbuffer {
    struct {
        int64    header_magic;
        int64    process;
        int64    log_id;
        int64    segments;
        bool    completed;
        bool    compressed;
        pthread_t   owner;
    } LogHeader;
    char        block[BLCKSZ];
} LogBuffer;

typedef struct logsegment {
    int64        segment_magic;
    int64        seg_id;
    int16         count;
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

static long _vfdreplaylogfile(File logfile, bool indexonly);
static long _vfdreplaysegment(File logfile,bool indexingonly, bool compressed);

static File _openlogfile(char* path, bool replay);
static void _resolvelogdir(void);
static int _ensurelogdir(void);
static void _importlegacyfile(const char *srcpath);
static void _importleftovers(void);
static bool _parselogfilename(const char *name, long *seq);
static int _cmp_shadowlog(const void *a, const void *b);
static int _listlogfiles(ShadowLogFile **out_files, int *out_count);
static void _buildlogpath(char *dst, size_t dstlen, long seq);
static File _opennewlogfile(void);
static int _prunelogfiles(int keep);
static void _freeloglist(ShadowLogFile *files);

static void  vfd_log(char* pattern, ...);


static void
_resolvelogdir(void)
{
    char *prop = GetProperty("vfdlogfile");

    if (prop == NULL) {
        prop = DEFAULT_SHADOWLOG_DIR;
    }

    if (prop[0] == SEP_CHAR) {
        snprintf(log_directory, sizeof(log_directory), "%s", prop);
    } else {
        snprintf(log_directory, sizeof(log_directory), "%s%c%s",
                 DataDir ? DataDir : ".", SEP_CHAR, prop);
    }
}

static void
_buildlogpath(char *dst, size_t dstlen, long seq)
{
    snprintf(dst, dstlen, "%s%c%s%0*ld",
             log_directory, SEP_CHAR, SHADOWLOG_PREFIX, SHADOWLOG_DIGITS, seq);
}

static bool
_parselogfilename(const char *name, long *seq)
{
    const char *p;
    long value = 0;
    int digits = 0;

    if (strncmp(name, SHADOWLOG_PREFIX, strlen(SHADOWLOG_PREFIX)) != 0) {
        return false;
    }

    p = name + strlen(SHADOWLOG_PREFIX);
    if (*p == '\0') {
        return false;
    }
    while (*p != '\0') {
        if (*p < '0' || *p > '9') {
            return false;
        }
        value = value * 10 + (*p - '0');
        digits++;
        p++;
    }
    if (digits != SHADOWLOG_DIGITS) {
        return false;
    }
    *seq = value;
    return true;
}

static int
_cmp_shadowlog(const void *a, const void *b)
{
    const ShadowLogFile *fa = (const ShadowLogFile *) a;
    const ShadowLogFile *fb = (const ShadowLogFile *) b;

    if (fa->seq < fb->seq) {
        return -1;
    }
    if (fa->seq > fb->seq) {
        return 1;
    }
    return 0;
}

static int
_listlogfiles(ShadowLogFile **out_files, int *out_count)
{
    DIR *dir;
    struct dirent *de;
    ShadowLogFile *files = NULL;
    int count = 0;
    int cap = 0;

    *out_files = NULL;
    *out_count = 0;

    dir = opendir(log_directory);
    if (dir == NULL) {
        return -1;
    }

    while ((de = readdir(dir)) != NULL) {
        long seq;
        ShadowLogFile *grown;

        if (!_parselogfilename(de->d_name, &seq)) {
            continue;
        }
        if (count >= cap) {
            cap = (cap == 0) ? 16 : cap * 2;
            grown = (ShadowLogFile *) os_realloc(files, cap * sizeof(ShadowLogFile));
            if (grown == NULL) {
                _freeloglist(files);
                closedir(dir);
                return -1;
            }
            files = grown;
        }
        files[count].seq = seq;
        snprintf(files[count].path, sizeof(files[count].path), "%s%c%s",
                 log_directory, SEP_CHAR, de->d_name);
        count++;
    }
    closedir(dir);

    if (count > 1) {
        qsort(files, count, sizeof(ShadowLogFile), _cmp_shadowlog);
    }

    *out_files = files;
    *out_count = count;
    return 0;
}

static void
_importlegacyfile(const char *srcpath)
{
    struct stat st;
    char dest[MAXPGPATH];
    int tries;

    if (stat(srcpath, &st) != 0 || !S_ISREG(st.st_mode)) {
        return;
    }

    for (tries = 0; tries < 1000; tries++) {
        log_file_seq++;
        _buildlogpath(dest, sizeof(dest), log_file_seq);
        if (rename(srcpath, dest) == 0) {
            vfd_log("migrated legacy shadow log %s -> %s", srcpath, dest);
            return;
        }
        if (errno != EEXIST) {
            vfd_log("could not migrate legacy shadow log %s", srcpath);
            log_file_seq--;
            return;
        }
    }

    vfd_log("could not migrate legacy shadow log %s", srcpath);
}

static void
_importleftovers(void)
{
    char leftover[MAXPGPATH];

    snprintf(leftover, sizeof(leftover), "%s.old", log_directory);
    _importlegacyfile(leftover);
    snprintf(leftover, sizeof(leftover), "%s.legacy", log_directory);
    _importlegacyfile(leftover);
}

static int
_ensurelogdir(void)
{
    struct stat st;
    char aside[MAXPGPATH];
    ShadowLogFile *files = NULL;
    int count = 0;

    if (stat(log_directory, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            log_file_seq = 0;
            if (_listlogfiles(&files, &count) == 0 && count > 0) {
                log_file_seq = files[count - 1].seq;
            }
            _freeloglist(files);
            _importleftovers();
            return 0;
        }
        if (!S_ISREG(st.st_mode)) {
            vfd_log("shadow log path %s exists and is not a file or directory", log_directory);
            return -1;
        }

        /*
         * Pre-directory layout: pg_shadowlog plus optional pg_shadowlog.old.
         * Move the live file aside, create the directory, then import .old
         * first so it sorts before the newer current log.
         */
        snprintf(aside, sizeof(aside), "%s.legacy", log_directory);
        if (rename(log_directory, aside) != 0) {
            vfd_log("could not move legacy shadow log %s", log_directory);
            return -1;
        }
        if (mkdir(log_directory, S_IRWXU) != 0) {
            vfd_log("could not create shadow log directory %s", log_directory);
            return -1;
        }
        log_file_seq = 0;
        _importleftovers();
        return 0;
    }

    if (errno != ENOENT) {
        vfd_log("could not stat shadow log path %s", log_directory);
        return -1;
    }

    if (mkdir(log_directory, S_IRWXU) != 0 && errno != EEXIST) {
        vfd_log("could not create shadow log directory %s", log_directory);
        return -1;
    }

    log_file_seq = 0;
    _importleftovers();
    return 0;
}

static File
_openlogfile(char* logfile_path, bool replay) {
      File  file;
      int fileflags = (replay) ? (O_RDONLY) : (O_WRONLY | O_CREAT | O_EXCL);

#ifdef LINUX        
    if ( !replay && GetBoolProperty("vfdoptimize_log") ) {
        fileflags |= O_DIRECT;
    }
#endif      
    file = PathNameOpenFile(logfile_path, fileflags , 0600);
     if ( file < 0 ) {
        return file;
    }
    if ( !replay && GetBoolProperty("vfdoptimize_log") ) {
           FileOptimize(file);  
    }
 
    FilePin(file,0);
    FileSeek(file,0,SEEK_SET);
    FileUnpin(file,0);
    
    return file;
}

static File
_opennewlogfile(void)
{
    char path[MAXPGPATH];
    File file;
    int tries;

    for (tries = 0; tries < 1000; tries++) {
        log_file_seq++;
        _buildlogpath(path, sizeof(path), log_file_seq);
        file = _openlogfile(path, false);
        if (file >= 0) {
            return file;
        }
        if (errno != EEXIST) {
            vfd_log("could not open new shadow log %s", path);
            return file;
        }
    }

    vfd_log("could not allocate a unique shadow log name in %s", log_directory);
    return -1;
}

static int
_prunelogfiles(int keep)
{
    ShadowLogFile *files = NULL;
    int count = 0;
    int i;
    const char *current = NULL;
    int limit;

    if (_listlogfiles(&files, &count) < 0) {
        return -1;
    }
    if (log_file > 0) {
        current = FileGetName(log_file);
    }

    limit = count - keep;
    for (i = 0; i < limit; i++) {
        if (current != NULL && strcmp(current, files[i].path) == 0) {
            continue;
        }
        if (unlink(files[i].path) != 0) {
            vfd_log("could not remove expired shadow log %s", files[i].path);
        }
    }

    _freeloglist(files);
    return 0;
}

static void
_freeloglist(ShadowLogFile *files)
{
    if (files != NULL) {
        os_free(files);
    }
}

int
vfdinit()
{
    ShadowLogFile *files = NULL;
    int count = 0;

    _resolvelogdir();

    if (PropertyIsValid("vfdlogindex")) {
        log_index = GetBoolProperty("vfdlogindex");
    }

    if (PropertyIsValid("vfdcompress_log")) {
        compress_log = GetBoolProperty("vfdcompress_log");
    }

    if (_ensurelogdir() < 0) {
        return SM_FAIL;
    }

    log_file_seq = 0;
    if (_listlogfiles(&files, &count) == 0 && count > 0) {
        log_file_seq = files[count - 1].seq;
    }
    _freeloglist(files);

    log_file = _opennewlogfile();
    if (log_file < 0) {
        return SM_FAIL;
    }

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
        LogBuffer.LogHeader.process = MyProcPid;
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
                elog(WARN,"file extend failed %d does not equal block size",nbytes);
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
			elog(WARN, "vfdopen: couldn't open %s: %m", path);
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
            elog(WARN,"File not valid");
            return SM_FAIL;
        }

        FilePin(fd, 3);
        Assert(strstr(FileGetName(fd),NameStr(info->relname))!=NULL);
	if (FileSeek(fd, seekpos, SEEK_SET) != seekpos) {
            elog(WARN,"bad read seek filename:%s, %ld db:%s,rel:%s,blk no.:%lu",FileGetName(fd),seekpos,NameStr(info->dbname),NameStr(info->relname),blocknum);
            status = SM_FAIL_SEEK;
        } else {
            blit = FileRead(fd, buffer, BLCKSZ);
            if (blit < 0) {
                elog(WARN,"bad read %d filename:%s, db:%s,rel:%s,blk no.:%lu",errno,FileGetName(fd),NameStr(info->dbname),NameStr(info->relname),blocknum);
                status = SM_FAIL_BASE;
            } else if ( blit == 0 ) {
                long checkpos = FileSeek(fd,0L,SEEK_END);
                MemSet(buffer, 0, BLCKSZ);
                if ( seekpos >= checkpos ) {
                    if ( seekpos > checkpos ) {
                        elog(WARN,"read past end of file filename: %s, rel: %s %ld %ld",FileGetName(fd), NameStr(info->relname),seekpos,checkpos);
                    }
                    status = SM_FAIL_EOF;
                } else {
                    if (FileSeek(fd, seekpos, SEEK_SET) != seekpos) {
                        elog(WARN,"read past end of file filename: %s, rel: %s %ld %ld",FileGetName(fd), NameStr(info->relname),seekpos,checkpos);
                        status = SM_FAIL_SEEK;
                    }
                }
            } else if ( blit != BLCKSZ ) {
                elog(WARN,"bad read %d filename:%s,db:%s,rel:%s,blk no.:%lu,read length:%d",errno,FileGetName(fd),NameStr(info->dbname),NameStr(info->relname),blocknum,blit);
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
	/* Writing at/past EOF grows the file; keep nblocks in sync. */
	if (status == SM_SUCCESS && (long) (blocknum + 1) > info->nblocks)
		info->nblocks = (long) (blocknum + 1);
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
	if (status == SM_SUCCESS && (long) (blocknum + 1) > info->nblocks)
		info->nblocks = (long) (blocknum + 1);
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
    LogBuffer.LogHeader.process = MyProcPid;
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
    long put = BLCKSZ;
    
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
    File next;

    /*
     * Rotate to a new uniquely named log in the shadow log directory,
     * then drop everything but the newest three files. The previous log
     * is kept because SyncBuffers may not have made those pages durable.
     */
    next = _opennewlogfile();
    if (next < 0) {
        return SM_FAIL;
    }
    if (log_file > 0) {
        FileClose(log_file);
    }
    log_file = next;

    if (_prunelogfiles(SHADOWLOG_KEEP) < 0) {
        vfd_log("could not prune shadow logs in %s", log_directory);
    }

    return SM_SUCCESS;
}


int
vfdremovelogs(int keep) {
    if (_prunelogfiles(keep) < 0) {
        vfd_log("could not prune shadow logs in %s", log_directory);
    }

    return SM_SUCCESS;
}

int
vfdreplaylogs() {
    ShadowLogFile *files = NULL;
    int count = 0;
    int i;
    long pid = 0;
    const char *current = NULL;
    File logfile;

    if (log_directory[0] == '\0') {
        _resolvelogdir();
        if (_ensurelogdir() < 0) {
            return SM_FAIL;
        }
    }

    /*
     * Replay every remaining shadow log in sequence order. Newer files
     * overlay older full-page images. Skip the log opened for writing
     * at init; it is empty until the first post-recovery dump.
     */
    if (_listlogfiles(&files, &count) < 0) {
        vfd_log("could not list shadow logs in %s", log_directory);
        return SM_FAIL;
    }

    if (log_file > 0) {
        current = FileGetName(log_file);
    }

    for (i = 0; i < count; i++) {
        if (current != NULL && strcmp(current, files[i].path) == 0) {
            continue;
        }
        logfile = _openlogfile(files[i].path, true);
        if (logfile < 0) {
            vfd_log("could not open shadow log %s for replay", files[i].path);
            continue;
        }
        long fpid = _vfdreplaylogfile(logfile, false);
        if (fpid > 0) {
            Assert(pid == 0 || fpid == pid);
            pid = fpid;
        }
        FileUnlink(logfile);
    }

    _freeloglist(files);
    log_count = LogBuffer.LogHeader.log_id + 1;
    return SM_SUCCESS;
}

static long
_vfdreplaylogfile(File logfile, bool indexonly) {
    int      count;
    long read = 0;
    long total = 0;
    long end = 0;
    long id = 0;
    long pid = 0;
    
    if ( logfile < 0 ) {
        vfd_log("Log File not valid. exiting.");
        return 0;
    }
    vfd_log("--- Replaying VFD storage manager log %s ---", FileGetName(logfile));
    FilePin(logfile,0);
    end = FileSeek(logfile,0,SEEK_END);
    FileSeek(logfile,0,SEEK_SET);
    
    while (total < end) {
        
        read = FileRead(logfile,LogBuffer.block,BLCKSZ);

        if ( read != BLCKSZ ) {
            vfd_log("Log File not valid. exiting.");
            break;
        }
        total += read;
        if ( LogBuffer.LogHeader.header_magic != HEADER_MAGIC ) {
            vfd_log("VFD Log ID: %ld-%d invalid log file. exiting.",LogBuffer.LogHeader.process,LogBuffer.LogHeader.log_id);
            break;
        }
        if ( !LogBuffer.LogHeader.completed ) {
            vfd_log("VFD Log ID: %ld-%d not completed. exiting.",LogBuffer.LogHeader.process,LogBuffer.LogHeader.log_id);
            break;
        }
        if ( id != 0 && id+1 != LogBuffer.LogHeader.log_id ) {
            vfd_log("VFD Log ID: %ld-%d out of sequence. exiting.",LogBuffer.LogHeader.process,LogBuffer.LogHeader.log_id);
            break;
        } else {
            id =  LogBuffer.LogHeader.log_id;
        }
        pid = LogBuffer.LogHeader.process;
        vfd_log("VFD Log ID: %ld-%d, complete: %s, segments: %d",LogBuffer.LogHeader.process, 
            LogBuffer.LogHeader.log_id,
            (LogBuffer.LogHeader.completed) ? "true":"false",LogBuffer.LogHeader.segments);   
        
        for ( count=0;count<LogBuffer.LogHeader.segments;count++) {
            long add =  _vfdreplaysegment(logfile,indexonly,LogBuffer.LogHeader.compressed);
            if ( add < 0 ) {
                vfd_log("exiting due to invalid segment");
                break;
            }
            total += add;
        }        
    }
    log_count = LogBuffer.LogHeader.log_id + 1;
    FileUnpin(logfile,0);
    
    return pid;    
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
    elog(DEBUG, "%s", msg);
    va_end(args);
}

