/*-------------------------------------------------------------------------
 *
 * zfs.c
 *	  This code manages relations that reside on Sun's ZFS file system.
 *          useing the dmu interface
 *
 * Portions Copyright (c) 2000-2024, Myron Scott  <myron@weaverdb.org>
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *
 *-------------------------------------------------------------------------
 */



#include <errno.h>
#include <unistd.h>
#include <thread.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/dmu.h>
#include <sys/zil.h>
#include <sys/zio.h>
#include <sys/refcount.h>
#include <sys/unique.h>


#include "postgres.h"

#include "catalog/catalog.h"
#include "miscadmin.h"
#include "storage/smgr.h"
#include "utils/inval.h"		/* ImmediateSharedRelationCacheInvalidate()  */
#include "utils/relcache.h"
#include "env/env.h"

#undef DIAGNOSTIC

static SectionId zfs_id = SECTIONID("ZFSD");

objset_t*               dbase;

/* routines declared here */

typedef struct zfsglobals {
    dmu_tx_t*           current_tx;
} ZFSGlobals;

static ZFSGlobals* GetZFSGlobals(void);

static dmu_tx_t* GetZFSTx(bool set);

#define	TXG_WAIT		1ULL
#define	TXG_NOWAIT		2ULL

#define ZFS_OPEN                3;
#define ZFS_CLOSED              -1;

int
zfsinit()
{
    int err = 0;
    refcount_init();
    unique_init();
    zio_init();
    dmu_init();
    zil_init();

    err = dmu_objset_open(DataDir,DMU_OST_ANY,DS_MODE_STANDARD,&dbase);
    printf("error %s %d\n",DataDir,err);

    return SM_SUCCESS;
}

int
zfsshutdown()
{
    int err;
    dmu_objset_close(dbase);
    dmu_fini();
    return SM_SUCCESS;
}

int 
zfstest(char* dd) {
    DataDir = dd;
    zfsinit();
    zfsshutdown();
    return 0;
}

int
zfscreate(Relation reln)
{
        Oid  id = RelationGetRelid(reln);

        dmu_tx_t* tx = GetZFSTx(true);

	int err = dmu_object_claim(dbase, id, DMU_OT_PLAIN_FILE_CONTENTS,
                BLCKSZ, DMU_OT_NONE, 0, tx);

	if (err != 0)
	{
            reln->rd_fd = ZFS_CLOSED;
            perror("ZFS");
            elog(ERROR,"error creating file for relation");
	} else {
            reln->rd_unlinked = false;
            reln->rd_fd = ZFS_OPEN;
        }

	return reln->rd_fd;
}


int
zfsunlink(Relation reln)
{
   dmu_tx_t* tx = GetZFSTx(true);
   Oid  id = RelationGetRelid(reln);
   
   int err = dmu_object_free(dbase,id,tx);

	if (err != 0)
	{
            reln->rd_fd = ZFS_CLOSED;
            perror("ZFS");
            elog(ERROR,"error creating file for relation");
	} else {
            reln->rd_fd = -1;
            reln->rd_unlinked = true;
        }

	return SM_SUCCESS;
}

/*
 *	zfsextend() -- Add a block to the specified relation.
 *
 *		This routine returns SM_FAIL or SM_SUCCESS, with errno set as
 *		appropriate.
 */
int
zfsextend(Relation reln, char *buffer)
{
	uint64_t	nblocks;

    dmu_tx_t* tx = GetZFSTx(true);
    Oid  id = RelationGetRelid(reln);

    nblocks = dmu_object_max_nonzero_offset(dbase,id);
   
    dmu_write(dbase,id,nblocks,BLCKSZ,buffer,tx);

    return SM_SUCCESS;
}

/*
 *	zfsopen() -- Open the specified relation.
 */
int
zfsopen(Relation reln)
{
	uint64_t	nblocks;
        
        /*  no-op  */
        reln->rd_fd = ZFS_OPEN;

	return ZFS_OPEN;
}

/*
 *	zfsclose() -- Close the specified relation, if it isn't closed already.
 *
 *		AND FREE fd vector! It may be re-used for other relation!
 *		reln should be flushed from cache after closing !..
 *
 *		Returns SM_SUCCESS or SM_FAIL with errno set as appropriate.
 */
int
zfsclose(Relation reln)
{
        reln->rd_fd = ZFS_CLOSED;

	return SM_SUCCESS;
}


/*
 *	zfsread() -- Read the specified block from a relation.
 *
 *		Returns SM_SUCCESS or SM_FAIL.
 */
int
zfsread(Relation reln, BlockNumber blocknum, char *buffer)
{
        int err = -1;
	uint64_t	nblocks = (uint64_t)blocknum;

    dmu_tx_t* tx = GetZFSTx(true);
    Oid  id = RelationGetRelid(reln);

    while ( err != 0 ) {
        err = dmu_read(dbase, id, blocknum, BLCKSZ, buffer);
        perror("ZFS");
        if ( IsBootstrapProcessingMode() ) {
            zfscreate(reln);
        } else {
            return SM_FAIL;
        }
    }

    return SM_SUCCESS;
}

/*
 *	zfswrite() -- Write the supplied block at the appropriate location.
 *
 *		Returns SM_SUCCESS or SM_FAIL.
 */
int
zfswrite(Relation reln, BlockNumber blocknum, char *buffer)
{
	uint64_t	nblocks = (uint64_t)blocknum;
        int err = -1;

    dmu_tx_t* tx = GetZFSTx(true);
    Oid  id = RelationGetRelid(reln);
    while ( err != 0 ) {
        dmu_write(dbase, id, blocknum, BLCKSZ, buffer, tx);
        perror("ZFS");
        if ( IsBootstrapProcessingMode() ) {
            zfscreate(reln);
        } else {
            return SM_FAIL;
        }
    }


    return SM_SUCCESS;
}

/*
 *	zfsflush() -- Synchronously write a block to disk.
 *
 *		This is exactly like mdwrite(), but doesn't return until the file
 *		system buffer cache has been flushed.
 */
int
zfsflush(Relation reln, BlockNumber blocknum, char *buffer)
{
    uint64_t	nblocks = (uint64_t)blocknum;
    dmu_tx_t* tx = dmu_tx_create(dbase);
    Oid  id = RelationGetRelid(reln);

        if ( !dmu_tx_assign(tx, TXG_WAIT) ) {
            perror("ZFS");
            elog(ERROR,"no zfs transaction");
        }

    dmu_write(dbase, id, nblocks, BLCKSZ, buffer, tx);

    dmu_tx_commit(tx);

    return SM_SUCCESS;
}

/*
 *	zfsblindwrt() -- Write a block to disk blind.
 *
 */
int
zfsblindwrt(char *dbname,
		   char *relname,
		   Oid dbid,
		   Oid relid,
		   BlockNumber blkno,
		   char *buffer,
		   bool dofsync)
{
        elog(ERROR,"no blind ops");

	return -1;
}

/*
 *	zfsmarkdirty() -- Mark the specified block "dirty" (ie, needs fsync).
 *
 *		Returns SM_SUCCESS or SM_FAIL.
 */
int
zfsmarkdirty(Relation reln, BlockNumber blkno)
{
    /*  no - op  */

	return SM_SUCCESS;
}

/*
 *	zfsblindmarkdirty() -- Mark the specified block "dirty" (ie, needs fsync).
 */
int
zfsblindmarkdirty(char *dbname,
				 char *relname,
				 Oid dbid,
				 Oid relid,
				 BlockNumber blkno)
{
        elog(ERROR,"no blind ops");

	return -1;
}

/*
 *	zfsnblocks() -- Get the number of blocks stored in a relation.
 *
 *		Important side effect: all segments of the relation are opened
 *		and added to the mdfd_chain list.  If this routine has not been
 *		called, then only segments up to the last one actually touched
 *		are present in the chain...
 *
 *		Returns # of blocks, elog's on error.
 */
int
zfsnblocks(Relation reln)
{
    Oid  id = RelationGetRelid(reln);
    
    uint64_t nblocks = dmu_object_max_nonzero_offset(dbase,id);
    return (int)nblocks;
}

/*
 *	zfstruncate() -- Truncate relation to specified number of blocks.
 *
 *		Returns # of blocks or -1 on error.
 */
int
zfstruncate(Relation reln, int nblocks)
{
    uint64_t	nbcks = (uint64_t)nblocks;

    dmu_tx_t* tx = GetZFSTx(true);
    Oid  id = RelationGetRelid(reln);
    
    int err = dmu_free_range(dbase, id, nbcks, 0, tx);

	return err;

}	/* mdtruncate */

/*
 *	zfscommit() -- Commit a transaction.
 *
 *		All changes to magnetic disk relations must be forced to stable
 *		storage.  This routine makes a pass over the private table of
 *		file descriptors.  Any descriptors to which we have done writes,
 *		but not synced, are synced here.
 *
 *		Returns SM_SUCCESS or SM_FAIL with errno set as appropriate.
 */
int
zfscommit()
{
    dmu_tx_t* tx = GetZFSTx(false);
    if ( tx != NULL ) {
        dmu_tx_commit(tx);
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
zfsabort()
{

    dmu_tx_t* tx = GetZFSTx(false);
    if ( tx != NULL ) {
        dmu_tx_abort(tx);
    }
	return SM_SUCCESS;
}


static dmu_tx_t*
GetZFSTx(bool set) {
    dmu_tx_t*    tx = NULL;
    ZFSGlobals*  info = GetZFSGlobals();
    
    if ( set && info->current_tx == NULL ) {
        info->current_tx = dmu_tx_create(dbase);
        if ( !dmu_tx_assign(tx, TXG_WAIT) ) {
            perror("ZFS");
            elog(ERROR,"no zfs transaction");
        }
    }

    return info->current_tx;
}

static ZFSGlobals*
GetZFSGlobals(void)
{
    ZFSGlobals* info = GetEnvSpace(zfs_id);
    if ( info == NULL ) {
        info = AllocateEnvSpace(zfs_id,sizeof(ZFSGlobals));
        memset(info,0x00,sizeof(ZFSGlobals));
        info->current_tx = NULL;
    }
    return info;
}
