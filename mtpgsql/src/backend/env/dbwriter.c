/*-------------------------------------------------------------------------
 *
 * dbwriter.c
 *     write to disk thread and pipeline
 * 
 * Copyright (c) 2000-2024, Myron Scott  <myron@weaverdb.org>
 *
 *
 * IDENTIFICATION
 *
 *-------------------------------------------------------------------------
 */

/*
 * DO NOT do any shared lock table locking here b/c  many stuctures in the Env
 * are used in locking and we are using the Env of the commiting user threads
 * to pull Relations out of caches and the file system.
 *
 * The problem lies in the Logging of transactions but we only need to lock the
 * buffer in this case.
 *
 * Vacuums are done with an exclusive lock on the entire system so
 * manipulations can be done without lock table locking.
 *
 * If we try and lock here, the Lock table goes inconsistent.
 *
 * MKS 1.26.2000
 *
 *
 */

#include <pthread.h>
#include <unistd.h>
#include <sys/time.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "c.h"
#include "postgres.h"
#include "env/env.h"
#include "env/properties.h"

#include "config.h"
#include "miscadmin.h"
#include "storage/localbuf.h"
#include "storage/block.h"
#include "storage/bufmgr.h"
#include "storage/smgr.h"
#include "storage/bufpage.h"
#include "access/genam.h"
#include "access/transam.h"
#include "storage/bufmgr.h"
#include "storage/spin.h"
#include "storage/multithread.h"
#include "storage/sinvaladt.h"
#include "utils/relcache.h"
#include "utils/memutils.h"
#include "utils/portal.h"
#include "utils/inval.h"
#include "utils/elog.h"
#include "catalog/catname.h"
#include "catalog/pg_class.h"
#include "env/poolsweep.h"
#include "env/freespace.h"
#include "env/pg_crc.h"

#ifdef SPIN_IS_MUTEX
#include "storage/m_lock.h"
#else
#include "storage/s_lock.h"
#endif

#include "env/dbwriter.h"



#define MAXTRANS  DEF_MAXBACKENDS

enum writerstates {
    NOT_READY,
    WAITING,
    READY,
    PRIMED,
    RUNNING,
    LOGGED,
    SYNCED,
    COMPLETED,
    FLUSHING,
    DEAD
};

typedef enum writerstates WriterState;

typedef struct writegroups* WriteGroup;

struct writegroups {
    WriterState                         currstate;
    bool*				buffers;
    bool*				wait_for_sync;
    int*				defers;		/* keep mark until CriticalIO hold drops */
    int*				release;
    BufferTag*                          descriptions;
    THREAD**				WaitingThreads;
    TransactionId                       LastSoftXid;
    TransactionId*                      transactions;
    int*				transactionState;
    int 				numberOfTrans;
    int 				cioHolds;
    int                                 flushCount;
    
    pthread_t                           owner;
    
    pthread_cond_t			gate;
    
    pthread_mutex_t			checkpoint;
    pthread_cond_t			broadcaster;
    
    bool                                loggable;
    bool                                log_started;
    
    bool				isTransFriendly;
    bool				locked;
    /*  for convenience, cache these here  */
    Oid                                 LogId;
    Oid                                 VarId;
    
    char*                               snapshot;
    long                                generation;
    
    WriteGroup				next;
};

typedef struct dbkey {
    Oid				relid;
    Oid				dbid;
} DBKey;

typedef struct DBPathCache {
    DBKey                       key;
    SmgrInfo                    smgrinfo;
    NameData                    relname;
    NameData                    dbname;
    double			accesses;
    double			tolerance;
    int                         idle_count;
    bool                        refresh;
    bool                        keepstats;
    bool                        commit;
} PathCache;

static HTAB*  				db_table;
static MemoryContext                    db_cxt;
static bool				db_inited = false;

static  PathCache* GetPathCache(HASHACTION mode, char *relname , char *dbname, Oid bufrel, Oid bufdb);
static void CommitPackage(WriteGroup  cart);
static int SignalDBWriter(WriteGroup  cart);
static WriteGroup GetCurrentWriteGroup(bool forcommit);
static int UnlockWriteGroup(WriteGroup  cart);
static WriteGroup GetNextTarget(WriteGroup last);
static void* DBWriter(void* arg);
static void* SyncWriter(void *jones);

static WriteGroup GetSyncGroup(void);
static void ActivateSyncGroup(void);
static int FlushWriteGroup(WriteGroup cart);

static void DBTableInit(void);
static void* DBAlloc(Size size, void* cxt);
static void DBFree(void* ptr, void* cxt);

static void PathCacheCompleteWalker(PathCache *relationPtr, int dummy);

static WriteGroup CreateWriteGroup(int maxtrans, int buffers);
static bool CheckWriteGroupState(WriteGroup grp, bool timed);
static void AdvanceWriteGroupQueue(WriteGroup cart);
static int LogWriteGroup(WriteGroup cart);
static int SyncWriteGroup(WriteGroup cart);
static int FinishWriteGroup(WriteGroup cart);
static WriteGroup CleanupWriteGroup(WriteGroup cart);
static WriteGroup DestroyWriteGroup(WriteGroup w);

static int LogTransactions(WriteGroup cart);
static int WakeWaitingThreads(WriteGroup cart, bool sync);
static int ClearLogs(WriteGroup list);
static int LogBuffers(WriteGroup list);
static int SyncBuffers(WriteGroup list, bool forcommit);
static void ResetWriteGroup(WriteGroup cart);
static void TransferDeferredWriteMarks(WriteGroup src);
static int ForgetPathCache(Oid relid, Oid dbid);

static int MergeWriteGroups(WriteGroup target, WriteGroup src);
static int ResetThreadState(THREAD*  t);

static int TakeFileSystemSnapshot(char* cmd);

extern bool     TransactionSystemInitialized;

static WriteGroup log_group;
static WriteGroup sync_group;   /*  a holder for buffers that need to be synced */
static int    sync_buffers = 0;

static pthread_attr_t writerprops;

static bool     logging = false;
static bool     stopped = false;

static int      wait_timeout = 400;
static int      sync_timeout = 5000;
static int      max_logcount = (512);
static long   flush_time = 3000;
/*
 * heap garbage collection threshold -- asks for a vacuum every time the
 * number of syncs on a heap/number of relation blocks is accessed
 */

static int      maxtrans = MAXTRANS;
static double   hgc_threshold = MAXTRANS;
static double   hgc_factor = 1;
static double   hgc_update = 1;

static pthread_t *writerid;
static int      writercount = 0;

/*
 * This thread writes out all buffers at transaction commit time Only one
 * thread is created at this time and two WriteGroups to collect information
 * about which buffers to write.  The point here is to maximize the number of
 * transaction commits that occur at a time.  We don't mind if the inserting
 * thread needs to wait a little bit for the other threads to register. or
 * for the preceeding write group to finish  MKS - 11/3/2000
 */

void DBWriterInit() {
    struct sched_param sched;
    stopped = false;
    int             sched_policy;
    maxtrans = MAXTRANS;
    
    if ( PropertyIsValid("maxgrouptrans") ) {
        int check = GetIntProperty("maxgrouptrans");
        if ( check > 0 && check < (32 * 1024) ) {
            maxtrans = check;
        }
    }
    if ( PropertyIsValid("waittime") ) {
        wait_timeout = GetIntProperty("waittime");
    }
    if ( PropertyIsValid("synctimeout") ) {
        sync_timeout = GetIntProperty("synctimeout");
    }    
    if ( PropertyIsValid("gcthreshold") ) {
        hgc_threshold = GetFloatProperty("gcthreshold");
    }
    if ( PropertyIsValid("gcsizefactor") ) {
        hgc_factor = GetFloatProperty("gcsizefactor");
    }
     if ( PropertyIsValid("gcupdatefactor") ) {
        hgc_update = GetFloatProperty("gcupdatefactor");
    }
    
    elog(DEBUG, "[DBWriter]waiting time %d", wait_timeout);
    elog(DEBUG, "[DBWriter]sync timeout %d", sync_timeout);
    elog(DEBUG, "[DBWriter]default commit type %d", GetTransactionCommitType());
    elog(DEBUG, "[DBWriter]maximum numbers of transactions %d", maxtrans);
    memset(&writerprops, 0, sizeof(pthread_attr_t));
    memset(&sched, 0, sizeof(struct sched_param));
    /* init thread attributes  */
    pthread_attr_init(&writerprops);
    #ifndef MACOSX
    pthread_attr_setscope(&writerprops, PTHREAD_SCOPE_SYSTEM);
    /*
     * pthread_attr_setstacksize(&writerprops,(0x200000));
     * pthread_attr_setguardsize(&writerprops,(0x4000));
     */
    pthread_attr_getschedpolicy(&writerprops, &sched_policy);
    sched.sched_priority = sched_get_priority_max(sched_policy);

    pthread_attr_setschedparam(&writerprops, &sched);
    #endif

    if (!IsMultiuser()) {
        logging = false;
        /* no logging so make sure everyone waits for sync */
        SetTransactionCommitType(SYNCED_COMMIT);
    } else {
        int mlog = GetIntProperty("maxlogcount");

        logging = true;

        max_logcount = MaxBuffers;

        if ( mlog >= 0 ) {
            /*  user provided sync timeout in microsecounds  */
            max_logcount = mlog;
        }
    }

    log_group = CreateWriteGroup(maxtrans, MaxBuffers);
    log_group->next = CreateWriteGroup(maxtrans, MaxBuffers);
    /* link in a circle  */
    log_group->next->next = log_group;

    sync_group = CreateWriteGroup(maxtrans, MaxBuffers);
    sync_group->next = sync_group;

    DBTableInit();
}

static void DBTableInit() {
    HASHCTL ctl;
    
    db_cxt = AllocSetContextCreate(GetEnvMemoryContext(),
            "DBWriterMemoryContext",
            ALLOCSET_DEFAULT_MINSIZE,
            ALLOCSET_DEFAULT_INITSIZE,
            ALLOCSET_DEFAULT_MAXSIZE);
    
    MemoryContextSwitchTo(db_cxt);
    
    memset(&ctl, 0, sizeof(HASHCTL));
    ctl.keysize = sizeof(DBKey);
    ctl.entrysize = sizeof(PathCache);
    ctl.hash = tag_hash;
    ctl.alloc = DBAlloc;
    ctl.free = DBFree;
    ctl.hcxt = db_cxt;
    
    db_table = hash_create("db writer hash", 100, &ctl, HASH_ELEM | HASH_ALLOC | HASH_FUNCTION | HASH_CONTEXT);
    
    
    db_inited = true;
}


static void* DBAlloc(Size size, void* cxt) {
    return MemoryContextAlloc(cxt, size);
}

static void DBFree(void* pointer, void* cxt) {
    pfree(pointer);
}

WriteGroup CreateWriteGroup(int trans, int buffers) {
    WriteGroup cart = (WriteGroup) os_malloc(sizeof(struct writegroups));
    memset(cart, 0, sizeof(struct writegroups));
    
    if (pthread_cond_init(&cart->gate, NULL) != 0) {
        perror("DBWriter init gate");
    }
    if (pthread_mutex_init(&cart->checkpoint, NULL) != 0) {
        perror("DBWriter init checkpoint");
    }
    if (pthread_cond_init(&cart->broadcaster, NULL) != 0) {
        perror("DBWriter init broadcaster");
    }
    
    cart->WaitingThreads = os_malloc(sizeof(THREAD*) * trans);
    cart->wait_for_sync = os_malloc(sizeof(bool) * trans);
    cart->transactions = os_malloc(sizeof(TransactionId) * trans);
    cart->transactionState = os_malloc(sizeof(int) * trans);
    cart->buffers = os_malloc(sizeof(bool) * buffers);
    cart->defers = os_malloc(sizeof(int) * buffers);
    cart->release = os_malloc(sizeof(int) * buffers);
    cart->descriptions = os_malloc(sizeof(BufferTag) * buffers);
    
    cart->numberOfTrans = 0;
    cart->cioHolds = 0;
    cart->flushCount = 0;
    cart->currstate = NOT_READY;
    cart->snapshot = NULL;
    cart->next = cart;

    ResetWriteGroup(cart);
    
    cart->LogId = RelOid_pg_log;
    cart->VarId = RelOid_pg_variable;
    
    cart->LastSoftXid = InvalidTransactionId;
    
    cart->generation = 1;
    
    return cart;
}

WriteGroup DestroyWriteGroup(WriteGroup w) {
    pthread_cond_destroy(&w->gate);
    
    pthread_mutex_destroy(&w->checkpoint);
    pthread_cond_destroy(&w->broadcaster);
    
    os_free(w->WaitingThreads);
    os_free(w->wait_for_sync);
    os_free(w->transactions);
    os_free(w->transactionState);
    os_free(w->buffers);
    os_free(w->defers);
    os_free(w->release);
    os_free(w->descriptions);

    WriteGroup next = w->next;

    while (next->next != w->next) {
        if (next->next == w) {
            next->next = w->next;
        } else {
            next = next->next;
        }
    }
    
    next = w->next == w ? NULL : w->next;

    os_free(w);

    return next;
}


void DBCreateWriterThread(DBMode mode) {
    switch ( mode ) {
        case LOG_MODE:
            if ( sync_timeout >= 0 ) {
                writerid = os_realloc(writerid, sizeof(pthread_t) * (writercount + 1));
                if (pthread_create(&writerid[writercount++], &writerprops, SyncWriter, NULL) != 0) {
                    elog(FATAL, "[DBWriter]could not create sync writer\n");
                }  
            }
            /*  fall through so that both threads are created */
        case SYNC_MODE: 
            writerid = os_realloc(writerid, sizeof(pthread_t) * (writercount + 1));
            if (pthread_create(&writerid[writercount++], &writerprops, DBWriter, NULL) != 0) {
                elog(FATAL, "[DBWriter]could not create db writer\n");
            }  
            break;
    }
}

void* SyncWriter(void *jones) {    
    Env            *env = CreateEnv(NULL);
    
    SetEnv(env);
    SetProcessingMode(InitProcessing);
        
    MemoryContextInit();
    MemoryContextSwitchTo(MemoryContextGetTopContext());
    
    while (!stopped) {         
        pthread_mutex_lock(&sync_group->checkpoint);
        while ( sync_group->currstate != WAITING && sync_group->currstate != DEAD ) {
            pthread_cond_wait(&sync_group->broadcaster,&sync_group->checkpoint);
            if ( stopped ) break;
        }
        sync_group->currstate = FLUSHING;
        pthread_mutex_unlock(&sync_group->checkpoint);
        
        if ( !stopped ) {
            SyncBuffers(sync_group,false);
        }
        
        pthread_mutex_lock(&sync_group->checkpoint);
        sync_group->currstate = NOT_READY;
        pthread_cond_signal(&sync_group->broadcaster);
        pthread_mutex_unlock(&sync_group->checkpoint);
    }
    
    
    SetEnv(NULL);
    DestroyEnv(env);    
    
    return NULL;
}

WriteGroup GetSyncGroup() {
    pthread_mutex_lock(&sync_group->checkpoint);
    while ( sync_group->currstate == FLUSHING || sync_group->currstate == COMPLETED ) {
        sync_group->currstate = COMPLETED;
        pthread_cond_wait(&sync_group->broadcaster,&sync_group->checkpoint);
    }
    sync_group->currstate = NOT_READY;
    pthread_mutex_unlock(&sync_group->checkpoint);
    return sync_group;
}

void ActivateSyncGroup() {
    pthread_mutex_lock(&sync_group->checkpoint);
    sync_group->currstate = WAITING;
    pthread_cond_signal(&sync_group->broadcaster);
    pthread_mutex_unlock(&sync_group->checkpoint);
}

int FlushWriteGroup(WriteGroup cart) {
    int release = 0;
    struct timeval t1,t2;
    long elapsed;
    
    pthread_mutex_unlock(&cart->checkpoint);

    /*
     * Idle the background syncer before taking the generation wrlock.
     * SyncBuffers can read catalog pages, and those reads take the rdlock.
     */
    WriteGroup sync = GetSyncGroup();

    gettimeofday(&t1,NULL);

    elog(DEBUG,"Flushing cart gen=%ld loggable: %d", cart->generation, cart->loggable);

    if ( cart->loggable ) {
        cart->flushCount += 1;
        release = LogBuffers(cart);
    }

    sync_buffers += MergeWriteGroups(cart, sync);
    release += SyncBuffers(cart, false);

    elog(DEBUG, "flushed out %d buffers", release);
     
    gettimeofday(&t2,NULL);
    
    elapsed = (t2.tv_sec - t1.tv_sec) * 1000;      // sec to ms
    elapsed += (t2.tv_usec - t1.tv_usec) / 1000;   // us to ms
    
    flush_time = elapsed;   
    
    pthread_mutex_lock(&cart->checkpoint);
    cart->currstate = READY;
    pthread_cond_broadcast(&cart->broadcaster);
    
    return release;
}

void* DBWriter(void *jones) {
    char            dbuser[255];
    WriteGroup     cart = NULL;
    Env            *env = CreateEnv(NULL);
    
    SetEnv(env);
    SetProcessingMode(InitProcessing);
        
    MemoryContextInit();
    
    SetDatabaseName("template1");
    if (!IsBootstrapProcessingMode()) {
        GetRawDatabaseInfo("template1", &env->DatabaseId, dbuser);
    }
    
    InitThread(DBWRITER_THREAD);
        
    RelationInitialize();
    
    while ( !CallableInitInvalidationState() ) { elog(WARN,"cannot create dbwriter's shared state"); };
    
    SetProcessingMode(NormalProcessing);
    
    GetSnapshotHolder()->ReferentialIntegritySnapshotOverride = true;
    
    MemoryContextSwitchTo(MemoryContextGetTopContext());
    
    cart = GetNextTarget(NULL);
    
    while (!stopped) {
        int     releasecount = 0;
        bool    primed = false;
                
        if (setjmp(env->errorContext) != 0) {
            elog(FATAL, "error in dbwriter");
        }
        
        if (TraceBufferFlushEnabled()) {
            fprintf(stderr,
                    "FLUSH: running cart gen=%ld\n",
                    cart->generation);
            fflush(stderr);
        }
        
        while (CheckWriteGroupState(cart, (sync_buffers > 0) || cart->cioHolds > 0)) {
            if ( cart->currstate == FLUSHING ) {
                FlushWriteGroup(cart);
            }
            if ( stopped ) break;
            /*  wait to see if we are ready to go  */
        }

        AdvanceWriteGroupQueue(cart);
        Assert(cart->currstate == PRIMED || cart->currstate == READY);
        
        primed = ( cart->currstate == PRIMED );
        
        cart->currstate = RUNNING;
        
        UnlockWriteGroup(cart);

        releasecount = LogWriteGroup(cart);

        if ( GetProcessingMode() == NormalProcessing && cart->loggable && (sync_buffers < max_logcount) && !primed ) {
            /*  move buffer syncs to the sync cart */
            Assert(cart->cioHolds == 0);
            sync_buffers += MergeWriteGroups(GetSyncGroup(), cart);
            ActivateSyncGroup();
        } else {
            /* transfer any saved writes back to the current cart and sync all buffers */
            WriteGroup sync = GetSyncGroup();
            MergeWriteGroups(cart, sync);
            ResetWriteGroup(sync);
            sync_buffers = 0;
            releasecount += SyncWriteGroup(cart);
        }

        FinishWriteGroup(cart);
        /* no invalids generated by DBWriter mean anything  */
        DiscardInvalid();

        cart = GetNextTarget(cart);
    }
    
    UnlockWriteGroup(cart);
    
    MergeWriteGroups(cart, GetSyncGroup());
    
    cart = CleanupWriteGroup(cart);
    CleanupWriteGroup(cart);

    cart = GetSyncGroup();
    pthread_mutex_lock(&cart->checkpoint);
    cart->currstate = DEAD;
    pthread_cond_signal(&cart->broadcaster);
    pthread_mutex_unlock(&cart->checkpoint);
    
    /* all done cleaning, we should have no valid threads or write groups  */
    CallableCleanupInvalidationState();
    RelationCacheShutdown();
    
    ThreadReleaseLocks(false);
    ThreadReleaseSpins(GetMyThread());
    
    DestroyThread();
    
    SetEnv(NULL);
    DestroyEnv(env);

    return NULL;
}

WriteGroup CleanupWriteGroup(WriteGroup cart) {
    pthread_mutex_lock(&cart->checkpoint);
    if ( cart->currstate == COMPLETED ) {
        cart->currstate = DEAD;
        pthread_mutex_unlock(&cart->checkpoint);
    } else {
        pthread_mutex_unlock(&cart->checkpoint);
        LogWriteGroup(cart);
        SyncWriteGroup(cart);
        FinishWriteGroup(cart);
        pthread_mutex_lock(&cart->checkpoint);
        cart->currstate = DEAD;
        pthread_mutex_unlock(&cart->checkpoint);
    }
    return cart->next;
}

bool CheckWriteGroupState(WriteGroup cart, bool timed) {
    int             timerr;
                
    switch (cart->currstate) {
        case COMPLETED:
            cart->currstate = NOT_READY;
            return true;
        case RUNNING:
        case LOGGED:
            /*  wait for this to complete it's current operation
             * before we move on
             */
            elog(FATAL, "should not happen");
            /*  do not continue, not valid */
            return true;
        case NOT_READY:
            cart->currstate = WAITING;
            /*  if there are logged buffers, wait sync_timeout
             * then sync buffers
             */
            if ( timed ) {
                struct timespec waittime;
                ptimeout(&waittime,sync_timeout);
                timerr = pthread_cond_timedwait(&cart->gate, &cart->checkpoint, &waittime);
                if (timerr == ETIMEDOUT) {
                    cart->currstate = PRIMED;
                    return false;
                }
            } else {
                /*  waiting for a write signal */
                if (pthread_cond_wait(&cart->gate, &cart->checkpoint) != 0) {
                    perror("DBWRITER:");
                    elog(FATAL, "[DBWriter]could not wait for write signal\n");
                }
                if ( sync_timeout == 0 ) {
                    cart->currstate = PRIMED;
                    return false;
                }
            }
            Assert(cart->currstate != WAITING);
            return true;
        case READY:
            timerr = 0;
            if (wait_timeout > 0 &&
                cart->isTransFriendly &&
                !(stopped) &&
                cart->numberOfTrans < maxtrans) {

                    struct timespec waittime;

                    cart->currstate = WAITING;
                    ptimeout(&waittime,wait_timeout);
                    timerr = pthread_cond_timedwait(&cart->gate, &cart->checkpoint, &waittime);
                    if (timerr == ETIMEDOUT) {
                        if ( cart->currstate == FLUSHING ) {
                            return true;
                        }
                        if ( wait_timeout > sync_timeout ) cart->currstate = PRIMED;
                        else cart->currstate = READY;
                        return false;
                    } else {
                        Assert(cart->currstate != WAITING);
                        return true;
                    }
            }
            return false;
        case PRIMED:
            return false;
        case FLUSHING:
            return true;
        default:
            return true;
    }
    return true;
    
}

void AdvanceWriteGroupQueue(WriteGroup cart) {
    pthread_mutex_lock(&cart->next->checkpoint);
    if ( cart->next->currstate == COMPLETED ||
            cart->next->currstate == NOT_READY
            ) {
        cart->next->currstate = NOT_READY;
        cart->next->LastSoftXid = cart->LastSoftXid;
        cart->next->generation = cart->generation + 1;
    } else {
        elog(FATAL, "DB write group in the wrong state");
    }
    if ( log_group == cart ) log_group = cart->next;
    else if ( sync_group == cart ) sync_group = cart->next;
    else elog(FATAL,"unknown advance of write group");
    pthread_mutex_unlock(&cart->next->checkpoint);    
}

int LogWriteGroup(WriteGroup cart) {
    int releasecount = 0;
    int x=0;
    
    if ( cart->loggable ) {
        releasecount = LogBuffers(cart);
        LogTransactions(cart);
        WakeWaitingThreads(cart, false);
    }

    pthread_mutex_lock(&cart->checkpoint);
    cart->currstate = LOGGED;
    pthread_cond_broadcast(&cart->broadcaster);
    pthread_mutex_unlock(&cart->checkpoint);
    
    return releasecount;
}

int WakeWaitingThreads(WriteGroup cart, bool sync) {
    int count = 0;
    for (int x=0;x<cart->numberOfTrans;x++) {
        if ( cart->WaitingThreads[x] != NULL && (sync || !cart->wait_for_sync[x]) ) {
            ResetThreadState(cart->WaitingThreads[x]);
            cart->WaitingThreads[x] = NULL;
            count += 1;
        }
    }
    return count;
}

int SyncWriteGroup(WriteGroup cart) {
    /*  syncing the buffers */
    int releases = 0;
    int trans_logged = 0;
    int x=0;

    releases = SyncBuffers(cart,true);

    if (!cart->loggable) {
        LogTransactions(cart);
    }

    CommitPackage(cart);
    
    ClearLogs(cart);
    
    WakeWaitingThreads(cart, true);
      /* need to lock to release  */
    pthread_mutex_lock(&cart->checkpoint);
    cart->currstate = SYNCED;
    pthread_cond_broadcast(&cart->broadcaster);
    pthread_mutex_unlock(&cart->checkpoint);
    
    return releases;
}

int FinishWriteGroup(WriteGroup cart) {
    if ( cart->snapshot ) {
        TakeFileSystemSnapshot(cart->snapshot);
    }

    pthread_mutex_lock(&cart->checkpoint);
    cart->currstate = COMPLETED;
    pthread_cond_broadcast(&cart->broadcaster);
    pthread_mutex_unlock(&cart->checkpoint);
    
    ResetWriteGroup(cart);
    
    return 0;
}

void CommitPackage(WriteGroup cart) {
    HashTableWalk(db_table, (HashtFunc)PathCacheCompleteWalker, 0);
}

void ResetWriteGroup(WriteGroup cart) {
    Assert(cart->currstate == COMPLETED || cart->currstate == NOT_READY);

    memset(cart->buffers, 0, sizeof(bool) * MaxBuffers);
    memset(cart->defers, 0, sizeof(int) * MaxBuffers);
    memset(cart->release, 0, sizeof(int) * MaxBuffers);
    memset(cart->descriptions, 0, sizeof(BufferTag) * MaxBuffers);
    
    memset(cart->transactions, 0, sizeof(TransactionId) * maxtrans);
    memset(cart->transactionState, 0, sizeof(int) * maxtrans);
    memset(cart->WaitingThreads, 0, sizeof(Env *) * maxtrans);
    memset(cart->wait_for_sync, 0, sizeof(bool) * maxtrans);
    
    cart->numberOfTrans = 0;
    cart->cioHolds = 0;
    cart->flushCount = 0;

    cart->isTransFriendly = true;
    cart->loggable = logging;
    cart->log_started = false;
    cart->snapshot = NULL;
}

int MergeWriteGroups(WriteGroup target, WriteGroup src) {
    BufferDesc*  bufHdr;
    int i = 0;
    int moved = 0;
    
    pthread_mutex_lock(&target->checkpoint);
    for (i = 0, bufHdr = BufferDescriptors; i < MaxBuffers; i++, bufHdr++) {
        
        if ( !src->buffers[i] ) {
            continue;
        }

        moved++;
        
        if ( !target->buffers[i] ) {
            memmove(&target->descriptions[i], &src->descriptions[i], sizeof(BufferTag));
            target->buffers[i] = true;
        }
        
        if ( target->descriptions[i].relId.dbId != src->descriptions[i].relId.dbId ||
                target->descriptions[i].relId.relId != src->descriptions[i].relId.relId ||
                target->descriptions[i].blockNum != src->descriptions[i].blockNum ) {
            elog(DEBUG, "investigate buffer write group merge");
            elog(DEBUG, "dbid:%ld relid:%ld blk:%ld\n",
                    target->descriptions[i].relId.dbId,
                    target->descriptions[i].relId.relId,
                    target->descriptions[i].blockNum);
            elog(DEBUG, "dbid:%ld relid:%ld blk:%ld\n",
                    src->descriptions[i].relId.dbId,
                    src->descriptions[i].relId.relId,
                    src->descriptions[i].blockNum);
        } else {
            target->release[i] += src->release[i];
            src->release[i] = 0;
            memset(&src->descriptions[i],0x00,sizeof(BufferTag));
            src->buffers[i] = false;
        }
    }
    pthread_mutex_unlock(&target->checkpoint);
    
    return moved;
}
 
int LogTransactions(WriteGroup cart) {
    int             i = 0;
    Buffer          buffer = InvalidBuffer;	/* buffer associated with block */
    Block           block;	/* block containing xstatus */
     Relation        LogRelation;

     if (!TransactionSystemInitialized) {
        return 0;
     }
   
    if (cart->numberOfTrans == 0)
        return 0;
    if (IsBootstrapProcessingMode()) {
        return 0;
    }
     
    LogRelation = RelationIdGetRelation(cart->LogId,DEFAULTDBOID);
    
    for (i = 0; i < cart->numberOfTrans; i++) {
        BlockNumber     localblock = InvalidBlockNumber;
        
        if (cart->transactions[i] == 0) {
            elog(FATAL, "zero transaction id");
        }

        DTRACE_PROBE1(mtpg, dbwriter__commit, cart->transactions[i]);
        localblock = TransComputeBlockNumber(LogRelation, cart->transactions[i]);
        
        if (buffer == InvalidBuffer || localblock != BufferGetBlockNumber(buffer)) {
            if (buffer != InvalidBuffer) {
                FlushBuffer(LogRelation,buffer);
            }
            buffer = ReadBuffer(LogRelation, localblock);
            if (!BufferIsValid(buffer)) {
                elog(FATAL, "[DBWriter]bad buffer read in transaction logging");
                return -1;
            }
            
            block = BufferGetBlock(buffer);
        }
        /*
         * ---------------- get the block containing the transaction
         * status ----------------
         */
        
        TransBlockSetXidStatus(block, cart->transactions[i], cart->transactionState[i]);
    }
    
    FlushBuffer(LogRelation,buffer);
    RelationClose(LogRelation);
    
    DTRACE_PROBE1(mtpg, dbwriter__logged, i);
    
    elog(DEBUG, "[DBWriter] logged %d transactions\n", i);
    return i;
}

long UnmarkDeferredWrites(int * criticals) {
    WriteGroup     cart = GetCurrentWriteGroup(false);
    long generation = cart->generation;
    for (int x=0;x<MaxBuffers;x++) {
        if (criticals[x] > 0) {
            Assert(cart->buffers[x]);
            cart->defers[x] -= criticals[x];
        }
    }
    UnlockWriteGroup(cart);
    
    return generation;
}

long CriticalIOHold() {
    WriteGroup     cart = GetCurrentWriteGroup(false);
    long generation = cart->generation - 1;

    if (cart->loggable) {
        cart->cioHolds += 1;
    } else {
        generation = 0;
    }
    UnlockWriteGroup(cart);
    return generation;
}

long CriticalIORelease() {
    WriteGroup     cart = GetCurrentWriteGroup(false);
    long generation = cart->generation - 1;
    if (cart->loggable) {
        cart->cioHolds -= 1;
    } else {
        generation = 0;
    }
    UnlockWriteGroup(cart);
    return generation;
}

long RegisterBufferWrite(BufferDesc * bufHdr, bool defer, bool release) {
    WriteGroup     cart = GetCurrentWriteGroup(false);
    long generation = cart->generation;
    /*
     * if this is the first time we write to this buffer we have to self
     * pin so someone else doesn't free the buffer before we have had a
     * chance to write it out.  Effectively, this means that DBWriter is
     * the only one who can unpin a dirty buffer.  we are doing this
     * without lock b/c the we are assuming the calling thread already
     * has pinned the buffer so we are just adding to it
     */
    
    if (!cart->buffers[bufHdr->buf_id]) {
        /*  need to make sure that the buffer is valid
         * before marking it for write
         */
        if ( ManualPin(bufHdr, false) ) {
            cart->buffers[bufHdr->buf_id] = true;
            cart->release[bufHdr->buf_id]++;
            memcpy(&cart->descriptions[bufHdr->buf_id], &bufHdr->tag, sizeof(BufferTag));
        } else {
            elog(FATAL, "Invalid buffer registered for write bufid:%d dbid:%ld relid:%ld blk:%ld\n",
                    bufHdr->buf_id,
                    bufHdr->tag.relId.dbId,
                    bufHdr->tag.relId.relId,
                    bufHdr->tag.blockNum);
        }
    } else {
        if (
                bufHdr->tag.relId.dbId != cart->descriptions[bufHdr->buf_id].relId.dbId ||
                bufHdr->tag.relId.relId != cart->descriptions[bufHdr->buf_id].relId.relId ||
                bufHdr->tag.blockNum != cart->descriptions[bufHdr->buf_id].blockNum
            ) {
            elog(WARN, "register write should not happen");
            elog(FATAL, "dbid:%ld relid:%ld blk:%ld\n",
                    bufHdr->tag.relId.dbId,
                    bufHdr->tag.relId.relId,
                    bufHdr->tag.blockNum);
        }
    }
    if (defer) {
        cart->defers[bufHdr->buf_id] += 1;
    }

    UnlockWriteGroup(cart);
    /*
     * it's now safe to give up the shared pin of the caller because we
     * know we now have ours, this method is only called by the thread
     * registering the write.
     */
    if (release) {
        ManualUnpin(bufHdr, true);
    }
    return generation;
}
 
void CommitDBBufferWrites(TransactionId xid, int setstate) {
    WriteGroup     cart;
        
    int             position;
    bool            setxid = true;
    
    cart = GetCurrentWriteGroup(true);
    
    if (cart->currstate == RUNNING) {
        elog(FATAL, "[DBWriter]commit in running state");
    }
    
    if ( setstate == XID_COMMIT ) {
        position = cart->numberOfTrans++;
        
        if ( !IsTransactionFriendly() ) {
            cart->isTransFriendly = false;
        }
        
        cart->transactions[position] = xid;
        cart->transactionState[position] = setstate;
        
        SignalDBWriter(cart);
        
        /* no need to wait around if we are aborting */
        if ( IsTransactionSystemDisabled() || (setstate == XID_COMMIT && IsTransactionCareful()) ) {
            cart->WaitingThreads[position] = GetMyThread();
            cart->wait_for_sync[position] = !IsLoggable();
            Assert(GetMyThread()->state == TRANS_COMMIT);
            while ( GetMyThread()->state != TRANS_DEFAULT ) {
                if (pthread_cond_wait(&cart->broadcaster, &cart->checkpoint)) {
                    UnlockWriteGroup(cart);
                    elog(FATAL, "[DBWriter]cannot attach to db write thread");
                }
            }
            setxid = false;
        } else {
            cart->LastSoftXid = xid;
        }
    }
    
    UnlockWriteGroup(cart);
    
    if ( setxid ) {
        Relation        LogRelation = RelationIdGetRelation(cart->LogId, DEFAULTDBOID);
        if (setstate == XID_COMMIT ) {
            setstate = XID_SOFT_COMMIT;
            DTRACE_PROBE1(mtpg, dbwriter__softcommit, xid);
        }
        
        TransBlockNumberSetXidStatus(LogRelation, xid, setstate);
        
        RelationClose(LogRelation);

        ResetThreadState(GetMyThread());
    }
    
    /*  this needs to happen this way so that the
     * proper serial order is maintained, if soft commits are made before
     * acquiring the lock on the write grp, it is possible for a hard commit
     * on an update to race ahead of a soft commit
     * this is ok because if even if the DBWriter
     * sets the commit state first, soft commits do
     * not overwrite hard commits
     */
    /*  sync local buffers for the caller  */
    LocalBufferSync();
}

int LogBuffers(WriteGroup list) {
    int             i;
    BufferDesc     *bufHdr;
    int             releasecount = 0, freecount = 0;
    int             buffer_hits = 0;
    int defer_count = 0;
    IOStatus        iostatus;

    smgrbeginlog();

    if (TraceBufferFlushEnabled()) {
        fprintf(stderr, "FLUSH: LogBuffers start gen=%ld\n", list->generation);
        fflush(stderr);
    }
    for (i = 0, bufHdr = BufferDescriptors; i < MaxBuffers; i++, bufHdr++) {
        if (list->defers[i] > 0) {
            Assert(list->currstate == FLUSHING);
            defer_count++;
            continue;
        }
        /* Ignore buffers that were not dirtied by me */
        if (!list->buffers[i])
            continue;
        
        if ( CheckBufferId(bufHdr,
                           list->descriptions[i].blockNum,
                           list->descriptions[i].relId.relId,
                           list->descriptions[i].relId.dbId) ) {
            
            if (list->descriptions[i].relId.relId == list->LogId ||
                list->descriptions[i].relId.relId == list->VarId) {
                /* skip these, they do not belong in the
                 log and we don't want them replayed
                 */
                continue;
            }

            iostatus = LogBufferIO(bufHdr);
            if ( iostatus == IO_SUCCESS ) {
                long shadow_before = bufHdr->generation;
                Block blk = AdvanceBufferIO(list->generation, bufHdr, false);
                int copied = bufHdr->generation != shadow_before;
                buffer_hits++;
                if (TraceBufferFlushEnabled()) {
                    fprintf(stderr,
                            "FLUSH: log-write %s blk=%lu cart_gen=%ld shadow_gen=%ld shadow_before=%ld copied=%d\n",
                            bufHdr->blind.relname,
                            (unsigned long) bufHdr->tag.blockNum,
                            list->generation,
                            bufHdr->generation,
                            shadow_before,
                            copied);
                    fflush(stderr);
                    if (bufHdr->kind == RELKIND_INDEX)
                        TraceLoggedBtreePage(bufHdr->blind.relname,
                                             bufHdr->tag.blockNum,
                                             (Page) blk,
                                             copied,
                                             list->generation,
                                             bufHdr->generation);
                }
                if ( SM_FAIL == smgrlog(
                                        DEFAULT_SMGR,
                                        bufHdr->blind.dbname,
                                        bufHdr->blind.relname,
                                        list->descriptions[i].relId.dbId,
                                        list->descriptions[i].relId.relId,
                                        bufHdr->tag.blockNum,
                                        bufHdr->kind,
                                        blk
                                        )
                    ) {
                    elog(DEBUG, "DBWriter: buffer failed to log in smgr bufid:%d dbid:%ld relid:%ld blk:%ld",
                        bufHdr->buf_id,
                        bufHdr->tag.relId.dbId,
                        bufHdr->tag.relId.relId,
                        bufHdr->tag.blockNum);
                    ErrorBufferIO(iostatus,bufHdr);
                } else {
                    TerminateBufferIO(iostatus,bufHdr);
                }
            } else if (IsDirtyBufferIO(bufHdr)) {
                /*
                 * LogBufferIO only fails when the page is not dirty or already
                 * in BM_IO_ERROR. A page that is dirty again lost the race and
                 * still needs an error so the writer does not forget it.
                 */
                elog(DEBUG, "DBWriter: dirty bufid:%d dbid:%ld relid:%ld blk:%ld",
                    bufHdr->buf_id,
                    bufHdr->tag.relId.dbId,
                    bufHdr->tag.relId.relId,
                    bufHdr->tag.blockNum);
                ErrorBufferIO(iostatus,bufHdr);
            } else {
                /*
                 * Write-group membership with no dirty image. The page was
                 * already logged (BM_LOGGED) or never needed a log. Marking
                 * BM_IO_ERROR here poisons a good buffer: BufferAlloc then
                 * retries the same descriptor forever.
                 */
                elog(DEBUG, "DBWriter: not dirty bufid:%d dbid:%ld relid:%ld blk:%ld",
                    bufHdr->buf_id,
                    bufHdr->tag.relId.dbId,
                    bufHdr->tag.relId.relId,
                    bufHdr->tag.blockNum);
            }
        } else {
            iostatus = LogBufferIO(bufHdr);
            
            if (iostatus == IO_SUCCESS) {
                elog(DEBUG, "log buffers - this should not happen");
                elog(DEBUG, "dbid:%ld relid:%ld blk:%ld",
                        list->descriptions[i].relId.dbId,
                        list->descriptions[i].relId.relId,
                        list->descriptions[i].blockNum);
                elog(DEBUG, "dbid:%ld relid:%ld blk:%ld",
                        bufHdr->tag.relId.dbId,
                        bufHdr->tag.relId.relId,
                        bufHdr->tag.blockNum);
                TerminateBufferIO(iostatus,bufHdr);
            } else {
                if (IsDirtyBufferIO(bufHdr)) {
                    elog(DEBUG, "DBWriter: bufferid dropped bufid:%d dbid:%ld relid:%ld blk:%ld",
                        bufHdr->buf_id,
                        bufHdr->tag.relId.dbId,
                        bufHdr->tag.relId.relId,
                        bufHdr->tag.blockNum);
                    ErrorBufferIO(iostatus,bufHdr);
                }
                /* releasing because this is no longer part of the write group
                   it has been flushed out for reuse due to buffer exhaustion
                */
                list->buffers[i] = false;
                while (list->release[i] > 0) {
                    if ( ManualUnpin(bufHdr, false)) {
                        freecount++;
                    }
                    list->release[i]--;
                    releasecount++;
                }
            }
        }
    }

    if (TraceBufferFlushEnabled()) {
        fprintf(stderr, "FLUSH: LogBuffers end gen=%ld pages=%d release=%d free=%d defer=%d\n",
                list->generation, buffer_hits, releasecount,freecount,defer_count);
        fflush(stderr);
    }
    smgrcommitlog();
    
    DTRACE_PROBE3(mtpg, dbwriter__loggedbuffers, buffer_hits, releasecount, freecount);
    return freecount;
}

int ClearLogs(WriteGroup list) {
  /* After a durable sync, rotate to a new uniquely named shadow log
   * and delete all but the last three files.
   */
    if ( list->loggable ) {
//        smgrexpirelogs();
        smgrremovelogs(list->flushCount + 1);
    }

    return 0;
}

int SyncBuffers(WriteGroup list,bool forcommit) {
    int             i;
    BufferDesc     *bufHdr;
    int             releasecount = 0,freecount = 0;
    int buffer_hits = 0;
    IOStatus        iostatus;
    int status = STATUS_OK;
    int iomode = WRITE_NORMAL;


    if (TraceBufferFlushEnabled()) {
        fprintf(stderr, "FLUSH: SyncBuffers start gen=%ld forcommit=%d\n",
                list->generation, forcommit ? 1 : 0);
        fflush(stderr);
    }
    for (i = 0, bufHdr = BufferDescriptors; i < MaxBuffers; i++, bufHdr++) {
        bool written = false;
      /* Ignore buffers that were not dirtied by me */
        if (!list->buffers[i] || list->defers[i] > 0)
            continue;

        if ( !forcommit ) {
            bool   exit = false;
            pthread_mutex_lock(&list->checkpoint);
            exit = (list->currstate == COMPLETED);
            iomode = WRITE_NORMAL; /* just in case, piggyback on this mutex */
            pthread_mutex_unlock(&list->checkpoint);
            if ( exit ) break;
        }

        /* no need to lock mutex, buffer is referenced by sync grp */
        if ( CheckBufferId(bufHdr, 
                list->descriptions[i].blockNum, 
                list->descriptions[i].relId.relId, 
                list->descriptions[i].relId.dbId) ) {
            /*
             * skip over any log relation
             * buffer
             */
            if (
                bufHdr->tag.relId.relId == list->LogId ||
                bufHdr->tag.relId.relId == list->VarId
                ) {
                /* VarRel should always be flushing out writes */
                /* LogRel should only get here due to soft commits holding
                 * a reference to the buffer though actually write to
                 * disk of the sync group
                 */
                if ( !forcommit ) continue;
                iostatus = WriteBufferIO(bufHdr, WRITE_FLUSH);
                if (iostatus == IO_SUCCESS) {
                    Relation target = RelationIdGetRelation(bufHdr->tag.relId.relId,DEFAULTDBOID);
                    Block blk = AdvanceBufferIO(list->generation, bufHdr, true);
                    status = smgrflush(target->rd_smgr, bufHdr->tag.blockNum, blk);
                    written = true;
                    
                    if (status == SM_FAIL) {
                        ErrorBufferIO(iostatus, bufHdr);
                        elog(FATAL, "BufferSync: cannot write %lu for %s-%s",
                                bufHdr->tag.blockNum, bufHdr->blind.relname, bufHdr->blind.dbname);
                    } else {
                        buffer_hits++;
                        TerminateBufferIO(iostatus, bufHdr);
                    }
                    
                    RelationClose(target);
                } else {            
                    elog(DEBUG, "DBWriter: buffer failed to in sync bufid:%d dbid:%ld relid:%ld blk:%ld",
                            bufHdr->buf_id,
                            bufHdr->tag.relId.dbId,
                            bufHdr->tag.relId.relId,
                            bufHdr->tag.blockNum);
                    ErrorBufferIO(iostatus, bufHdr);
                }
            } else {
                PathCache*   cache = NULL;

                cache = GetPathCache((forcommit) ?  HASH_ENTER : HASH_FIND, bufHdr->blind.relname, bufHdr->blind.dbname, bufHdr->tag.relId.relId, bufHdr->tag.relId.dbId);
                if ( !cache ) continue;
                
                if (cache->keepstats && cache->tolerance > 0.0) {
                    /*
                     * use the release count as
                     * approximate number of writes to
                     * this page and factor it with the
                     * tolerance MKS  02.16.2003
                     */
                    cache->accesses += ((list->release[i] * cache->tolerance) * (hgc_update / hgc_factor));
                }

                iostatus = WriteBufferIO(bufHdr, iomode);
                if (iostatus == IO_CLEAN) {
                    /* Not logged. The shadow log has no image of this page. */
                } else if ( iostatus == IO_SUCCESS) {                    
                    buffer_hits++;
                    cache->commit = true;
                    Block blk = AdvanceBufferIO(list->generation, bufHdr, true);
                    
                    if (blk == 0)
                        elog(FATAL, "[DBWriter]bad buffer block in buffer sync");
                    
                    if ( cache == NULL ) {
                        elog(FATAL, "NULL PathCache");
                    }

                    if (TraceBufferFlushEnabled()) {
                        fprintf(stderr,
                                "FLUSH: sync-write %s blk=%lu cart_gen=%ld shadow_gen=%ld\n",
                                bufHdr->blind.relname,
                                (unsigned long) bufHdr->tag.blockNum,
                                list->generation,
                                bufHdr->generation);
                        fflush(stderr);
                    }
                    status = smgrwrite(cache->smgrinfo, bufHdr->tag.blockNum,
                            blk);
                    written = true;
                    
                    if ( status == SM_FAIL ) {
                        ErrorBufferIO(iostatus, bufHdr);
                        elog(FATAL, "BufferSync: cannot write %lu for %s-%s",
                                bufHdr->tag.blockNum, bufHdr->blind.relname, bufHdr->blind.dbname);
                    } else {
                        TerminateBufferIO(iostatus, bufHdr);
                    }
                } else {
                    elog(WARN, "DBWriter: buffer failed sync for writeio bufid:%d dbid:%ld relid:%ld blk:%ld",
                        bufHdr->buf_id,
                        bufHdr->tag.relId.dbId,
                        bufHdr->tag.relId.relId,
                        bufHdr->tag.blockNum);
                    ErrorBufferIO(iostatus, bufHdr);
                }
            }
        } else {
            iostatus = WriteBufferIO(bufHdr, WRITE_FLUSH);
            if (iostatus == IO_SUCCESS) {
                elog(DEBUG, "already out dbid:%ld relid:%ld blk:%ld",
                        list->descriptions[i].relId.dbId,
                        list->descriptions[i].relId.relId,
                        list->descriptions[i].blockNum);
                elog(DEBUG, "now dbid:%ld relid:%ld blk:%ld",
                        bufHdr->tag.relId.dbId, bufHdr->tag.relId.relId, bufHdr->tag.blockNum);
                TerminateBufferIO(iostatus, bufHdr);
            } else {
                elog(DEBUG, "DBWriter: buffer failed to writeio2 bufid:%d dbid:%ld relid:%ld blk:%ld",
                    bufHdr->buf_id,
                    bufHdr->tag.relId.dbId,
                    bufHdr->tag.relId.relId,
                    bufHdr->tag.blockNum);
                ErrorBufferIO(iostatus, bufHdr);
            }
        }

        list->buffers[i] = false;
        while(list->release[i] > 0) {
            if ( ManualUnpin(bufHdr, false) ) {
                freecount++;
            }
            list->release[i]--;
            releasecount++;
        }
    }

    DTRACE_PROBE4(mtpg, dbwriter__syncedbuffers, buffer_hits, releasecount, freecount, forcommit);
    return freecount;
}

bool FlushAllDirtyBuffers(bool wait) {
    if (!db_inited) {
        return false;
    }

    WriteGroup          cart =  GetCurrentWriteGroup(false);
    int                 releasecount = 0;
    bool iflushed = false;
        
    if (TraceBufferFlushEnabled()) {
        fprintf(stderr,
                "FLUSH: FlushAllDirtyBuffers called cart_gen=%ld\n",
                cart->generation);
        fflush(stderr);
    }

    if (IsDBWriter()) {
        while ( FlushWriteGroup(cart) == 0 ) {
            UnlockWriteGroup(cart);
            cart = GetNextTarget(cart);
        }
        DTRACE_PROBE2(mtpg, dbwriter__circularflush, sync_buffers, releasecount);
        elog(DEBUG, "released %d", releasecount);
    } else {      
        if ( cart->currstate != FLUSHING ) {
            SignalDBWriter(cart);
            cart->currstate = FLUSHING;
            iflushed = true;
        }
        while ( wait && cart->currstate == FLUSHING ) {
            pthread_cond_wait(&cart->broadcaster, &cart->checkpoint);
        }
    }
    
    UnlockWriteGroup(cart);
    
    return iflushed;
}

static PathCache* GetPathCache(HASHACTION  mode, char *relname, char *dbname, Oid bufrel, Oid bufdb) {
    PathCache*       target = NULL;
    DBKey           key;
    bool            found;
    
    key.relid = bufrel;
    key.dbid = bufdb;
    
    target = hash_search(db_table, &key, mode, &found);

    if ( target == NULL ) return NULL;
    
    if (!found) {
        target->accesses = 0;
        target->tolerance = 0.0;
        target->idle_count = 0;
        
        target->smgrinfo = smgropen(DEFAULT_SMGR, dbname, relname, bufdb, bufrel);
        if ( target->smgrinfo == NULL ) {
            elog(ERROR, "failed to open required file");
        }
        target->refresh = true;
        target->keepstats = true;
        target->commit = false;
        
        strcpy(NameStr(target->relname), relname);
        strcpy(NameStr(target->dbname), dbname);
    }
    
    if (target->keepstats && target->refresh) {
        double          check = 0.0;
        
        check = GetUpdateFactor(bufrel, bufdb, relname, dbname, target->tolerance, &target->keepstats);
        if ( !target->keepstats ) {
            target->refresh = false;
        } else {
            if ( check > 0.0 ) {
                target->refresh = false;
                DTRACE_PROBE4(mtpg, dbwriter__tolerance, relname, dbname, &check, &target->tolerance);
                target->tolerance = check;
            }
        }
    }
    
    return target;
}

static WriteGroup GetNextTarget(WriteGroup last) {
    /*
     * if currstate is COMPLETED that means there are too many threads or
     * too few write groups, or we are shutting down ( see
     * DestroyWriteGroup )
     */
    WriteGroup     cart = (last) ? last->next : log_group;
    
    if (cart != last) {
        pthread_mutex_lock(&cart->checkpoint);
        if (!cart->locked) {
            cart->owner = pthread_self();
            cart->locked = true;
        }
    }

    return cart;
}

void
PanicStopDBWriter(void)
{
    stopped = true;
}

void ShutdownDBWriter(void) {
    WriteGroup     cart = NULL;
    
    if ( !db_inited ) return;
    
    cart = GetCurrentWriteGroup(false);
    
    stopped = true;
    
    cart->isTransFriendly = false;
    cart->loggable = false;
    
    SignalDBWriter(cart);
    
    UnlockWriteGroup(cart);
    
    /* now join all DBWriter threads  */
    for (writercount -= 1; writercount >= 0; writercount--) {
        void           *ret;
        pthread_join(writerid[writercount], &ret);
    }
    /* now destroy all the write groups  */
    {
        WriteGroup     target = GetNextTarget(NULL);
        
        while (target != NULL) {
            target = DestroyWriteGroup(target);
        }

        DestroyWriteGroup(sync_group);
        
    }
    /* free id memory. The join loop leaves writercount at -1; a later
     * DBCreateWriterThread would otherwise realloc the freed pointer. */
    if (writerid != NULL)
        os_free(writerid);
    writerid = NULL;
    writercount = 0;
    db_inited = false;
    sync_group = NULL;
    log_group = NULL;
    db_table = NULL;
    db_cxt = NULL;
}

bool IsDBWriter() {
    THREAD*   thread = GetMyThread();
    if (thread != NULL) {
        return (thread->ttype == DBWRITER_THREAD) ? true : false;
    } else {
        pthread_t       tid = pthread_self();
        int             i = 0;
        
        for (i = 0; i < writercount; i++) {
            if (pthread_equal(writerid[i], tid) != 0) {
                return true;
            }
        }
        return false;
    }
}

int SignalDBWriter(WriteGroup  cart) {
    switch (cart->currstate) {
        case (NOT_READY):
            cart->currstate = READY;
            break;
        case (COMPLETED):
            cart->currstate = READY;
            break;
        case (WAITING):
            cart->currstate = READY;
            if (pthread_cond_signal(&cart->gate) != 0) {
                elog(WARN, "[DBWriter]problem waking db writer");
            }
            break;
        case (READY):
            break;
        case (PRIMED):
            break;
        case (FLUSHING):          
            break;
        default:
            elog(FATAL, "DBWriter in the wrong state");
            break;
    }
    return 0;
}

WriteGroup GetCurrentWriteGroup(bool forcommit) {
    WriteGroup     cart = NULL;
    
    cart = GetNextTarget(cart);
    while (
            cart != log_group || 
            (
                cart->currstate == RUNNING ||
                /*cart->currstate == FLUSHING ||  FLUSHING should be fine because it is not a completed state */
                cart->currstate == LOGGED ||
                cart->currstate == SYNCED ||
                cart->currstate == COMPLETED ||
                cart->currstate == DEAD 
            ) ||
            ( forcommit && cart->numberOfTrans == maxtrans )
          ) {
        UnlockWriteGroup(cart);
        cart = GetNextTarget(cart);
    }

    return cart;
}

int UnlockWriteGroup(WriteGroup  cart) {
    int             err;
    
    if ((err = pthread_mutex_unlock(&cart->checkpoint))) {
        elog(FATAL, "[DBWriter]error unlocking cart");
    } else {
        cart->owner = 0;
        cart->locked = false;
    }
    return err;
}

void ResetAccessCounts(Oid relid, Oid dbid) {
    DBKey key;
    bool  found;
    
    key.relid = relid;
    key.dbid = dbid;
    
    PathCache*  pc = hash_search(db_table, &key, HASH_FIND, &found);
    
    if (found) {
        pc->accesses = 0;
        pc->tolerance = 0.0;
        pc->refresh = true;
        pc->keepstats = true;
    }
}

static void PathCacheCompleteWalker(PathCache *tinfo, int dummy) {
    double          turnstyle = MaxBuffers;
    
    if ( tinfo->commit) {
        if ( tinfo->keepstats ) {
            turnstyle *= (hgc_threshold / tinfo->accesses);
            double check = prandom();
            DTRACE_PROBE4(mtpg, dbwriter__accesses, NameStr(tinfo->relname), NameStr(tinfo->dbname), &check, &turnstyle);
            if (!stopped && check < (MAX_RANDOM_VALUE / turnstyle)) {
                DTRACE_PROBE3(mtpg, dbwriter__vacuumactivation, NameStr(tinfo->relname), NameStr(tinfo->dbname), tinfo->accesses);
                SetFreespacePending(tinfo->key.relid, tinfo->key.dbid);
                AddVacuumRequest( NameStr(tinfo->relname), NameStr(tinfo->dbname), tinfo->key.relid, tinfo->key.dbid);
                tinfo->accesses = 0;
                /*
                 * set freespace pending so
                 * that the system update
                 * factor does not update
                 * until new stats are
                 * available
                 */
                tinfo->tolerance = 0.0;
                tinfo->refresh = true;
            }
        }
        smgrsync(tinfo->smgrinfo);
        tinfo->commit = false;
        tinfo->idle_count = 0;
    } else {
        /*  if idle count > 30 cycles
         * forget about any stats collected
         * and remove the entry
         */
        if ( tinfo->idle_count++ > 100 ) {
            smgrclose(tinfo->smgrinfo);
            ForgetPathCache(tinfo->key.relid, tinfo->key.dbid);
        }
    }
}

/*  rely on locking at the relation level to protect from
 * removing a referenced freespace
 */
int ForgetPathCache(Oid relid, Oid dbid) {
    DBKey tag;
    bool found;
    
    tag.relid = relid;
    tag.dbid = dbid;
    
    hash_search(db_table, (char*)&tag, HASH_REMOVE, &found);
    
    if ( !found ) {
        /*
         * elog(WARN,"de-referencing unknown freespace %s-%s",RelationGetRelationName(rel),GetDatabaseName());
         */
    } else {
        
    }
    
    return 0;
}

int ResetThreadState(THREAD*  thread) {
    pthread_mutex_lock(&thread->gate);
    thread->state = TRANS_DEFAULT;
    thread->xid = InvalidTransactionId;
    thread->xmin = InvalidTransactionId;
    pthread_mutex_unlock(&thread->gate);
    return 0;
}

static int TakeFileSystemSnapshot(char* sys) {
    char        cmd[512];
    snprintf(cmd,512,"takesnapshot %s",sys);
    return my_system(cmd);    
}

char* RequestSnapshot(char* cmd) {
    WriteGroup     cart = GetCurrentWriteGroup(false);
    
    if (cart->currstate == RUNNING) {
        UnlockWriteGroup(cart);
        elog(FATAL, "[DBWriter]commit in running state");
    }
    
    if ( cart->snapshot != NULL ) {
        UnlockWriteGroup(cart);
        elog(ERROR, "[DBWriter] Snapshot already requested");
    }
    
    cart->snapshot = cmd;
        
    SignalDBWriter(cart);

    while ( cart->snapshot == cmd ) {
        if (pthread_cond_wait(&cart->broadcaster, &cart->checkpoint)) {
            UnlockWriteGroup(cart);
            elog(FATAL, "[DBWriter]cannot attach to db write thread");
        }
    }
    UnlockWriteGroup(cart);
    
    return NULL;
}

long
GetFlushTime() {
    return flush_time;
}
