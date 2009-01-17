/*-------------------------------------------------------------------------
 *
 * smgr.h
 *	  storage manager switch public interface declarations.
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * $Id: smgr.h,v 1.1.1.1 2006/08/12 00:22:25 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */
#ifndef SMGR_H
#define SMGR_H

#include "storage/block.h"
#include "storage/spin.h"
#include "nodes/pg_list.h"

#define SM_FAIL			0
#define SM_SUCCESS		1

#define DEFAULT_SMGR	0

typedef struct recoveredpage {
    Oid          dbid;
    Oid          relid;
    BlockNumber  block;
    char         dbname[64];
} RecoveredPage;

typedef struct smgrdata* SmgrInfo;

PG_EXTERN int	smgrinit(void);
PG_EXTERN int	smgrshutdown(void);
PG_EXTERN SmgrInfo  smgrcreate(int16 which, char *dbname, char *relname, 
			  Oid dbid, Oid relid);
PG_EXTERN int	smgrunlink(SmgrInfo info);
PG_EXTERN long	smgrextend(SmgrInfo info, char *buffer, long count);
PG_EXTERN SmgrInfo  smgropen(int16 which, char *dbname, char *relname,
			  Oid dbid, Oid relid);
PG_EXTERN int	smgrclose(SmgrInfo info);
PG_EXTERN int smgrread(SmgrInfo info, BlockNumber blocknum,
		 char *buffer);
PG_EXTERN int smgrwrite(SmgrInfo info, BlockNumber blocknum,
		  char *buffer);
PG_EXTERN int smgrflush(SmgrInfo info, BlockNumber blocknum,
		  char *buffer);
PG_EXTERN int	smgrmarkdirty(SmgrInfo info, BlockNumber blkno);
PG_EXTERN long	smgrnblocks(SmgrInfo info);
PG_EXTERN long	smgrtruncate(SmgrInfo info, long nblocks);
PG_EXTERN int	smgrsync(SmgrInfo info);
PG_EXTERN int	smgrcommit(void);
PG_EXTERN int	smgrabort(void);

PG_EXTERN int   smgrbeginlog();
PG_EXTERN int   smgrlog(int which, char *dbname, char *relname,
	Oid dbid, Oid relid, BlockNumber blocknum, char relkind, char* buffer);
PG_EXTERN int   smgrcommitlog();

PG_EXTERN int   smgrexpirelogs();
PG_EXTERN int   smgrreplaylogs();

PG_EXTERN void smgrcompleterecovery();
PG_EXTERN List* smgrgetrecoveredlist(Oid dbid);
PG_EXTERN List* smgrdbrecoverylist(void);
PG_EXTERN char* smgrdbrecoveryname(Oid dbid);
/* internals: move me elsewhere -- ay 7/94 */
#ifdef MMD
/* in mmd.c */
PG_EXTERN int	mmdinit(void);
PG_EXTERN int	mmdshutdown(void);
PG_EXTERN int	mmdcreate(SmgrInfo info);
PG_EXTERN int	mmdunlink(SmgrInfo info);
PG_EXTERN int	mmdextend(SmgrInfo info, char *buffer);
PG_EXTERN int	mmdopen(SmgrInfo info);
PG_EXTERN int	mmdclose(SmgrInfo info);
PG_EXTERN int	mmdread(SmgrInfo info, BlockNumber blocknum, char *buffer);
PG_EXTERN int	mmdwrite(SmgrInfo info, BlockNumber blocknum, char *buffer);
PG_EXTERN int	mmdflush(SmgrInfo info, BlockNumber blocknum, char *buffer);
PG_EXTERN int	mmdmarkdirty(SmgrInfo info, BlockNumber blkno);
PG_EXTERN int	mmdnblocks(SmgrInfo info);
PG_EXTERN int	mmdtruncate(SmgrInfo info, long nblocks);
PG_EXTERN int	mmdsync(SmgrInfo info);
PG_EXTERN int	mmdcommit(void);
PG_EXTERN int	mmdabort(void);
#endif

/* in vfd.c */
PG_EXTERN int	vfdinit(void);
PG_EXTERN int	vfdshutdown(void);
PG_EXTERN int	vfdcreate(SmgrInfo info);
PG_EXTERN int	vfdunlink(SmgrInfo info);
PG_EXTERN int	vfdextend(SmgrInfo info, char *buffer, long count);
PG_EXTERN int	vfdopen(SmgrInfo info);
PG_EXTERN int	vfdclose(SmgrInfo info);
PG_EXTERN int	vfdread(SmgrInfo info, BlockNumber blocknum, char *buffer);
PG_EXTERN int	vfdwrite(SmgrInfo info, BlockNumber blocknum, char *buffer);
PG_EXTERN int	vfdflush(SmgrInfo info, BlockNumber blocknum, char *buffer);
PG_EXTERN int	vfdmarkdirty(SmgrInfo info, BlockNumber blkno);
PG_EXTERN int	vfdnblocks(SmgrInfo info);
PG_EXTERN int	vfdtruncate(SmgrInfo info, long nblocks);
PG_EXTERN int	vfdsync(SmgrInfo info);
PG_EXTERN int	vfdcommit(void);
PG_EXTERN int	vfdabort(void);
PG_EXTERN int	vfdbeginlog(void);
PG_EXTERN int	vfdlog(SmgrInfo info, BlockNumber, char* buffer);
PG_EXTERN int	vfdcommitlog(void);
PG_EXTERN int	vfdexpirelogs(void);
PG_EXTERN int	vfdreplaylogs(void);

#ifdef ZFS
/* in zfs.c */
PG_EXTERN int	zfstest(char* dd);
PG_EXTERN int	zfsinit(void);
PG_EXTERN int	zfsshutdown(void);
PG_EXTERN int	zfscreate(SmgrInfo info);
PG_EXTERN int	zfsunlink(SmgrInfo info);
PG_EXTERN int	zfsextend(SmgrInfo info, char *buffer);
PG_EXTERN int	zfsopen(SmgrInfo info);
PG_EXTERN int	zfsclose(SmgrInfo info);
PG_EXTERN int	zfsread(SmgrInfo info, BlockNumber blocknum, char *buffer);
PG_EXTERN int	zfswrite(SmgrInfo info, BlockNumber blocknum, char *buffer);
PG_EXTERN int	zfsflush(SmgrInfo info, BlockNumber blocknum, char *buffer);
PG_EXTERN int	zfsmarkdirty(SmgrInfo info, BlockNumber blkno);
PG_EXTERN int	zfsnblocks(SmgrInfo info);
PG_EXTERN int	zfstruncate(SmgrInfo info, int nblocks);
PG_EXTERN int	zfssync(SmgrInfo info);
PG_EXTERN int	zfscommit(void);
PG_EXTERN int	zfsabort(void);
#endif
#ifdef STABLE_MEMORY_STORAGE
/* mm.c */
extern SPINLOCK MMCacheLock;

PG_EXTERN int	mminit(void);
PG_EXTERN int	mmcreate(SmgrInfo info);
PG_EXTERN int	mmunlink(SmgrInfo info);
PG_EXTERN int	mmextend(SmgrInfo info, char *buffer);
PG_EXTERN int	mmopen(SmgrInfo info);
PG_EXTERN int	mmclose(SmgrInfo info);
PG_EXTERN int	mmread(SmgrInfo info, BlockNumber blocknum, char *buffer);
PG_EXTERN int	mmwrite(SmgrInfo info, BlockNumber blocknum, char *buffer);
PG_EXTERN int	mmflush(SmgrInfo info, BlockNumber blocknum, char *buffer);
PG_EXTERN int	mmmarkdirty(SmgrInfo info, BlockNumber blkno);
PG_EXTERN int	mmnblocks(SmgrInfo info);
PG_EXTERN int	mmtruncate(SmgrInfo info, int nblocks);
PG_EXTERN int	mmsync(SmgrInfo info);
PG_EXTERN int	mmcommit(void);
PG_EXTERN int	mmabort(void);

PG_EXTERN int	mmshutdown(void);
PG_EXTERN int	MMShmemSize(void);
#endif

/* smgrtype.c */
PG_EXTERN char *smgrout(int2 i);
PG_EXTERN int2 smgrin(char *s);
PG_EXTERN bool smgreq(int2 a, int2 b);
PG_EXTERN bool smgrne(int2 a, int2 b);


#endif	 /* SMGR_H */
