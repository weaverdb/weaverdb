/*-------------------------------------------------------------------------
 *
 * smgr.c
 *	  public interface routines to storage manager switch.
 *
 *	  All file system operations in POSTGRES dispatch through these
 *	  routines.
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
#include "postgres.h"
#include "env/env.h"

#include "storage/smgr.h"
#include "storage/smgr_spi.h"
#include "utils/builtins.h"
#include "miscadmin.h"

/*  made callable from elsewhere
static void smgrshutdown(int dummy);
 */
typedef struct f_smgr {
    int (*smgr_init) (void); /* may be NULL */
    int (*smgr_shutdown) (void); /* may be NULL */
    int (*smgr_create) (SmgrInfo info);
    int (*smgr_unlink) (SmgrInfo info);
    int (*smgr_extend) (SmgrInfo info, char *buffer, int count);
    int (*smgr_open) (SmgrInfo info);
    int (*smgr_close) (SmgrInfo info);
    int (*smgr_read) (SmgrInfo info, BlockNumber blocknum,
            char *buffer);
    int (*smgr_write) (SmgrInfo info, BlockNumber blocknum,
            char *buffer);
    int (*smgr_flush) (SmgrInfo info, BlockNumber blocknum,
            char *buffer);
    int (*smgr_markdirty) (SmgrInfo info, BlockNumber blkno);
    int (*smgr_nblocks) (SmgrInfo info);
    int (*smgr_truncate) (SmgrInfo info, long nblocks);
    int (*smgr_sync) (SmgrInfo info);
    int (*smgr_commit) (void); /* may be NULL */
    int (*smgr_abort) (void); /* may be NULL */
    int (*smgr_beginlog) (void);
    int (*smgr_log) (SmgrInfo info, BlockNumber block, char* buffer);
    int (*smgr_commitlog) (void);
    int (*smgr_expirelogs) (void);
    int (*smgr_replaylogs) (void);
} f_smgr;

/*
 *	The weird placement of commas in this init block is to keep the compiler
 *	happy, regardless of what storage managers we have (or don't have).
 */

static f_smgr smgrsw[] = {
#ifdef MMD
    /* magnetic disk */
    {mmdinit, mmdshutdown, mmdcreate, mmdunlink, mmdextend, mmdopen, mmdclose,
        mmdread, mmdwrite, mmdflush, mmdmarkdirty,
        mmdnblocks, mmdtruncate, mmdsync, mmdcommit, mmdabort},
#endif
    /* direct magnetic disk */
    {vfdinit, vfdshutdown, vfdcreate, vfdunlink, vfdextend, vfdopen, vfdclose,
        vfdread, vfdwrite, vfdflush, vfdmarkdirty,
        vfdnblocks, vfdtruncate, vfdsync, vfdcommit, vfdabort, vfdbeginlog, vfdlog, vfdcommitlog,
        vfdexpirelogs, vfdreplaylogs},
#ifdef ZFS
    /* zfs dmu layer */
    {zfsinit, zfsshutdown, zfscreate, zfsunlink, zfsextend, zfsopen, zfsclose,
        zfsread, zfswrite, zfsflush, zfsmarkdirty,
        zfsnblocks, zfstruncate, zfssync, zfscommit, zfsabort},
#endif
#ifdef STABLE_MEMORY_STORAGE
    /* main memory */
    {mminit, mmshutdown, mmcreate. mmunlink, mmextend, mmopen, mmclose,
        mmread, mmwrite, mmflush, mmmarkdirty,
        mmnblocks, NULL, mmsync, mmcommit, mmabort},

#endif
};

typedef struct SmgrGlobals {
    MemoryContext smgr_cxt;
} SmgrGlobals;

static SectionId thread_id = SECTIONID("SMGR");

/*  Thread local storage for ThreadGlobals  */
#ifdef TLS
TLS SmgrGlobals* smgr_globals = NULL;
#else
#define smgr_globals  GetEnv()->smgr_globals
#endif

static SmgrGlobals* GetSmgrGlobals(void);

static int NSmgr = lengthof(smgrsw);
/*  this list is created at recovery time with memory from 
 *  the main thread's memory pool
 */
static List* recovered;
static MemoryContext recovery_cxt;

static void smgrbeginrecovery(void);

/*
 *	smgrinit(), smgrshutdown() -- Initialize or shut down all storage
 *								  managers.
 *
 */
int
smgrinit() {
    int i;

    for (i = 0; i < NSmgr; i++) {
        if (smgrsw[i].smgr_init) {
            if ((*(smgrsw[i].smgr_init)) () != SM_SUCCESS)
                elog(FATAL, "initialization failed on %s", smgrout(i));
        }
    }

    /* register the shutdown proc */
    /*	on_proc_exit(smgrshutdown, NULL);   */

    return SM_SUCCESS;
}

int
smgrshutdown(void) {
    int i;

    for (i = 0; i < NSmgr; i++) {
        if (smgrsw[i].smgr_shutdown) {
            if ((*(smgrsw[i].smgr_shutdown)) () != SM_SUCCESS)
                elog(FATAL, "shutdown failed on %s", smgrout(i));
        }
    }

    return i;
}

SmgrInfo
smgrcreate(int16 which, char* dbname, char* relname, Oid dbid, Oid relid) {
    int fd;
    SmgrInfo info;

    info = MemoryContextAlloc(GetSmgrGlobals()->smgr_cxt, sizeof (SmgrData));

    info->which = which;
    namestrcpy(&info->relname, relname);
    namestrcpy(&info->dbname, dbname);
    info->relid = relid;
    info->dbid = dbid;

    if ((fd = (*(smgrsw[which].smgr_create)) (info)) < 0) {
        elog(NOTICE, "cannot create %s-%s", relname, dbname);
        pfree(info);
        info = NULL;
    } else {
        info->fd = fd;
    }

    return info;
}

/*
 *	smgrunlink() -- Unlink a relation.
 *
 *		The relation is removed from the store.
 */
int
smgrunlink(SmgrInfo info) {
    IOStatus status;

    if ((status = (*(smgrsw[info->which].smgr_unlink)) (info))  != SM_SUCCESS) {
        elog(NOTICE, "cannot unlink %s-%s code: %d", NameStr(info->relname), NameStr(info->dbname), status);
        status = SM_FAIL;
    }

    pfree(info);

    return status;
}

/*
 *	smgrextend() -- Add a new block to a file.
 *
 *		Returns SM_SUCCESS on success; aborts the current transaction on
 *		failure.
 */
long
smgrextend(SmgrInfo info, char *buffer, int count) {
    int status = (*(smgrsw[info->which].smgr_extend)) (info, buffer, count);

    if (status  != SM_SUCCESS) {
        elog(NOTICE, "%s-%s: cannot extend.  Check free disk space.",
                NameStr(info->relname), NameStr(info->dbname));
        return -1;
    }

    return info->nblocks;
}

/*
 *	smgrblindopen() -- Open a descriptor blindly using a particular storage manager.
 *
 *		Returns the fd for the open relation on success, aborts the
 *		transaction on failure.
 */
SmgrInfo
smgropen(int16 which, char *dbname, char *relname,Oid dbid, Oid relid)
{
        SmgrInfo                info;
        int count               =0;
        
        info = MemoryContextAlloc(GetSmgrGlobals()->smgr_cxt,sizeof(SmgrData));
        
        info->which = which;
        namestrcpy(&info->relname,relname);
        namestrcpy(&info->dbname,dbname);
        info->relid = relid;
        info->dbid = dbid;        
        
	while ((*(smgrsw[which].smgr_open)) (info) != SM_SUCCESS) {
		elog(NOTICE, "cannot open %s-%s", relname, dbname);
                perror("SMGR open:");
                if ( count ++ > 3 ) {
                        pfree(info);
                        info = NULL;
        		elog(ERROR, "cannot open %s-%s", relname, dbname);
                }
        } 
        
        return info;
}

/*
 *	smgrclose() -- Close a relation.
 *
 *		NOTE: underlying manager should allow case where relation is
 *		already closed.  Indeed relation may have been unlinked!
 *		This is currently called only from RelationFlushRelation() when
 *		the relation cache entry is about to be dropped; could be doing
 *		simple relation cache clear, or finishing up DROP TABLE.
 *
 *		Returns SM_SUCCESS on success, aborts on failure.
 */
int
smgrclose(SmgrInfo info) {
    IOStatus status = SM_SUCCESS;
    if ((*(smgrsw[info->which].smgr_close)) (info) != SM_SUCCESS) {
        elog(NOTICE, "cannot close %s-%s", NameStr(info->relname), NameStr(info->dbname));
        status = SM_FAIL;
    }

    pfree(info);

    return SM_SUCCESS;
}

/*
 *	smgrread() -- read a particular block from a relation into the supplied
 *				  buffer.
 *
 *		This routine is called from the buffer manager in order to
 *		instantiate pages in the shared buffer cache.  All storage managers
 *		return pages in the format that POSTGRES expects.  This routine
 *		dispatches the read.  On success, it returns SM_SUCCESS.  On failure,
 *		the current transaction is aborted.
 */
int
smgrread(SmgrInfo info, BlockNumber blocknum, char *buffer) {
    IOStatus status = SM_SUCCESS;

    status = (*(smgrsw[info->which].smgr_read)) (info, blocknum, buffer);

    if (status != SM_SUCCESS) {
        if (status == SM_FAIL_EOF && info->nblocks == blocknum) {
            status = SM_SUCCESS;
        } else {
            elog(NOTICE, "cannot read block %ld of %s-%s code: %d",
                    blocknum, NameStr(info->relname), NameStr(info->dbname), status);
            status = SM_FAIL;
        }
    }

    return status;
}

/*
 *	smgrwrite() -- Write the supplied buffer out.
 *
 *		This is not a synchronous write -- the interface for that is
 *		smgrflush().  The buffer is written out via the appropriate
 *		storage manager.  This routine returns SM_SUCCESS or aborts
 *		the current transaction.
 */
int
smgrwrite(SmgrInfo info, BlockNumber blocknum, char *buffer) {
    IOStatus status = SM_SUCCESS;

    status = (*(smgrsw[info->which].smgr_write)) (info, blocknum, buffer);

    if (status != SM_SUCCESS) {
        elog(NOTICE, "cannot write block %ld of %s-%s",
            blocknum, NameStr(info->relname), NameStr(info->dbname));
        status = SM_FAIL;
    }
    return status;
}

/*
 *	smgrflush() -- A synchronous smgrwrite().
 */
int
smgrflush(SmgrInfo info, BlockNumber blocknum, char *buffer) {
    IOStatus status = SM_SUCCESS;

    status = (*(smgrsw[info->which].smgr_flush)) (info, blocknum, buffer);

    if (status != SM_SUCCESS) {
        elog(NOTICE, "cannot flush block %ld of %s-%s to stable store",
            blocknum, NameStr(info->relname), NameStr(info->dbname));
        status = SM_FAIL;
    }

    return status;
}

/*
 *	smgrmarkdirty() -- Mark a page dirty (needs fsync).
 *
 *		Mark the specified page as needing to be fsync'd before commit.
 *		Ordinarily, the storage manager will do this implicitly during
 *		smgrwrite().  However, the buffer manager may discover that some
 *		other backend has written a buffer that we dirtied in the current
 *		transaction.  In that case, we still need to fsync the file to be
 *		sure the page is down to disk before we commit.
 */
int
smgrmarkdirty(SmgrInfo info, BlockNumber blkno) {
    IOStatus status = SM_SUCCESS;

    status = (*(smgrsw[info->which].smgr_markdirty)) (info, blkno);

    if (status != SM_SUCCESS) {
        elog(NOTICE, "cannot mark block %ld of %s:%s",
            blkno, NameStr(info->relname), NameStr(info->dbname));
        status = SM_FAIL;
    }   

    return status;
}

/*
 *	smgrnblocks() -- Calculate the number of POSTGRES blocks in the
 *					 supplied relation.
 *
 *		Returns the number of blocks on success, aborts the current
 *		transaction on failure.
 */
long
smgrnblocks(SmgrInfo info) {
    if (((*(smgrsw[info->which].smgr_nblocks)) (info)) != SM_SUCCESS)
        elog(NOTICE, "cannot count blocks for %s-%s",
            NameStr(info->relname), NameStr(info->dbname));


    return info->nblocks;
}

/*
 *	smgrtruncate() -- Truncate supplied relation to a specified number
 *						of blocks
 *
 *		Returns the number of blocks on success, aborts the current
 *		transaction on failure.
 */
long
smgrtruncate(SmgrInfo info, long nblocks) {
    long newblks;

    newblks = nblocks;
    if (smgrsw[info->which].smgr_truncate) {
        if ((newblks = (*(smgrsw[info->which].smgr_truncate)) (info, nblocks)) != SM_SUCCESS)
            elog(NOTICE, "cannot truncate %s-%s to %ld blocks",
                NameStr(info->relname), NameStr(info->dbname), nblocks);
    }

    return info->nblocks;
}

/*
 *	smgrcommit(), smgrabort() -- Commit or abort changes made during the
 *								 current transaction.
 */
int
smgrcommit() {
    int i;

    for (i = 0; i < NSmgr; i++) {
        if (smgrsw[i].smgr_commit) {
            if ((*(smgrsw[i].smgr_commit)) () != SM_SUCCESS) {
                elog(FATAL, "transaction commit failed on %s", smgrout(i));
            }
        }
    }

    return SM_SUCCESS;
}

int
smgrabort() {
    int i;

    for (i = 0; i < NSmgr; i++) {
        if (smgrsw[i].smgr_abort) {
            if ((*(smgrsw[i].smgr_abort)) () != SM_SUCCESS) {
                elog(FATAL, "transaction abort failed on %s", smgrout(i));
            }
        }
    }

    return SM_SUCCESS;
}

int
smgrsync(SmgrInfo info) {
    IOStatus status = SM_SUCCESS;
    if (smgrsw[info->which].smgr_sync) {
        if (((*(smgrsw[info->which].smgr_sync)) (info)) != SM_SUCCESS) {
            elog(NOTICE, "cannot sync %s-%s",
                NameStr(info->relname), NameStr(info->dbname));
            status = SM_FAIL;
        }
    }
    return status;
}

int
smgrbeginlog(void) {
    int i;

    for (i = 0; i < NSmgr; i++) {
        if (smgrsw[i].smgr_beginlog) {
            if ((*(smgrsw[i].smgr_beginlog)) () != SM_SUCCESS) {
                elog(FATAL, "begin log failed on %s", smgrout(i));
            }
        }
    }

    return SM_SUCCESS;
}

int
smgrlog(int which, char *dbname, char *relname,
        Oid dbid, Oid relid, BlockNumber number, char relkind, char* buffer) {
    SmgrData data;
    IOStatus status = SM_SUCCESS;

    data.which = which;
    namestrcpy(&data.dbname, dbname);
    namestrcpy(&data.relname, relname);
    data.dbid = dbid;
    data.relid = relid;
    data.relkind = relkind;

    if (smgrsw[which].smgr_log) {
        if ((*(smgrsw[which].smgr_log)) (&data, number, buffer) != SM_SUCCESS) {
            elog(FATAL, "log failed on %s for %s-%s block number: %ld", smgrout(which), NameStr(data.relname),
                NameStr(data.dbname), number);
            status = SM_FAIL;
        }
    }

    return status;
}

int
smgrcommitlog() {
    int i;

    for (i = 0; i < NSmgr; i++) {
        if (smgrsw[i].smgr_commitlog) {
            if ((*(smgrsw[i].smgr_commitlog)) () != SM_SUCCESS)
                elog(FATAL, "commit log failed on %s", smgrout(i));
        }
    }

    return SM_SUCCESS;
}

int
smgrreplaylogs() {
    int i;

    smgrbeginrecovery();

    for (i = 0; i < NSmgr; i++) {
        if (smgrsw[i].smgr_replaylogs) {
            if ((*(smgrsw[i].smgr_replaylogs)) () != SM_SUCCESS)
                elog(FATAL, "replay logs failed on %s", smgrout(i));
        }
    }

    return SM_SUCCESS;
}

int
smgrexpirelogs() {
    int i;

    for (i = 0; i < NSmgr; i++) {
        if (smgrsw[i].smgr_expirelogs) {
            if ((*(smgrsw[i].smgr_expirelogs)) () != SM_SUCCESS)
                elog(FATAL, "expire logs failed on %s", smgrout(i));
        }
    }

    return SM_SUCCESS;
}

int smgraddrecoveredpage(char* dbname, Oid dbid, Oid relid, BlockNumber block) {
    MemoryContext old = MemoryContextSwitchTo(recovery_cxt);
    RecoveredPage* page = palloc(sizeof (RecoveredPage));
    page->dbid = dbid;
    page->relid = relid;
    page->block = block;
    strncpy(page->dbname, dbname, 64);
    recovered = lappend(recovered, page);
    MemoryContextSwitchTo(old);
    return 0;
}

List* smgrgetrecoveredlist(Oid dbid) {
    List* item;
    List* specific = NULL;

    /*
        elog(NOTICE,"generating recovery list");
     */
    if (recovered == NULL) return NULL;

    foreach(item, recovered) {
        RecoveredPage* page = lfirst(item);
        if (page->dbid == dbid) {
            specific = lappend(specific, page);
        }
    }
    /*
        elog(NOTICE,"done generating recovery list");
     */
    return specific;
}

void smgrbeginrecovery() {
    recovery_cxt = AllocSetContextCreate(GetSmgrMemoryContext(),
            "SmgrMemoryContext",
            ALLOCSET_DEFAULT_MINSIZE,
            ALLOCSET_DEFAULT_INITSIZE,
            ALLOCSET_DEFAULT_MAXSIZE);
}

void smgrcompleterecovery() {
    MemoryContextDelete(recovery_cxt);
    recovery_cxt = NULL;
    recovered = NULL;
}

List* smgrdbrecoverylist() {
    List* item;
    List* specific = NULL;

    /*
        elog(NOTICE,"evaluating recovery list");
     */
    if (recovered == NULL) return NULL;

    MemoryContext old = MemoryContextSwitchTo(recovery_cxt);

    foreach(item, recovered) {
        RecoveredPage* page = lfirst(item);
        if (!intMember(page->dbid, specific)) {
            specific = lappendi(specific, page->dbid);
        }
    }
    /*
        elog(NOTICE,"done evaluating recovery list");
     */
    MemoryContextSwitchTo(old);
    return specific;
}

char* smgrdbrecoveryname(Oid dbid) {
    List* item;

    if (recovered == NULL) return NULL;

    foreach(item, recovered) {
        RecoveredPage* page = lfirst(item);
        if (page->dbid == dbid) {
            return page->dbname;
        }
    }

    return NULL;
}

SmgrGlobals*
GetSmgrGlobals(void) {
    SmgrGlobals* globals = smgr_globals;
    if (globals == NULL) {
        globals = AllocateEnvSpace(thread_id, sizeof (SmgrGlobals));
        memset(globals, 0x00, sizeof (SmgrGlobals));
        globals->smgr_cxt = AllocSetContextCreate(GetEnvMemoryContext(),
                "SmgrMemoryContext",
                ALLOCSET_DEFAULT_MINSIZE,
                ALLOCSET_DEFAULT_INITSIZE,
                ALLOCSET_DEFAULT_MAXSIZE);
        smgr_globals = globals;
    }
    return globals;
}

MemoryContext GetSmgrMemoryContext() {
    return GetSmgrGlobals()->smgr_cxt;
}
