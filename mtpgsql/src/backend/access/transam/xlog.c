/*-------------------------------------------------------------------------
 *
 * xlog.c
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

#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>

#include "postgres.h"

#include "env/env.h"

#include "access/xlog.h"
#include "access/xact.h"
#include "catalog/catversion.h"
#include "storage/sinval.h"
#include "storage/multithread.h"
#include "storage/spin.h"
#ifdef SPIN_IS_MUTEX
#include "storage/m_lock.h"
#else
#include "storage/s_lock.h"
#endif

void		UpdateControlFile(void);
void		BootStrapXLOG(void);
void		StartupXLOG(void);
void		ShutdownXLOG(void);
void		CreateCheckPoint(bool shutdown);

/*
*	Moved to env MKS  7/30/2000
*
*
*
*
*
*XLogRecPtr	MyLastRecPtr = {0, 0};
*bool		StopIfError = false;
*
*/
SPINLOCK	ControlFileLockId;
SPINLOCK	XidGenLockId;


char		XLogDir[MAXPGPATH];
char		ControlFilePath[MAXPGPATH];
uint32		XLOGbuffers = 0;



extern char ReleaseDataFile(void);


extern VariableCache ShmemVariableCache;


#define MinXLOGbuffers	4

typedef struct XLgwrRqst
{
	XLogRecPtr	Write;			/* byte (1-based) to write out */
	XLogRecPtr	Flush;			/* byte (1-based) to flush */
} XLgwrRqst;

typedef struct XLgwrResult
{
	XLogRecPtr	Write;			/* bytes written out */
	XLogRecPtr	Flush;			/* bytes flushed */
} XLgwrResult;

typedef struct XLogCtlInsert
{
	XLgwrResult LgwrResult;
	XLogRecPtr	PrevRecord;
	uint16		curridx;		/* current block index in cache */
	XLogPageHeader currpage;
	char	   *currpos;
} XLogCtlInsert;

typedef struct XLogCtlWrite
{
	XLgwrResult LgwrResult;
	uint16		curridx;		/* index of next block to write */
} XLogCtlWrite;


#ifndef HAS_TEST_AND_SET
#define TAS(lck)		0
#define S_UNLOCK(lck)
#define S_INIT_LOCK(lck)
#endif

typedef struct XLogCtlData
{
	XLogCtlInsert Insert;
	XLgwrRqst	LgwrRqst;
	XLgwrResult LgwrResult;
	XLogCtlWrite Write;
	char	   *pages;
	XLogRecPtr *xlblocks;		/* 1st byte ptr-s + BLCKSZ */
	uint32		XLogCacheByte;
	uint32		XLogCacheBlck;
#ifdef HAS_TEST_AND_SET
	slock_t		insert_lck;
	slock_t		info_lck;
	slock_t		lgwr_lck;
#endif
} XLogCtlData;

static XLogCtlData *XLogCtl = NULL;

typedef enum DBState
{
	DB_STARTUP = 0,
	DB_SHUTDOWNED,
	DB_SHUTDOWNING,
	DB_IN_RECOVERY,
	DB_IN_PRODUCTION
} DBState;

typedef struct ControlFileData
{
    uint32		logId;			/* current log file id */
	uint32		logSeg;			/* current log file segment (1-based) */
	XLogRecPtr	checkPoint;		/* last check point record ptr */
	time_t		time;			/* time stamp of last modification */
	DBState		state;			/* */

	/*
	 * this data is used to make sure that configuration of this DB is
	 * compatible with the current backend
	 */
	uint32		blcksz;			/* block size for this DB */
	unsigned long		relseg_size;	/* blocks per segment of large relation */
	uint32		catalog_version_no;		/* internal version number */

	/*
	 * MORE DATA FOLLOWS AT THE END OF THIS STRUCTURE - locations of data
	 * dirs
	 */
} ControlFileData;

static ControlFileData *ControlFile = NULL;

typedef struct CheckPoint
{
	XLogRecPtr	redo;			/* next RecPtr available when we */
	/* began to create CheckPoint */
	/* (i.e. REDO start point) */
	XLogRecPtr	undo;			/* first record of oldest in-progress */
	/* transaction when we started */
	/* (i.e. UNDO end point) */
	TransactionId nextXid;
	Oid			nextOid;
} CheckPoint;


typedef struct xlog_globals {
/*      from xid.c    */
	bool			StopIfError;
	bool			vacuumrecover;
	XLogRecPtr		MyLastRecPtr;
} XlogGlobals;

static SectionId xlog_id = SECTIONID("XLOG");

#ifdef TLS
TLS XlogGlobals* xlog_globals = NULL;
#else
#define xlog_globals GetEnv()->xlog_globals
#endif

static XlogGlobals* GetXlogInfo(void);

/*
 * We break each log file in 16Mb segments
 */
#define XLogSegSize		(16*1024*1024)
#define XLogLastSeg		(0xffffffff / XLogSegSize)
#define XLogFileSize	(XLogLastSeg * XLogSegSize)

#define XLogFileName(path, log, seg)	\
			snprintf(path, MAXPGPATH, "%s%c%08X%08X",	\
					XLogDir, SEP_CHAR, log, seg)

#define PrevBufIdx(curridx)		\
		((curridx == 0) ? XLogCtl->XLogCacheBlck : (curridx - 1))

#define NextBufIdx(curridx)		\
		((curridx == XLogCtl->XLogCacheBlck) ? 0 : (curridx + 1))

#define XLByteLT(left, right)		\
			(right.xlogid > left.xlogid || \
			(right.xlogid == left.xlogid && right.xrecoff > left.xrecoff))

#define XLByteLE(left, right)		\
			(right.xlogid > left.xlogid || \
			(right.xlogid == left.xlogid && right.xrecoff >=  left.xrecoff))

#define XLByteEQ(left, right)		\
			(right.xlogid == left.xlogid && right.xrecoff ==  left.xrecoff)

#define InitXLBuffer(curridx)	(\
				XLogCtl->xlblocks[curridx].xrecoff = \
				(XLogCtl->xlblocks[Insert->curridx].xrecoff == XLogFileSize) ? \
				BLCKSZ : (XLogCtl->xlblocks[Insert->curridx].xrecoff + BLCKSZ), \
				XLogCtl->xlblocks[curridx].xlogid = \
				(XLogCtl->xlblocks[Insert->curridx].xrecoff == XLogFileSize) ? \
				(XLogCtl->xlblocks[Insert->curridx].xlogid + 1) : \
				XLogCtl->xlblocks[Insert->curridx].xlogid, \
				Insert->curridx = curridx, \
				Insert->currpage = (XLogPageHeader) (XLogCtl->pages + curridx * BLCKSZ), \
				Insert->currpos = \
					((char*) Insert->currpage) + SizeOfXLogPHD, \
				Insert->currpage->xlp_magic = XLOG_PAGE_MAGIC, \
				Insert->currpage->xlp_info = 0 \
				)

#define XRecOffIsValid(xrecoff) \
		(xrecoff % BLCKSZ >= SizeOfXLogPHD && \
		(BLCKSZ - xrecoff % BLCKSZ) >= SizeOfXLogRecord)

static void GetFreeXLBuffer(void);
static void XLogWrite(char *buffer);
static int	XLogFileInit(uint32 log, uint32 seg);
static int	XLogFileOpen(uint32 log, uint32 seg, bool econt);
static XLogRecord *ReadRecord(XLogRecPtr *RecPtr, char *buffer);
static char *str_time(time_t tnow);

static XLgwrResult LgwrResult = {{0, 0}, {0, 0}};
static XLgwrRqst LgwrRqst = {{0, 0}, {0, 0}};

static int	logFile = -1;
static uint32 logId = 0;
static uint32 logSeg = 0;
static uint32 logOff = 0;

static XLogRecPtr ReadRecPtr;
static XLogRecPtr EndRecPtr;
static int	readFile = -1;
static uint32 readId = 0;
static uint32 readSeg = 0;
static uint32 readOff = 0;
static char readBuf[BLCKSZ];
static XLogRecord *nextRecord = NULL;

void 
SetControlFilePath(char* path)
{
	strncpy(ControlFilePath,path,MAXPGPATH);
}

void 
SetXLogDir(char* path)
{
	strncpy(XLogDir,path,MAXPGPATH);
}

XLogRecPtr
XLogInsert(RmgrId rmid, char *hdr, uint32 hdrlen, char *buf, uint32 buflen)
{
	XLogCtlInsert *Insert = &XLogCtl->Insert;
	XLogRecord *record;
	XLogSubRecord *subrecord;
	XLogRecPtr	RecPtr;
	uint32		len = hdrlen + buflen,
				freespace,
				wlen;
	uint16		curridx;
	bool		updrqst = false;
	XlogGlobals* env = GetXlogInfo();
	

	if (len == 0 || len > MAXLOGRECSZ)
		elog(STOP, "XLogInsert: invalid record len %u", len);

	/* obtain xlog insert lock */
	if (TAS(&(XLogCtl->insert_lck)))	/* busy */
	{
		bool		do_lgwr = true;
		unsigned	i = 0;

		for (;;)
		{
			/* try to read LgwrResult while waiting for insert lock */
			if (!TAS(&(XLogCtl->info_lck)))
			{
				LgwrRqst = XLogCtl->LgwrRqst;
				LgwrResult = XLogCtl->LgwrResult;
				S_UNLOCK(&(XLogCtl->info_lck));

				/*
				 * If cache is half filled then try to acquire lgwr lock
				 * and do LGWR work, but only once.
				 */
				if (do_lgwr &&
					(LgwrRqst.Write.xlogid != LgwrResult.Write.xlogid ||
					 (LgwrRqst.Write.xrecoff - LgwrResult.Write.xrecoff >=
					  XLogCtl->XLogCacheByte / 2)))
				{
					if (!TAS(&(XLogCtl->lgwr_lck)))
					{
						LgwrResult = XLogCtl->Write.LgwrResult;
						if (!TAS(&(XLogCtl->info_lck)))
						{
							LgwrRqst = XLogCtl->LgwrRqst;
							S_UNLOCK(&(XLogCtl->info_lck));
						}
						if (XLByteLT(LgwrResult.Write, LgwrRqst.Write))
						{
							XLogWrite(NULL);
							do_lgwr = false;
						}
						S_UNLOCK(&(XLogCtl->lgwr_lck));
					}
				}
			}
			s_lock_sleep(i++);
			if (!TAS(&(XLogCtl->insert_lck)))
				break;
		}
	}

	freespace = ((char *) Insert->currpage) + BLCKSZ - Insert->currpos;
	if (freespace < SizeOfXLogRecord)
	{
		curridx = NextBufIdx(Insert->curridx);
		if (XLByteLE(XLogCtl->xlblocks[curridx], LgwrResult.Write))
			InitXLBuffer(curridx);
		else
			GetFreeXLBuffer();
		freespace = BLCKSZ - SizeOfXLogPHD;
	}
	else
		curridx = Insert->curridx;

	freespace -= SizeOfXLogRecord;
	record = (XLogRecord *) Insert->currpos;
	record->xl_prev = Insert->PrevRecord;
	if (rmid != RM_XLOG_ID)
		record->xl_xact_prev = env->MyLastRecPtr;
	else
	{
		record->xl_xact_prev.xlogid = 0;
		record->xl_xact_prev.xrecoff = 0;
	}
	record->xl_xid = GetCurrentTransactionId();
	record->xl_len = (len > freespace) ? freespace : len;
	record->xl_info = (len > freespace) ? XLR_TO_BE_CONTINUED : 0;
	record->xl_rmid = rmid;
	RecPtr.xlogid = XLogCtl->xlblocks[curridx].xlogid;
	RecPtr.xrecoff =
		XLogCtl->xlblocks[curridx].xrecoff - BLCKSZ +
		Insert->currpos - ((char *) Insert->currpage);
	if (env->MyLastRecPtr.xrecoff == 0 && rmid != RM_XLOG_ID)
	{
		SpinAcquire(SInvalLock);
/*		env->thread->logRec = RecPtr;   */
		SpinRelease(SInvalLock);
	}
	env->MyLastRecPtr = RecPtr;
	RecPtr.xrecoff += record->xl_len;
	Insert->currpos += SizeOfXLogRecord;
	if (freespace > 0)
	{
		wlen = (hdrlen > freespace) ? freespace : hdrlen;
		memcpy(Insert->currpos, hdr, wlen);
		freespace -= wlen;
		hdrlen -= wlen;
		hdr += wlen;
		Insert->currpos += wlen;
		if (buflen > 0 && freespace > 0)
		{
			wlen = (buflen > freespace) ? freespace : buflen;
			memcpy(Insert->currpos, buf, wlen);
			freespace -= wlen;
			buflen -= wlen;
			buf += wlen;
			Insert->currpos += wlen;
		}
		Insert->currpos = ((char *) Insert->currpage) +
			DOUBLEALIGN(Insert->currpos - ((char *) Insert->currpage));
		len = hdrlen + buflen;
	}

	if (len != 0)
	{
nbuf:
		curridx = NextBufIdx(curridx);
		if (XLByteLE(XLogCtl->xlblocks[curridx], LgwrResult.Write))
		{
			InitXLBuffer(curridx);
			updrqst = true;
		}
		else
		{
			GetFreeXLBuffer();
			updrqst = false;
		}
		freespace = BLCKSZ - SizeOfXLogPHD - SizeOfXLogSubRecord;
		Insert->currpage->xlp_info |= XLP_FIRST_IS_SUBRECORD;
		subrecord = (XLogSubRecord *) Insert->currpos;
		Insert->currpos += SizeOfXLogSubRecord;
		if (hdrlen > freespace)
		{
			subrecord->xl_len = freespace;
			subrecord->xl_info = XLR_TO_BE_CONTINUED;
			memcpy(Insert->currpos, hdr, freespace);
			hdrlen -= freespace;
			hdr += freespace;
			goto nbuf;
		}
		else if (hdrlen > 0)
		{
			subrecord->xl_len = hdrlen;
			memcpy(Insert->currpos, hdr, hdrlen);
			Insert->currpos += hdrlen;
			freespace -= hdrlen;
			hdrlen = 0;
		}
		else
			subrecord->xl_len = 0;
		if (buflen > freespace)
		{
			subrecord->xl_len += freespace;
			subrecord->xl_info = XLR_TO_BE_CONTINUED;
			memcpy(Insert->currpos, buf, freespace);
			buflen -= freespace;
			buf += freespace;
			goto nbuf;
		}
		else if (buflen > 0)
		{
			subrecord->xl_len += buflen;
			memcpy(Insert->currpos, buf, buflen);
			Insert->currpos += buflen;
		}
		subrecord->xl_info = 0;
		RecPtr.xlogid = XLogCtl->xlblocks[curridx].xlogid;
		RecPtr.xrecoff = XLogCtl->xlblocks[curridx].xrecoff -
			BLCKSZ + SizeOfXLogPHD + subrecord->xl_len;
		Insert->currpos = ((char *) Insert->currpage) +
			DOUBLEALIGN(Insert->currpos - ((char *) Insert->currpage));
	}
	freespace = ((char *) Insert->currpage) + BLCKSZ - Insert->currpos;

	/*
	 * All done! Update global LgwrRqst if some block was filled up.
	 */
	if (freespace < SizeOfXLogRecord)
		updrqst = true;			/* curridx is filled and available for
								 * writing out */
	else
		curridx = PrevBufIdx(curridx);
	LgwrRqst.Write = XLogCtl->xlblocks[curridx];

	S_UNLOCK(&(XLogCtl->insert_lck));

	if (updrqst)
	{
		unsigned	i = 0;

		for (;;)
		{
			if (!TAS(&(XLogCtl->info_lck)))
			{
				if (XLByteLT(XLogCtl->LgwrRqst.Write, LgwrRqst.Write))
					XLogCtl->LgwrRqst.Write = LgwrRqst.Write;
				S_UNLOCK(&(XLogCtl->info_lck));
				break;
			}
			s_lock_sleep(i++);
		}
	}

	return (RecPtr);
}

void
XLogFlush(XLogRecPtr record)
{
	XLogRecPtr	WriteRqst;
	char		buffer[BLCKSZ];
	char	   *usebuf = NULL;
	unsigned	i = 0;
	bool		force_lgwr = false;

	if (XLByteLE(record, LgwrResult.Flush))
		return;
	WriteRqst = LgwrRqst.Write;
	for (;;)
	{
		/* try to read LgwrResult */
		if (!TAS(&(XLogCtl->info_lck)))
		{
			LgwrResult = XLogCtl->LgwrResult;
			if (XLByteLE(record, LgwrResult.Flush))
			{
				S_UNLOCK(&(XLogCtl->info_lck));
				return;
			}
			if (XLByteLT(XLogCtl->LgwrRqst.Flush, record))
				XLogCtl->LgwrRqst.Flush = record;
			if (XLByteLT(WriteRqst, XLogCtl->LgwrRqst.Write))
			{
				WriteRqst = XLogCtl->LgwrRqst.Write;
				usebuf = NULL;
			}
			S_UNLOCK(&(XLogCtl->info_lck));
		}
		/* if something was added to log cache then try to flush this too */
		if (!TAS(&(XLogCtl->insert_lck)))
		{
			XLogCtlInsert *Insert = &XLogCtl->Insert;
			uint32		freespace =
			((char *) Insert->currpage) + BLCKSZ - Insert->currpos;

			if (freespace < SizeOfXLogRecord)	/* buffer is full */
			{
				usebuf = NULL;
				LgwrRqst.Write = WriteRqst = XLogCtl->xlblocks[Insert->curridx];
			}
			else
			{
				usebuf = buffer;
				memcpy(usebuf, Insert->currpage, BLCKSZ - freespace);
				memset(usebuf + BLCKSZ - freespace, 0, freespace);
				WriteRqst = XLogCtl->xlblocks[Insert->curridx];
				WriteRqst.xrecoff = WriteRqst.xrecoff - BLCKSZ +
					Insert->currpos - ((char *) Insert->currpage);
			}
			S_UNLOCK(&(XLogCtl->insert_lck));
			force_lgwr = true;
		}
		if (force_lgwr || WriteRqst.xlogid > record.xlogid ||
			(WriteRqst.xlogid == record.xlogid &&
			 WriteRqst.xrecoff >= record.xrecoff + BLCKSZ))
		{
			if (!TAS(&(XLogCtl->lgwr_lck)))
			{
				LgwrResult = XLogCtl->Write.LgwrResult;
				if (XLByteLE(record, LgwrResult.Flush))
				{
					S_UNLOCK(&(XLogCtl->lgwr_lck));
					return;
				}
				if (XLByteLT(LgwrResult.Write, WriteRqst))
				{
					LgwrRqst.Flush = LgwrRqst.Write = WriteRqst;
					XLogWrite(usebuf);
					S_UNLOCK(&(XLogCtl->lgwr_lck));
					if (XLByteLT(LgwrResult.Flush, record))
						elog(STOP, "XLogFlush: request is not satisfyed");
					return;
				}
				break;
			}
		}
		s_lock_sleep(i++);
	}

	if (logFile >= 0 && (LgwrResult.Write.xlogid != logId ||
				 (LgwrResult.Write.xrecoff - 1) / XLogSegSize != logSeg))
	{
		if (close(logFile) != 0)
			elog(STOP, "Close(logfile %u seg %u) failed: %d",
				 logId, logSeg, errno);
		logFile = -1;
	}

	if (logFile < 0)
	{
		logId = LgwrResult.Write.xlogid;
		logSeg = (LgwrResult.Write.xrecoff - 1) / XLogSegSize;
		logOff = 0;
		logFile = XLogFileOpen(logId, logSeg, false);
	}

	if (fsync(logFile) != 0)
		elog(STOP, "Fsync(logfile %u seg %u) failed: %d",
			 logId, logSeg, errno);
	LgwrResult.Flush = LgwrResult.Write;

	for (i = 0;;)
	{
		if (!TAS(&(XLogCtl->info_lck)))
		{
			XLogCtl->LgwrResult = LgwrResult;
			if (XLByteLT(XLogCtl->LgwrRqst.Write, LgwrResult.Write))
				XLogCtl->LgwrRqst.Write = LgwrResult.Write;
			S_UNLOCK(&(XLogCtl->info_lck));
			break;
		}
		s_lock_sleep(i++);
	}
	XLogCtl->Write.LgwrResult = LgwrResult;

	S_UNLOCK(&(XLogCtl->lgwr_lck));
	return;

}

static void
GetFreeXLBuffer()
{
	XLogCtlInsert *Insert = &XLogCtl->Insert;
	XLogCtlWrite *Write = &XLogCtl->Write;
	uint16		curridx = NextBufIdx(Insert->curridx);

	LgwrRqst.Write = XLogCtl->xlblocks[Insert->curridx];
	for (;;)
	{
		if (!TAS(&(XLogCtl->info_lck)))
		{
			LgwrResult = XLogCtl->LgwrResult;
			XLogCtl->LgwrRqst.Write = LgwrRqst.Write;
			S_UNLOCK(&(XLogCtl->info_lck));
			if (XLByteLE(XLogCtl->xlblocks[curridx], LgwrResult.Write))
			{
				Insert->LgwrResult = LgwrResult;
				InitXLBuffer(curridx);
				return;
			}
		}

		/*
		 * LgwrResult lock is busy or un-updated. Try to acquire lgwr lock
		 * and write full blocks.
		 */
		if (!TAS(&(XLogCtl->lgwr_lck)))
		{
			LgwrResult = Write->LgwrResult;
			if (XLByteLE(XLogCtl->xlblocks[curridx], LgwrResult.Write))
			{
				S_UNLOCK(&(XLogCtl->lgwr_lck));
				Insert->LgwrResult = LgwrResult;
				InitXLBuffer(curridx);
				return;
			}

			/*
			 * Have to write buffers while holding insert lock - not
			 * good...
			 */
			XLogWrite(NULL);
			S_UNLOCK(&(XLogCtl->lgwr_lck));
			Insert->LgwrResult = LgwrResult;
			InitXLBuffer(curridx);
			return;
		}
	}
/*
	return;
*/
}

static void
XLogWrite(char *buffer)
{
	XLogCtlWrite *Write = &XLogCtl->Write;
	char	   *from;
	uint32		wcnt = 0;
	int			i = 0;

	for (; XLByteLT(LgwrResult.Write, LgwrRqst.Write);)
	{
		LgwrResult.Write = XLogCtl->xlblocks[Write->curridx];
		if (LgwrResult.Write.xlogid != logId ||
			(LgwrResult.Write.xrecoff - 1) / XLogSegSize != logSeg)
		{
			if (wcnt > 0)
			{
				if (fsync(logFile) != 0)
					elog(STOP, "Fsync(logfile %u seg %u) failed: %d",
						 logId, logSeg, errno);
				if (LgwrResult.Write.xlogid != logId)
					LgwrResult.Flush.xrecoff = XLogFileSize;
				else
					LgwrResult.Flush.xrecoff = LgwrResult.Write.xrecoff - BLCKSZ;
				LgwrResult.Flush.xlogid = logId;
				if (!TAS(&(XLogCtl->info_lck)))
				{
					XLogCtl->LgwrResult.Flush = LgwrResult.Flush;
					XLogCtl->LgwrResult.Write = LgwrResult.Flush;
					if (XLByteLT(XLogCtl->LgwrRqst.Write, LgwrResult.Flush))
						XLogCtl->LgwrRqst.Write = LgwrResult.Flush;
					if (XLByteLT(XLogCtl->LgwrRqst.Flush, LgwrResult.Flush))
						XLogCtl->LgwrRqst.Flush = LgwrResult.Flush;
					S_UNLOCK(&(XLogCtl->info_lck));
				}
			}
			if (logFile >= 0)
			{
				if (close(logFile) != 0)
					elog(STOP, "Close(logfile %u seg %u) failed: %d",
						 logId, logSeg, errno);
				logFile = -1;
			}
			logId = LgwrResult.Write.xlogid;
			logSeg = (LgwrResult.Write.xrecoff - 1) / XLogSegSize;
			logOff = 0;
			logFile = XLogFileInit(logId, logSeg);
			SpinAcquire(ControlFileLockId);
			ControlFile->logId = logId;
			ControlFile->logSeg = logSeg + 1;
			ControlFile->time = time(NULL);
			UpdateControlFile();
			SpinRelease(ControlFileLockId);
		}

		if (logFile < 0)
		{
			logId = LgwrResult.Write.xlogid;
			logSeg = (LgwrResult.Write.xrecoff - 1) / XLogSegSize;
			logOff = 0;
			logFile = XLogFileOpen(logId, logSeg, false);
		}

		if (logOff != (LgwrResult.Write.xrecoff - BLCKSZ) % XLogSegSize)
		{
			logOff = (LgwrResult.Write.xrecoff - BLCKSZ) % XLogSegSize;
			if (lseek(logFile, (off_t) logOff, SEEK_SET) < 0)
				elog(STOP, "Lseek(logfile %u seg %u off %u) failed: %d",
					 logId, logSeg, logOff, errno);
		}

		if (buffer != NULL && XLByteLT(LgwrRqst.Write, LgwrResult.Write))
			from = buffer;
		else
			from = XLogCtl->pages + Write->curridx * BLCKSZ;

		if (write(logFile, from, BLCKSZ) != BLCKSZ)
			elog(STOP, "Write(logfile %u seg %u off %u) failed: %d",
				 logId, logSeg, logOff, errno);

		wcnt++;
		logOff += BLCKSZ;

		if (from != buffer)
			Write->curridx = NextBufIdx(Write->curridx);
		else
			LgwrResult.Write = LgwrRqst.Write;
	}
	if (wcnt == 0)
		elog(STOP, "XLogWrite: nothing written");

	if (XLByteLT(LgwrResult.Flush, LgwrRqst.Flush) &&
		XLByteLE(LgwrRqst.Flush, LgwrResult.Write))
	{
		if (fsync(logFile) != 0)
			elog(STOP, "Fsync(logfile %u seg %u) failed: %d",
				 logId, logSeg, errno);
		LgwrResult.Flush = LgwrResult.Write;
	}

	for (;;)
	{
		if (!TAS(&(XLogCtl->info_lck)))
		{
			XLogCtl->LgwrResult = LgwrResult;
			if (XLByteLT(XLogCtl->LgwrRqst.Write, LgwrResult.Write))
				XLogCtl->LgwrRqst.Write = LgwrResult.Write;
			S_UNLOCK(&(XLogCtl->info_lck));
			break;
		}
		s_lock_sleep(i++);
	}
	Write->LgwrResult = LgwrResult;
}

static int
XLogFileInit(uint32 log, uint32 seg)
{
	char		path[MAXPGPATH];
	int			fd;

	XLogFileName(path, log, seg);
	unlink(path);

tryAgain:
#ifndef __CYGWIN__
	fd = open(path, O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
#else
	fd = open(path, O_RDWR | O_CREAT | O_EXCL | O_BINARY, S_IRUSR | S_IWUSR);
#endif
	if (fd < 0 && (errno == EMFILE || errno == ENFILE))
	{
		fd = errno;
		if (!ReleaseDataFile())
			elog(STOP, "Create(logfile %u seg %u) failed: %d (and no one data file can be closed)",
				 logId, logSeg, fd);
		goto tryAgain;
	}
	if (fd < 0)
		elog(STOP, "Init(logfile %u seg %u) failed: %d",
			 logId, logSeg, errno);

	if (lseek(fd, XLogSegSize - 1, SEEK_SET) != (off_t) (XLogSegSize - 1))
		elog(STOP, "Lseek(logfile %u seg %u) failed: %d",
			 logId, logSeg, errno);

	if (write(fd, "", 1) != 1)
		elog(STOP, "Init(logfile %u seg %u) failed: %d",
			 logId, logSeg, errno);

	if (fsync(fd) != 0)
		elog(STOP, "Fsync(logfile %u seg %u) failed: %d",
			 logId, logSeg, errno);

	if (lseek(fd, 0, SEEK_SET) < 0)
		elog(STOP, "Lseek(logfile %u seg %u off %u) failed: %d",
			 log, seg, 0, errno);

	return (fd);
}

static int
XLogFileOpen(uint32 log, uint32 seg, bool econt)
{
	char		path[MAXPGPATH];
	int			fd;

	XLogFileName(path, log, seg);

tryAgain:
#ifndef __CYGWIN__
	fd = open(path, O_RDWR);
#else
	fd = open(path, O_RDWR | O_BINARY);
#endif
	if (fd < 0 && (errno == EMFILE || errno == ENFILE))
	{
		fd = errno;
		if (!ReleaseDataFile())
			elog(STOP, "Open(logfile %u seg %u) failed: %d (and no one data file can be closed)",
				 logId, logSeg, fd);
		goto tryAgain;
	}
	if (fd < 0)
	{
		if (econt && errno == ENOENT)
		{
			elog(LOG, "Open(logfile %u seg %u) failed: file doesn't exist",
				 logId, logSeg);
			return (fd);
		}
		elog(STOP, "Open(logfile %u seg %u) failed: %d",
			 logId, logSeg, errno);
	}

	return (fd);
}

static XLogRecord *
ReadRecord(XLogRecPtr *RecPtr, char *buffer)
{
	XLogRecord *record;
	XLogRecPtr	tmpRecPtr = EndRecPtr;
	bool		nextmode = (RecPtr == NULL);
	int			emode = (nextmode) ? LOG : STOP;
	bool		noBlck = false;

	if (nextmode)
	{
		RecPtr = &tmpRecPtr;
		if (nextRecord != NULL)
		{
			record = nextRecord;
			goto got_record;
		}
		if (tmpRecPtr.xrecoff % BLCKSZ != 0)
			tmpRecPtr.xrecoff += (BLCKSZ - tmpRecPtr.xrecoff % BLCKSZ);
		if (tmpRecPtr.xrecoff >= XLogFileSize)
		{
			(tmpRecPtr.xlogid)++;
			tmpRecPtr.xrecoff = 0;
		}
		tmpRecPtr.xrecoff += SizeOfXLogPHD;
	}
	else if (!XRecOffIsValid(RecPtr->xrecoff))
		elog(STOP, "ReadRecord: invalid record offset in (%u, %u)",
			 RecPtr->xlogid, RecPtr->xrecoff);

	if (readFile >= 0 && (RecPtr->xlogid != readId ||
						  RecPtr->xrecoff / XLogSegSize != readSeg))
	{
		close(readFile);
		readFile = -1;
	}
	readId = RecPtr->xlogid;
	readSeg = RecPtr->xrecoff / XLogSegSize;
	if (readFile < 0)
	{
		noBlck = true;
		readFile = XLogFileOpen(readId, readSeg, nextmode);
		if (readFile < 0)
			goto next_record_is_invalid;
	}

	if (noBlck || readOff != (RecPtr->xrecoff % XLogSegSize) / BLCKSZ)
	{
		readOff = (RecPtr->xrecoff % XLogSegSize) / BLCKSZ;
		if (lseek(readFile, (off_t) (readOff * BLCKSZ), SEEK_SET) < 0)
			elog(STOP, "ReadRecord: lseek(logfile %u seg %u off %u) failed: %d",
				 readId, readSeg, readOff, errno);
		if (read(readFile, readBuf, BLCKSZ) != BLCKSZ)
			elog(STOP, "ReadRecord: read(logfile %u seg %u off %u) failed: %d",
				 readId, readSeg, readOff, errno);
		if (((XLogPageHeader) readBuf)->xlp_magic != XLOG_PAGE_MAGIC)
		{
			elog(emode, "ReadRecord: invalid magic number %u in logfile %u seg %u off %u",
				 ((XLogPageHeader) readBuf)->xlp_magic,
				 readId, readSeg, readOff);
			goto next_record_is_invalid;
		}
	}
	if ((((XLogPageHeader) readBuf)->xlp_info & XLP_FIRST_IS_SUBRECORD) &&
		RecPtr->xrecoff % BLCKSZ == SizeOfXLogPHD)
	{
		elog(emode, "ReadRecord: subrecord is requested by (%u, %u)",
			 RecPtr->xlogid, RecPtr->xrecoff);
		goto next_record_is_invalid;
	}
	record = (XLogRecord *) ((char *) readBuf + RecPtr->xrecoff % BLCKSZ);

got_record:;
	if (record->xl_len == 0 || record->xl_len >
		(BLCKSZ - RecPtr->xrecoff % BLCKSZ - SizeOfXLogRecord))
	{
		elog(emode, "ReadRecord: invalid record len %u in (%u, %u)",
			 record->xl_len, RecPtr->xlogid, RecPtr->xrecoff);
		goto next_record_is_invalid;
	}
	if (record->xl_rmid > RM_MAX_ID)
	{
		elog(emode, "ReadRecord: invalid resource managed id %u in (%u, %u)",
			 record->xl_rmid, RecPtr->xlogid, RecPtr->xrecoff);
		goto next_record_is_invalid;
	}
	nextRecord = NULL;
	if (record->xl_info & XLR_TO_BE_CONTINUED)
	{
		XLogSubRecord *subrecord;
		uint32		len = record->xl_len;

		if (record->xl_len + RecPtr->xrecoff % BLCKSZ + SizeOfXLogRecord != BLCKSZ)
		{
			elog(emode, "ReadRecord: invalid fragmented record len %u in (%u, %u)",
				 record->xl_len, RecPtr->xlogid, RecPtr->xrecoff);
			goto next_record_is_invalid;
		}
		memcpy(buffer, record, record->xl_len + SizeOfXLogRecord);
		record = (XLogRecord *) buffer;
		buffer += record->xl_len + SizeOfXLogRecord;
		for (;;)
		{
			readOff++;
			if (readOff == XLogSegSize / BLCKSZ)
			{
				readSeg++;
				if (readSeg == XLogLastSeg)
				{
					readSeg = 0;
					readId++;
				}
				close(readFile);
				readOff = 0;
				readFile = XLogFileOpen(readId, readSeg, nextmode);
				if (readFile < 0)
					goto next_record_is_invalid;
			}
			if (read(readFile, readBuf, BLCKSZ) != BLCKSZ)
				elog(STOP, "ReadRecord: read(logfile %u seg %u off %u) failed: %d",
					 readId, readSeg, readOff, errno);
			if (((XLogPageHeader) readBuf)->xlp_magic != XLOG_PAGE_MAGIC)
			{
				elog(emode, "ReadRecord: invalid magic number %u in logfile %u seg %u off %u",
					 ((XLogPageHeader) readBuf)->xlp_magic,
					 readId, readSeg, readOff);
				goto next_record_is_invalid;
			}
			if (!(((XLogPageHeader) readBuf)->xlp_info & XLP_FIRST_IS_SUBRECORD))
			{
				elog(emode, "ReadRecord: there is no subrecord flag in logfile %u seg %u off %u",
					 readId, readSeg, readOff);
				goto next_record_is_invalid;
			}
			subrecord = (XLogSubRecord *) ((char *) readBuf + SizeOfXLogPHD);
			if (subrecord->xl_len == 0 || subrecord->xl_len >
				(BLCKSZ - SizeOfXLogPHD - SizeOfXLogSubRecord))
			{
				elog(emode, "ReadRecord: invalid subrecord len %u in logfile %u seg %u off %u",
					 subrecord->xl_len, readId, readSeg, readOff);
				goto next_record_is_invalid;
			}
			len += subrecord->xl_len;
			if (len > MAXLOGRECSZ)
			{
				elog(emode, "ReadRecord: too long record len %u in (%u, %u)",
					 len, RecPtr->xlogid, RecPtr->xrecoff);
				goto next_record_is_invalid;
			}
			memcpy(buffer, (char *) subrecord + SizeOfXLogSubRecord, subrecord->xl_len);
			buffer += subrecord->xl_len;
			if (subrecord->xl_info & XLR_TO_BE_CONTINUED)
			{
				if (subrecord->xl_len +
					SizeOfXLogPHD + SizeOfXLogSubRecord != BLCKSZ)
				{
					elog(emode, "ReadRecord: invalid fragmented subrecord len %u in logfile %u seg %u off %u",
						 subrecord->xl_len, readId, readSeg, readOff);
					goto next_record_is_invalid;
				}
				continue;
			}
			break;
		}
		if (BLCKSZ - SizeOfXLogRecord >=
			subrecord->xl_len + SizeOfXLogPHD + SizeOfXLogSubRecord)
		{
			nextRecord = (XLogRecord *)
				((char *) subrecord + subrecord->xl_len + SizeOfXLogSubRecord);
		}
		EndRecPtr.xlogid = readId;
		EndRecPtr.xrecoff = readSeg * XLogSegSize + readOff * BLCKSZ +
			SizeOfXLogPHD + SizeOfXLogSubRecord + subrecord->xl_len;
		ReadRecPtr = *RecPtr;
		return (record);
	}
	if (BLCKSZ - SizeOfXLogRecord >=
		record->xl_len + RecPtr->xrecoff % BLCKSZ + SizeOfXLogRecord)
		nextRecord = (XLogRecord *) ((char *) record + record->xl_len + SizeOfXLogRecord);
	EndRecPtr.xlogid = RecPtr->xlogid;
	EndRecPtr.xrecoff = RecPtr->xrecoff + record->xl_len + SizeOfXLogRecord;
	ReadRecPtr = *RecPtr;

	return (record);

next_record_is_invalid:;
	close(readFile);
	readFile = -1;
	nextRecord = NULL;
	memset(buffer, 0, SizeOfXLogRecord);
	record = (XLogRecord *) buffer;

	/*
	 * If we assumed that next record began on the same page where
	 * previous one ended - zero end of page.
	 */
	if (XLByteEQ(tmpRecPtr, EndRecPtr))
	{
		Assert(EndRecPtr.xrecoff % BLCKSZ > (SizeOfXLogPHD + SizeOfXLogSubRecord) &&
			   BLCKSZ - EndRecPtr.xrecoff % BLCKSZ >= SizeOfXLogRecord);
		readId = EndRecPtr.xlogid;
		readSeg = EndRecPtr.xrecoff / XLogSegSize;
		readOff = (EndRecPtr.xrecoff % XLogSegSize) / BLCKSZ;
		elog(LOG, "Formatting logfile %u seg %u block %u at offset %u",
			 readId, readSeg, readOff, EndRecPtr.xrecoff % BLCKSZ);
		readFile = XLogFileOpen(readId, readSeg, false);
		if (lseek(readFile, (off_t) (readOff * BLCKSZ), SEEK_SET) < 0)
			elog(STOP, "ReadRecord: lseek(logfile %u seg %u off %u) failed: %d",
				 readId, readSeg, readOff, errno);
		if (read(readFile, readBuf, BLCKSZ) != BLCKSZ)
			elog(STOP, "ReadRecord: read(logfile %u seg %u off %u) failed: %d",
				 readId, readSeg, readOff, errno);
		memset(readBuf + EndRecPtr.xrecoff % BLCKSZ, 0,
			   BLCKSZ - EndRecPtr.xrecoff % BLCKSZ);
		if (lseek(readFile, (off_t) (readOff * BLCKSZ), SEEK_SET) < 0)
			elog(STOP, "ReadRecord: lseek(logfile %u seg %u off %u) failed: %d",
				 readId, readSeg, readOff, errno);
		if (write(readFile, readBuf, BLCKSZ) != BLCKSZ)
			elog(STOP, "ReadRecord: write(logfile %u seg %u off %u) failed: %d",
				 readId, readSeg, readOff, errno);
		readOff++;
	}
	else
	{
		Assert(EndRecPtr.xrecoff % BLCKSZ == 0 ||
			   BLCKSZ - EndRecPtr.xrecoff % BLCKSZ < SizeOfXLogRecord);
		readId = tmpRecPtr.xlogid;
		readSeg = tmpRecPtr.xrecoff / XLogSegSize;
		readOff = (tmpRecPtr.xrecoff % XLogSegSize) / BLCKSZ;
		Assert(readOff > 0);
	}
	if (readOff > 0)
	{
		if (!XLByteEQ(tmpRecPtr, EndRecPtr))
			elog(LOG, "Formatting logfile %u seg %u block %u at offset 0",
				 readId, readSeg, readOff);
		readOff *= BLCKSZ;
		memset(readBuf, 0, BLCKSZ);
		readFile = XLogFileOpen(readId, readSeg, false);
		if (lseek(readFile, (off_t) readOff, SEEK_SET) < 0)
			elog(STOP, "ReadRecord: lseek(logfile %u seg %u off %u) failed: %d",
				 readId, readSeg, readOff, errno);
		while (readOff < XLogSegSize)
		{
			if (write(readFile, readBuf, BLCKSZ) != BLCKSZ)
				elog(STOP, "ReadRecord: write(logfile %u seg %u off %u) failed: %d",
					 readId, readSeg, readOff, errno);
			readOff += BLCKSZ;
		}
	}
	if (readFile >= 0)
	{
		if (fsync(readFile) < 0)
			elog(STOP, "ReadRecord: fsync(logfile %u seg %u) failed: %d",
				 readId, readSeg, errno);
		close(readFile);
		readFile = -1;
	}

	readId = EndRecPtr.xlogid;
	readSeg = (EndRecPtr.xrecoff - 1) / XLogSegSize + 1;
	elog(LOG, "The last logId/logSeg is (%u, %u)", readId, readSeg - 1);
	if (ControlFile->logId != readId || ControlFile->logSeg != readSeg)
	{
		elog(LOG, "Set logId/logSeg in control file");
		ControlFile->logId = readId;
		ControlFile->logSeg = readSeg;
		ControlFile->time = time(NULL);
		UpdateControlFile();
	}
	if (readSeg == XLogLastSeg)
	{
		readSeg = 0;
		readId++;
	}
	{
		char		path[MAXPGPATH];

		XLogFileName(path, readId, readSeg);
		unlink(path);
	}

	return (record);
}

void
UpdateControlFile()
{
	int			fd;

tryAgain:
#ifndef __CYGWIN__
	fd = open(ControlFilePath, O_RDWR);
#else
	fd = open(ControlFilePath, O_RDWR | O_BINARY);
#endif
	if (fd < 0 && (errno == EMFILE || errno == ENFILE))
	{
		fd = errno;
		if (!ReleaseDataFile())
			elog(STOP, "Open(cntlfile) failed: %d (and no one data file can be closed)",
				 fd);
		goto tryAgain;
	}
	if (fd < 0)
		elog(STOP, "Open(cntlfile) failed: %d", errno);

	if (write(fd, ControlFile, BLCKSZ) != BLCKSZ)
		elog(STOP, "Write(cntlfile) failed: %d", errno);

	if (fsync(fd) != 0)
		elog(STOP, "Fsync(cntlfile) failed: %d", errno);

	close(fd);

	return;
}

size_t
XLOGShmemSize()
{
	if (XLOGbuffers < MinXLOGbuffers)
		XLOGbuffers = MinXLOGbuffers;

	return (sizeof(XLogCtlData) + BLCKSZ * XLOGbuffers +
			sizeof(XLogRecPtr) * XLOGbuffers + BLCKSZ);
}

void
XLOGShmemInit(void)
{
	bool		found;

	if (XLOGbuffers < MinXLOGbuffers)
		XLOGbuffers = MinXLOGbuffers;

	ControlFile = (ControlFileData *)
		ShmemInitStruct("Control File", BLCKSZ, &found);
	Assert(!found);
	XLogCtl = (XLogCtlData *)
		ShmemInitStruct("XLOG Ctl", sizeof(XLogCtlData) + BLCKSZ * XLOGbuffers +
						sizeof(XLogRecPtr) * XLOGbuffers, &found);
	Assert(!found);
}

/*
 * This func must be called ONCE on system install
 */
void
BootStrapXLOG()
{
	int			fd;
	union master {
            double              alignment;
            char		buffer[BLCKSZ];
        }   master;
	CheckPoint	checkPoint;
        char*           buffer = master.buffer;

#ifdef NOT_USED
	XLogPageHeader page = (XLogPageHeader) buffer;
	XLogRecord *record;

#endif

#ifndef __CYGWIN__
	fd = open(ControlFilePath, O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
#else
	fd = open(ControlFilePath, O_RDWR | O_CREAT | O_EXCL | O_BINARY, S_IRUSR | S_IWUSR);
#endif
	if (fd < 0)
		elog(STOP, "BootStrapXLOG failed to create control file (%s): %d",
			 ControlFilePath, errno);

	checkPoint.redo.xlogid = 0;
	checkPoint.redo.xrecoff = SizeOfXLogPHD;
	checkPoint.undo = checkPoint.redo;
	checkPoint.nextXid = FirstTransactionId;
	checkPoint.nextOid = BootstrapObjectIdData;

#ifdef NOT_USED

	memset(buffer, 0, BLCKSZ);
	page->xlp_magic = XLOG_PAGE_MAGIC;
	page->xlp_info = 0;
	record = (XLogRecord *) ((char *) page + SizeOfXLogPHD);
	record->xl_prev.xlogid = 0;
	record->xl_prev.xrecoff = 0;
	record->xl_xact_prev = record->xl_prev;
	record->xl_xid = InvalidTransactionId;
	record->xl_len = sizeof(checkPoint);
	record->xl_info = 0;
	record->xl_rmid = RM_XLOG_ID;
	memcpy((char *) record + SizeOfXLogRecord, &checkPoint, sizeof(checkPoint));

	logFile = XLogFileInit(0, 0);

	if (write(logFile, buffer, BLCKSZ) != BLCKSZ)
		elog(STOP, "BootStrapXLOG failed to write logfile: %d", errno);

	if (fsync(logFile) != 0)
		elog(STOP, "BootStrapXLOG failed to fsync logfile: %d", errno);

	close(logFile);
	logFile = -1;

#endif
        
	memset(buffer, 0, BLCKSZ);
	ControlFile = (ControlFileData *) buffer;
	ControlFile->logId = 0;
	ControlFile->logSeg = 1;
	ControlFile->checkPoint = checkPoint.redo;
	ControlFile->time = time(NULL);
	ControlFile->state = DB_SHUTDOWNED;
	ControlFile->blcksz = BLCKSZ;
	ControlFile->relseg_size = RELSEG_SIZE;
	ControlFile->catalog_version_no = CATALOG_VERSION_NO;

	if (write(fd, buffer, BLCKSZ) != BLCKSZ)
		elog(STOP, "BootStrapXLOG failed to write control file: %d", errno);

	if (fsync(fd) != 0)
		elog(STOP, "BootStrapXLOG failed to fsync control file: %d", errno);
	
	elog(DEBUG,"BLCKSZ set to %d",ControlFile->blcksz);
	close(fd);
}

static char *
str_time(time_t tnow)
{
	char	   *result = ctime(&tnow);
	char	   *p = strchr(result, '\n');

	if (p != NULL)
		*p = 0;

	return (result);
}

/*
 * This func must be called ONCE on system startup
 */
void
StartupXLOG()
{
	int			fd;

	elog(LOG, "Data Base System is starting up at %s", str_time(time(NULL)));

	XLogCtl->xlblocks = (XLogRecPtr *) (((char *) XLogCtl) + sizeof(XLogCtlData));
	XLogCtl->pages = ((char *) XLogCtl->xlblocks + sizeof(XLogRecPtr) * XLOGbuffers);
	XLogCtl->XLogCacheByte = BLCKSZ * XLOGbuffers;
	XLogCtl->XLogCacheBlck = XLOGbuffers - 1;
	memset(XLogCtl->xlblocks, 0, sizeof(XLogRecPtr) * XLOGbuffers);
	XLogCtl->LgwrRqst = LgwrRqst;
	XLogCtl->LgwrResult = LgwrResult;
	XLogCtl->Insert.LgwrResult = LgwrResult;
	XLogCtl->Insert.curridx = 0;
	XLogCtl->Insert.currpage = (XLogPageHeader) (XLogCtl->pages);
	XLogCtl->Write.LgwrResult = LgwrResult;
	XLogCtl->Write.curridx = 0;
	S_INIT_LOCK(&(XLogCtl->insert_lck));
	S_INIT_LOCK(&(XLogCtl->info_lck));
	S_INIT_LOCK(&(XLogCtl->lgwr_lck));

	/*
	 * Open/read Control file
	 */
tryAgain:
#ifndef __CYGWIN__
	fd = open(ControlFilePath, O_RDWR);
#else
	fd = open(ControlFilePath, O_RDWR | O_BINARY);
#endif
	if (fd < 0 && (errno == EMFILE || errno == ENFILE))
	{
		fd = errno;
		if (!ReleaseDataFile())
			elog(STOP, "Open(\"%s\") failed: %d (and no one data file can be closed)",
				 ControlFilePath, fd);
		goto tryAgain;
	}
	if (fd < 0)
		elog(STOP, "Open(\"%s\") failed: %d", ControlFilePath, errno);

	if (read(fd, ControlFile, BLCKSZ) != BLCKSZ)
		elog(STOP, "Read(\"%s\") failed: %d backend BLCKSZ may not match database", ControlFilePath, errno);

	close(fd);

	if (ControlFile->logSeg == 0 ||
		ControlFile->time <= 0 ||
		ControlFile->state < DB_SHUTDOWNED ||
		ControlFile->state > DB_IN_PRODUCTION ||
		!XRecOffIsValid(ControlFile->checkPoint.xrecoff))
		elog(STOP, "Control file context is broken");

	/* Check for incompatible database */
	if (ControlFile->blcksz != BLCKSZ)
		elog(STOP, "database was initialized with BLCKSZ %d,\n\tbut the backend was compiled with BLCKSZ %d.\n\tlooks like you need to initdb.",
			 ControlFile->blcksz, BLCKSZ);
#ifndef LET_OS_MANAGE_FILESIZE
	if (ControlFile->relseg_size != RELSEG_SIZE)
		elog(STOP, "database was initialized with RELSEG_SIZE %d,\n\tbut the backend was compiled with RELSEG_SIZE %d.\n\tlooks like you need to initdb.",
			 ControlFile->relseg_size, RELSEG_SIZE);
#endif
	if (ControlFile->catalog_version_no != CATALOG_VERSION_NO)
		elog(STOP, "database was initialized with CATALOG_VERSION_NO %d,\n\tbut the backend was compiled with CATALOG_VERSION_NO %d.\n\tlooks like you need to initdb.",
			 ControlFile->catalog_version_no, CATALOG_VERSION_NO);

	if (ControlFile->state == DB_SHUTDOWNED)
		elog(LOG, "Data Base System was shut down at %s",
			 str_time(ControlFile->time));
	else if (ControlFile->state == DB_SHUTDOWNING)
		elog(LOG, "Data Base System was interrupted when shutting down at %s",
			 str_time(ControlFile->time));
	else if (ControlFile->state == DB_IN_RECOVERY)
	{
		elog(LOG, "Data Base System was interrupted being in recovery at %s\n"
			 "\tThis propably means that some data blocks are corrupted\n"
			 "\tAnd you will have to use last backup for recovery",
			 str_time(ControlFile->time));
	}
	else if (ControlFile->state == DB_IN_PRODUCTION) {
		elog(LOG, "Data Base System was interrupted being in production at %s",
			 str_time(ControlFile->time));
			 
		SetRecoveryCheckingEnabled(true);
	}

#ifdef NOT_USED

	LastRec = RecPtr = ControlFile->checkPoint;
	if (!XRecOffIsValid(RecPtr.xrecoff))
		elog(STOP, "Invalid checkPoint in control file");
	elog(LOG, "CheckPoint record at (%u, %u)", RecPtr.xlogid, RecPtr.xrecoff);

	record = ReadRecord(&RecPtr, buffer);
	if (record->xl_rmid != RM_XLOG_ID)
		elog(STOP, "Invalid RMID in checkPoint record");
	if (record->xl_len != sizeof(checkPoint))
		elog(STOP, "Invalid length of checkPoint record");
	checkPoint = *((CheckPoint *) ((char *) record + SizeOfXLogRecord));

	elog(LOG, "Redo record at (%u, %u); Undo record at (%u, %u)",
		 checkPoint.redo.xlogid, checkPoint.redo.xrecoff,
		 checkPoint.undo.xlogid, checkPoint.undo.xrecoff);
	elog(LOG, "NextTransactionId: %u; NextOid: %u",
		 checkPoint.nextXid, checkPoint.nextOid);
	if (checkPoint.nextXid < FirstTransactionId ||
		checkPoint.nextOid < BootstrapObjectIdData)
#ifdef XLOG
		elog(STOP, "Invalid NextTransactionId/NextOid");
#else
		elog(LOG, "Invalid NextTransactionId/NextOid");
#endif

#ifdef XLOG
	ShmemVariableCache->nextXid = checkPoint.nextXid;
	ShmemVariableCache->nextOid = checkPoint.nextOid;
#endif

	if (XLByteLT(RecPtr, checkPoint.redo))
		elog(STOP, "Invalid redo in checkPoint record");
	if (checkPoint.undo.xrecoff == 0)
		checkPoint.undo = RecPtr;
	if (XLByteLT(RecPtr, checkPoint.undo))
		elog(STOP, "Invalid undo in checkPoint record");

	if (XLByteLT(checkPoint.undo, RecPtr) || XLByteLT(checkPoint.redo, RecPtr))
	{
		if (ControlFile->state == DB_SHUTDOWNED)
			elog(STOP, "Invalid Redo/Undo record in Shutdowned state");
		recovery = 2;
	}
	else if (ControlFile->state != DB_SHUTDOWNED)
		recovery = 2;

	if (recovery > 0)
	{
		elog(LOG, "The DataBase system was not properly shut down\n"
			 "\tAutomatic recovery is in progress...");
		ControlFile->state = DB_IN_RECOVERY;
		ControlFile->time = time(NULL);
		UpdateControlFile();

		sie_saved = env->StopIfError;
		env->StopIfError = true;

		/* Is REDO required ? */
		if (XLByteLT(checkPoint.redo, RecPtr))
			record = ReadRecord(&(checkPoint.redo), buffer);
		else
/* read past CheckPoint record */
			record = ReadRecord(NULL, buffer);

		/* REDO */
		if (record->xl_len != 0)
		{
			elog(LOG, "Redo starts at (%u, %u)",
				 ReadRecPtr.xlogid, ReadRecPtr.xrecoff);
			do
			{
#ifdef XLOG
				if (record->xl_xid >= ShmemVariableCache->nextXid)
					ShmemVariableCache->nextXid = record->xl_xid + 1;
#endif
				RmgrTable[record->xl_rmid].rm_redo(EndRecPtr, record);
				record = ReadRecord(NULL, buffer);
			} while (record->xl_len != 0);
			elog(LOG, "Redo done at (%u, %u)",
				 ReadRecPtr.xlogid, ReadRecPtr.xrecoff);
			LastRec = ReadRecPtr;
		}
		else
		{
			elog(LOG, "Redo is not required");
			recovery--;
		}

		/* UNDO */
		RecPtr = ReadRecPtr;
		if (XLByteLT(checkPoint.undo, RecPtr))
		{
			elog(LOG, "Undo starts at (%u, %u)",
				 RecPtr.xlogid, RecPtr.xrecoff);
			do
			{
				record = ReadRecord(&RecPtr, buffer);
				if (TransactionIdIsValid(&record->xl_xid) &&
					!TransactionIdDidCommit(&record->xl_xid))
					RmgrTable[record->xl_rmid].rm_undo(record);
				RecPtr = record->xl_prev;
			} while (XLByteLE(checkPoint.undo, RecPtr));
			elog(LOG, "Undo done at (%u, %u)",
				 ReadRecPtr.xlogid, ReadRecPtr.xrecoff);
		}
		else
		{
			elog(LOG, "Undo is not required");
			recovery--;
		}
	}

	/* Init xlog buffer cache */
	record = ReadRecord(&LastRec, buffer);
	logId = EndRecPtr.xlogid;
	logSeg = (EndRecPtr.xrecoff - 1) / XLogSegSize;
	logOff = 0;
	logFile = XLogFileOpen(logId, logSeg, false);
	XLogCtl->xlblocks[0].xlogid = logId;
	XLogCtl->xlblocks[0].xrecoff =
		((EndRecPtr.xrecoff - 1) / BLCKSZ + 1) * BLCKSZ;
	Insert = &XLogCtl->Insert;
	memcpy((char *) (Insert->currpage), readBuf, BLCKSZ);
	Insert->currpos = ((char *) Insert->currpage) +
		(EndRecPtr.xrecoff + BLCKSZ - XLogCtl->xlblocks[0].xrecoff);
	Insert->PrevRecord = ControlFile->checkPoint;

	if (recovery > 0)
	{
		int			i;

		/*
		 * Let resource managers know that recovery is done
		 */
		for (i = 0; i <= RM_MAX_ID; i++)
			RmgrTable[record->xl_rmid].rm_redo(ReadRecPtr, NULL);
		CreateCheckPoint(true);
		env->StopIfError = sie_saved;
	}

#endif	 /* NOT_USED */

	ControlFile->state = DB_IN_PRODUCTION;
	ControlFile->time = time(NULL);
	UpdateControlFile();

	elog(LOG, "Data Base System is in production state at %s", str_time(time(NULL)));

	return;
}

/*
 * This func must be called ONCE on system shutdown
 */
void
ShutdownXLOG()
{

	elog(LOG, "Data Base System shutting down at %s", str_time(time(NULL)));

	CreateCheckPoint(true);

	elog(LOG, "Data Base System shut down at %s", str_time(time(NULL)));
}

void
CreateCheckPoint(bool shutdown)
{
#ifdef NOT_USED
	CheckPoint	checkPoint;
	XLogRecPtr	recptr;
	XLogCtlInsert *Insert = &XLogCtl->Insert;
	uint32		freespace;
	uint16		curridx;

	memset(&checkPoint, 0, sizeof(checkPoint));
	if (shutdown)
	{
		ControlFile->state = DB_SHUTDOWNING;
		ControlFile->time = time(NULL);
		UpdateControlFile();
	}

	/* Get REDO record ptr */
	while (TAS(&(XLogCtl->insert_lck)))
	{
		struct timeval delay = {0, 5000};

		if (shutdown)
			elog(STOP, "XLog insert lock is busy while data base is shutting down");
		(void) select(0, NULL, NULL, NULL, &delay);
	}
	freespace = ((char *) Insert->currpage) + BLCKSZ - Insert->currpos;
	if (freespace < SizeOfXLogRecord)
	{
		curridx = NextBufIdx(Insert->curridx);
		if (XLByteLE(XLogCtl->xlblocks[curridx], LgwrResult.Write))
			InitXLBuffer(curridx);
		else
			GetFreeXLBuffer();
		freespace = BLCKSZ - SizeOfXLogPHD;
	}
	else
		curridx = Insert->curridx;
	checkPoint.redo.xlogid = XLogCtl->xlblocks[curridx].xlogid;
	checkPoint.redo.xrecoff = XLogCtl->xlblocks[curridx].xrecoff - BLCKSZ +
		Insert->currpos - ((char *) Insert->currpage);
	S_UNLOCK(&(XLogCtl->insert_lck));

	SpinAcquire(XidGenLockId);
	checkPoint.nextXid = ShmemVariableCache->nextXid;
	SpinRelease(XidGenLockId);
	SpinAcquire(OidGenLockId);
	checkPoint.nextOid = ShmemVariableCache->nextOid;
	SpinRelease(OidGenLockId);

	/* Get UNDO record ptr */
	checkPoint.undo.xrecoff = 0;

	if (shutdown && checkPoint.undo.xrecoff != 0)
		elog(STOP, "Active transaction while data base is shutting down");

	recptr = XLogInsert(RM_XLOG_ID, (char *) &checkPoint, sizeof(checkPoint), NULL, 0);

	if (shutdown && !XLByteEQ(checkPoint.redo, env->MyLastRecPtr))
		elog(STOP, "XLog concurrent activity while data base is shutting down");

	XLogFlush(recptr);

#endif	 /* NOT_USED */

	SpinAcquire(ControlFileLockId);
	if (shutdown)
		ControlFile->state = DB_SHUTDOWNED;

#ifdef NOT_USED
	ControlFile->checkPoint = env->MyLastRecPtr;
#else
	ControlFile->checkPoint.xlogid = 0;
	ControlFile->checkPoint.xrecoff = SizeOfXLogPHD;
#endif

	ControlFile->time = time(NULL);
	UpdateControlFile();
	SpinRelease(ControlFileLockId);

	return;
}


XlogGlobals*
GetXlogInfo(void)
{
    XlogGlobals* info = xlog_globals;
    if ( info == NULL ) {
        info = AllocateEnvSpace(xlog_id,sizeof(XlogGlobals));
    }
    return info;
}

