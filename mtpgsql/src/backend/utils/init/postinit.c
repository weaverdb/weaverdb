/*-------------------------------------------------------------------------
 *
 * postinit.c
 *	  postgres initialization utilities
 *
 * Portions Copyright (c) 2000-2024, Myron Scott  <myron@weaverdb.org>
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *
 *
 *
 *-------------------------------------------------------------------------
 */
#include <fcntl.h>
#include <sys/file.h>

#include <math.h>
#include <unistd.h>
#include <errno.h>


#include "postgres.h"
#include "env/env.h"
#include "env/properties.h"
#include "env/dbwriter.h"
#include "env/dolhelper.h"

#include "access/heapam.h"
#include "access/xact.h"
#include "catalog/catname.h"
#include "catalog/pg_database.h"
#include "libpq/libpq.h"
#include "miscadmin.h"
#include "storage/backendid.h"
#include "storage/multithread.h"
#include "storage/sinval.h"
#include "storage/smgr.h"
#include "utils/inval.h"
#include "utils/portal.h"
#include "utils/relcache.h"
#include "utils/syscache.h"
#include "version.h"
#include "env/freespace.h"
#include "env/poolsweep.h"

#ifdef MULTIBYTE
#include "mb/pg_wchar.h"
#endif

void		BaseInit(void);

static void ReverifyMyDatabase(const char *name);
static void InitCommunication(void);
static void flush_all(void);

static IPCKey PostgresIpcKey;

/*** InitPostgres support ***/


/* --------------------------------
 *		ReverifyMyDatabase
 *
 * Since we are forced to fetch the database OID out of pg_database without
 * benefit of locking or transaction ID checking (see utils/misc/database.c),
 * we might have gotten a wrong answer.  Or, we might have attached to a
 * database that's in process of being destroyed by destroydb().  This
 * routine is called after we have all the locking and other infrastructure
 * running --- now we can check that we are really attached to a valid
 * database.
 *
 * In reality, if destroydb() is running in parallel with our startup,
 * it's pretty likely that we will have failed before now, due to being
 * unable to read some of the system tables within the doomed database.
 * This routine just exists to make *sure* we have not started up in an
 * invalid database.  If we quit now, we should have managed to avoid
 * creating any serious problems.
 *
 * This is also a handy place to fetch the database encoding info out
 * of pg_database, if we are in MULTIBYTE mode.
 * --------------------------------
 */
static void
ReverifyMyDatabase(const char *name)
{
	Relation	pgdbrel;
	HeapScanDesc pgdbscan;
	ScanKeyData key;
	HeapTuple	tup;

	/*
	 * Because we grab AccessShareLock here, we can be sure that destroydb
	 * is not running in parallel with us (any more).
	 */
	pgdbrel = heap_openr(DatabaseRelationName, AccessShareLock);

	ScanKeyEntryInitialize(&key, 0, Anum_pg_database_datname,
						   F_NAMEEQ, NameGetDatum(name));

	pgdbscan = heap_beginscan(pgdbrel, SnapshotNow, 1, &key);

	tup = heap_getnext(pgdbscan);
	if (!HeapTupleIsValid(tup) ||
		tup->t_data->t_oid != GetEnv()->DatabaseId)
	{
		/* OOPS */
		heap_close(pgdbrel, AccessShareLock);

		/*
		 * The only real problem I could have created is to load dirty
		 * buffers for the dead database into shared buffer cache; if I
		 * did, some other backend will eventually try to write them and
		 * die in mdblindwrt.  Flush any such pages to forestall trouble.
		 */
		DropBuffers(GetEnv()->DatabaseId);
		/* Now I can commit hara-kiri with a clear conscience... */
		elog(FATAL, "Database '%s', OID %lu, has disappeared from pg_database",
			 name, GetEnv()->DatabaseId);
	}

	/*
	 * OK, we're golden.  Only other to-do item is to save the MULTIBYTE
	 * encoding info out of the pg_database tuple.	Note we also set the
	 * "template encoding", which is the default encoding for any CREATE
	 * DATABASE commands executed in this backend; essentially, you get
	 * the same encoding of the database you connected to as the default.
	 * (This replaces code that unreliably grabbed template1's encoding
	 * out of pg_database.	We could do an extra scan to find template1's
	 * tuple, but for 99.99% of all backend startups it'd be wasted cycles
	 * --- and the 'createdb' script connects to template1 anyway, so
	 * there's no difference.)
	 */
#ifdef MULTIBYTE
	SetDatabaseEncoding(((Form_pg_database) GETSTRUCT(tup))->encoding);
	SetTemplateEncoding(((Form_pg_database) GETSTRUCT(tup))->encoding);
#endif

	heap_endscan(pgdbscan);
	heap_close(pgdbrel, AccessShareLock);
}



/* --------------------------------
 *		InitCommunication
 *
 *		This routine initializes stuff needed for ipc, locking, etc.
 *		it should be called something more informative.
 *
 * Note:
 *		This does not set MyBackendId.	MyBackendTag is set, however.
 * --------------------------------
 */
static void
InitCommunication()
{
	char	   *postid;			/* value of environment variable */
	char	   *postport;		/* value of environment variable */
	char	   *ipc_key;		/* value of environemnt variable */
	IPCKey		key = 0;

	/* ----------------
	 *	try and get the backend tag from POSTID
	 * ----------------
	 */
	SetMyBackendId(-1);

	postid = getenv("POSTID");
	if (!PointerIsValid(postid))
		SetMyBackendId(-1);
	else
	{
		SetMyBackendId(atoi(postid));
	}


	ipc_key = getenv("IPC_KEY");
	if (!PointerIsValid(ipc_key))
		key = -1;
	else
	{
		key = atoi(ipc_key);
	}

	postport = getenv("POSTPORT");

	if (PointerIsValid(postport))
	{
		if (GetMyBackendId() == -1)
			elog(FATAL, "InitCommunication: missing POSTID");
	}
	else
	{
		/* ----------------
		 *	assume we're running a postgres backend by itself with
		 *	no front end or postmaster.
		 * ----------------
		 */
		if (GetMyBackendId() == -1)
			SetMyBackendTag(1);

		key = PrivateIPCKey;
	}

	/* ----------------
	 *	initialize shared memory and semaphores appropriately.
	 * ----------------
	 */

        PostgresIpcKey = key;
        AttachSharedMemoryAndSemaphores(key);
}



/* --------------------------------
 * InitPostgres
 *		Initialize POSTGRES.
 *
 * Note:
 *		Be very careful with the order of calls in the InitPostgres function.
 * --------------------------------
 */
int			lockingOff = 0;		/* backend -L switch */

void 
flush_all() {
    FlushAllDirtyBuffers(true);
}

/*
 */
void
InitPostgres(const char *dbname)
{

	char	   *fullpath,datpath[MAXPGPATH];
	char	   *reason;
	bool		bootstrap = IsBootstrapProcessingMode();
	/* ----------------
	 *	initialize the backend local portal stack used by
	 *	internal PQ function calls.  see src/lib/libpq/be-dumpdata.c
	 *	This is different from the "portal manager" so this goes here.
	 *	-cim 2/12/91
	 * ----------------
	 */
	be_portalinit();

	/* initialize the local buffer manager */

#ifndef XLOG
		on_shmem_exit(flush_all, (caddr_t) NULL);
#endif

	/* ----------------
	 *	initialize the database id used for system caches and lock tables
	 * ----------------
	 */
	if (bootstrap)
	{
		if ( (access(DataDir, F_OK) != -1) ) {
			SetDatabaseName(dbname);
			snprintf(datpath,MAXPGPATH,"%s%c%s",DataDir,SEP_CHAR,DatabaseRelationName);
			if ( access(datpath,R_OK) != -1 ) {
				GetRawDatabaseInfo(DataDir,&GetEnv()->DatabaseId,datpath);
				if (GetEnv()->DatabaseId == InvalidOid )  GetEnv()->DatabaseId = 1;
			} else {
				GetEnv()->DatabaseId = 1;
			}
			fullpath = ExpandDatabasePath(datpath);
		}
		LockDisable(true);
	}
	else
	{
		if (access(DataDir, F_OK) == -1)
			elog(FATAL, "Database system not found. Data directory '%s' does not exist.",
			DataDir);
	
		ValidatePgVersion(DataDir, &reason);
		if (reason != NULL)
			elog(FATAL, "%s", reason);

		SetDatabaseName(dbname);
		GetRawDatabaseInfo(dbname, &GetEnv()->DatabaseId, datpath);
	
		if (!OidIsValid(GetEnv()->DatabaseId))
			elog(FATAL,
				 "Database \"%s\" does not exist in the system catalog.",
				 dbname);

		fullpath = ExpandDatabasePath(datpath);
		if (!fullpath)
			elog(FATAL, "Database path could not be resolved.");

		/* Verify the database path */

		if (access(fullpath, F_OK) == -1)
			elog(FATAL, "Database \"%s\" does not exist. The data directory '%s' is missing.",
				 dbname, fullpath);

		ValidatePgVersion(fullpath, &reason);
		if (reason != NULL)
			elog(FATAL, "%s", reason);

		if (chdir(fullpath) == -1)
			elog(FATAL, "Unable to change directory to '%s': %s", fullpath, strerror(errno));

/*		SetDatabasePath(fullpath);    */
	}

	/*
	 * Code after this point assumes we are in the proper directory!
	 */

	/*
	 * Initialize the transaction system and the relation descriptor
	 * cache. Note we have to make certain the lock manager is off while
	 * we do this.
	 */
	LockDisable(true);

	/*
	 * Part of the initialization processing done here sets a read lock on
	 * pg_log.	Since locking is disabled the set doesn't have intended
	 * effect of locking out writers, but this is ok, since we only lock
	 * it to examine AMI transaction status, and this is never written
	 * after initdb is done. -mer 15 June 1992
	 */

        smgrinit();
        RelationInitialize();		/* pre-allocated reldescs created here */
        DBWriterInit();
        DBCreateWriterThread(SYNC_MODE);
 	InitializeTransactionSystem();		/* pg_log,etc init/crash recovery here */
        InitFreespace();


        InitializeDol();                              /* Division of Labor System init */
	LockDisable(false);

        InitThread(NORMAL_THREAD);
	/*
	 * Initialize my entry in the shared-invalidation manager's array of
	 * per-backend data.  (Formerly this came before InitProcess, but now
	 * it must happen after, because it uses MyProc.)  Once I have done
	 * this, I am visible to other backends!
	 *
	 * Sets up MyBackendId, a unique backend identifier.
	 */
	InitSharedInvalidationState();

	if (GetMyBackendId() > GetMaxBackends() || GetMyBackendId() <= 0)
	{
		elog(FATAL, "cinit2: bad backend id %d (%d)",
                     GetMyBackendTag(),
                     GetMyBackendId());
	}

	/*
	 * Initialize the access methods. Does not touch files (?) - thomas
	 * 1997-11-01
	 */
	initam();


	/*
	 * Does not touch files since all routines are builtins (?) - thomas
	 * 1997-11-01
	 */
	InitCatalogCache();

	/*
	 * Set ourselves to the proper user id and figure out our postgres
	 * user id.  If we ever add security so that we check for valid
	 * postgres users, we might do it here.
	 */

	setuid(geteuid());
	SetUserId();

	if (lockingOff)
		LockDisable(true);

	/*
	 * Unless we are bootstrapping, double-check that InitMyDatabaseInfo()
	 * got a correct result.  We can't do this until essentially all the
	 * infrastructure is up, so just do it at the end.
	 */
	if (!bootstrap)
		ReverifyMyDatabase(dbname);
        
        if ( !bootstrap ) {
/*
            PoolsweepInit(0);   
*/
            on_proc_exit(PoolsweepDestroy, NULL);
/*  if there are recovered pages are present,
 *  index pages need to be scanned and items 
 *  that point to unused heap items need to be 
 *  removed.
 */            
            {
                List* dbids = smgrdbrecoverylist();
                List*  item;

                if ( dbids != NULL ) {
                    PoolsweepInit(0);   
                    foreach(item, dbids) {
                        AddRecoverRequest(smgrdbrecoveryname(lfirsti(item)), lfirsti(item));
                    }
                    foreach(item, dbids) {
                        AddWaitRequest(smgrdbrecoveryname(lfirsti(item)), lfirsti(item));
                    }   
                    
                    smgrcompleterecovery();
                    PoolsweepDestroy();
                }
            }
        }
}

void
BaseInit()
{

	/*
	 * Turn on the exception handler. Note: we cannot use elog, Assert,
	 * AssertState, etc. until after exception handling is on.
	 */
	EnableExceptionHandling(true);

	/*
	 * Memory system initialization - we may call palloc after
	 * EnableMemoryContext()).	Note that EnableMemoryContext() must
	 * happen before EnablePortalManager().
	 */
	MemoryContextInit();	
/*
	EnablePortalManager();	 memory for portal/transaction stuff 
*/

	/*
	 * Attach to shared memory and semaphores, and initialize our
	 * input/output/debugging file descriptors.
	 */
	InitCommunication();
}
