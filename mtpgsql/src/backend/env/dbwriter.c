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
#include <sys/types.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <math.h>

#include "c.h"
#include "postgres.h"
#include "env/env.h"
#include "env/connectionutil.h"

#include "config.h"
#include "miscadmin.h"
#include "storage/buf_internals.h"
#include "storage/block.h"
#include "storage/bufmgr.h"
#include "storage/smgr.h"
#include "storage/bufpage.h"
#include "access/transam.h"
#include "storage/bufmgr.h"
#include "storage/spin.h"
#include "storage/multithread.h"
#include "storage/sinvaladt.h"
#include "utils/relcache.h"
#include "utils/memutils.h"
#include "utils/portal.h"
#include "utils/inval.h"
#include "catalog/catname.h"
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

enum passtype {
    HEAP_PASS,
    INDEX_PASS,
    NONINDEX_PASS,
    ALL_PASS
};

typedef enum passtype PassType;

typedef enum writerstates WriterState;

typedef struct writegroups* WriteGroup;

struct writegroups {
    WriterState                         currstate;
    bool*				buffers;
    bool*				logged;
    int*				release;
    BufferTag*                          descriptions;
    THREAD**				WaitingThreads;
    TransactionId                       LastSoftXid;
    TransactionId*                      transactions;
    int*				transactionState;
    int 				numberOfTrans;
    
    pthread_t                           owner;
    
    pthread_cond_t			gate;
    
    pthread_mutex_t			checkpoint;
    pthread_cond_t			broadcaster;
    bool				dotransaction;
    
    bool                                loggable;
    
    bool				isTransFriendly;
    bool				locked;
    bool                                flush_run;
    /*  for convenience, cache these here  */
    Relation                            LogRelation;
    Relation                            VarRelation;
    
    char*                               snapshot;
    
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

static  PathCache* GetPathCache(WriteGroup  cart, char *relname , char *dbname, Oid bufrel, Oid bufdb);
static void CommitPackage(WriteGroup  cart);
static int SignalDBWriter(WriteGroup  cart);
static WriteGroup GetCurrentWriteGroup(bool forcommit);
static int UnlockWriteGroup(WriteGroup  cart);
static WriteGroup GetNextTarget(WriteGroup last);
static void* DBWriter(void* arg);

static void DBTableInit();
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
static int ClearLogs(WriteGroup list);
static int LogBuffers(WriteGroup list, PassType type);
static int SyncBuffers(WriteGroup list);
static void ResetWriteGroup(WriteGroup cart);
static int ForgetPathCache(Oid relid, Oid dbid);

static int MergeWriteGroups(WriteGroup target, WriteGroup src);
static int ResetThreadState(THREAD*  t);

static int TakeFileSystemSnapshot(char* cmd);

extern int      NBuffers;
extern bool     TransactionSystemInitialized;

static int      BufferFlushCount;

static WriteGroup groups;
static WriteGroup sync_group;   /*  a holder for buffers that need to be synced */
int    sync_buffers = 0;

static int      groupcount;

static pthread_attr_t writerprops;

/* static pthread_mutex_t groupguard;  */
/* static pthread_cond_t swing;  */

static bool     logging = false;
static bool     stopped = false;

static int      timeout = 400;
static int      sync_timeout = 5000;
static int      max_logcount = (512);

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

extern SLock   *SLockArray;
extern pthread_t lockowner;



/*
 * This thread writes out all buffers at transaction commit time Only one
 * thread is created at this time and two WriteGroups to collect information
 * about which buffers to write.  The point here is to maximize the number of
 * transaction commits that occur at a time.  We don't mind if the inserting
 * thread needs to wait a little bit for the other threads to register. or
 * for the preceeding write group to finish  MKS - 11/3/2000
 */

void DBWriterInit(int maxcount, int to, int thres, int update, int factor) {
    struct sched_param sched;
    stopped = false;
    int             sched_policy;
    maxtrans = MAXTRANS;
    if (maxcount > 0 && maxcount < (32 * 1024) /* don't be stupid check */)
        maxtrans = maxcount;
    if (to > 0)
        timeout = to;
    if (thres > 0)
        hgc_threshold = (double) thres;
    if (update > 0)
        hgc_update = (double) update;
    if (factor > 0)
        hgc_factor = (double) factor;
    
    elog(DEBUG, "[DBWriter]waiting time %li", timeout);
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
    /*	pthread_mutex_init(&groupguard, NULL);  */
    /*	pthread_cond_init(&swing, NULL);  */

    groups = CreateWriteGroup(maxtrans, NBuffers);
    groups->next = CreateWriteGroup(maxtrans, NBuffers);
    /* link in a circle  */
    groups->next->next = groups;

    sync_group = CreateWriteGroup(maxtrans, NBuffers);;

    DBTableInit();

    if (!IsMultiuser()) {
        logging = false;
        /* no logging so make sure everyone waits for sync */
        SetTransactionCommitType(SYNCED_COMMIT);
    } else {
        char*  sto = GetProperty("synctimeout");
        char*  mlog = GetProperty("maxlogcount");

        logging = true;

        if ( sto != NULL ) {
            /*  user provided sync timeout in microsecounds  */
            sync_timeout = atoi(sto);
            if ( sync_timeout <= 0 ) sync_timeout = 5000;
        }

        max_logcount = NBuffers;

        if ( mlog != NULL ) {
            /*  user provided sync timeout in microsecounds  */
            max_logcount = atoi(mlog);
            if ( max_logcount <= 0 ) max_logcount = NBuffers;
        }
    }
}

static void DBTableInit() {
    HASHCTL ctl;
    HTAB* temptable;
    MemoryContext  hash_cxt, old;
    
    db_cxt = AllocSetContextCreate(GetEnvMemoryContext(),
            "DBWriterMemoryContext",
            ALLOCSET_DEFAULT_MINSIZE,
            ALLOCSET_DEFAULT_INITSIZE,
            ALLOCSET_DEFAULT_MAXSIZE);
    
    old = MemoryContextSwitchTo(db_cxt);
    
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
    cart->transactions = os_malloc(sizeof(TransactionId) * trans);
    cart->transactionState = os_malloc(sizeof(int) * trans);
    cart->buffers = os_malloc(sizeof(bool) * buffers);
    cart->logged = os_malloc(sizeof(bool) * buffers);
    cart->release = os_malloc(sizeof(int) * buffers);
    cart->descriptions = os_malloc(sizeof(BufferTag) * buffers);
    
    cart->numberOfTrans = 0;
    cart->currstate = NOT_READY;
    cart->snapshot = NULL;
    
    ResetWriteGroup(cart);
    
    cart->LogRelation = NULL;
    cart->VarRelation = NULL;
    
    cart->LastSoftXid = InvalidTransactionId;
    
    return cart;
}

WriteGroup DestroyWriteGroup(WriteGroup w) {
    pthread_cond_destroy(&w->gate);
    
    pthread_mutex_destroy(&w->checkpoint);
    pthread_cond_destroy(&w->broadcaster);
    
    os_free(w->WaitingThreads);
    os_free(w->transactions);
    os_free(w->transactionState);
    os_free(w->buffers);
    os_free(w->logged);
    os_free(w->release);
    os_free(w->descriptions);
    
    w->currstate = DEAD;
    return w->next;
}


void DBCreateWriterThread() {
    int             prio;
    writerid = os_realloc(writerid, sizeof(pthread_t) * (writercount + 1));
    if (pthread_create(&writerid[writercount++], &writerprops, DBWriter, NULL) != 0) {
        elog(FATAL, "[DBWriter]could not create db writer\n");
    }
}

void* DBWriter(void *jones) {
    int             timerr;
    char            dbuser[255];
    int             i = 0;
    bool            leak = false;
    WriteGroup     cart = NULL;
    WriteGroup     last = NULL;
    Env            *env = CreateEnv(NULL);
    
    
    SetEnv(env);
    env->Mode = InitProcessing;
    
    ResetWriteGroup(sync_group);
    sync_buffers = 0;
    
    MemoryContextInit();
    
    SetDatabaseName("template1");
    if (!IsBootstrapProcessingMode()) {
        GetRawDatabaseInfo("template1", &env->DatabaseId, dbuser);
    }
    
    InitThread(DBWRITER_THREAD);
    
    RelationInitialize();
    
    CallableInitInvalidationState();
    
    env->Mode = NormalProcessing;
    
    GetSnapshotHolder()->ReferentialIntegritySnapshotOverride = true;
    
    MemoryContextSwitchTo(MemoryContextGetTopContext());
    
    last = NULL;
    
    while (!stopped) {
        int     releasecount = 0;
        
        cart = GetNextTarget(last);
        
        if (setjmp(env->errorContext) != 0) {
            elog(FATAL, "error in dbwriter");
        }
        
        while ( CheckWriteGroupState(cart, (sync_buffers > 0)) ) {
            /*  wait to see if we are ready to go  */
        }
        
        AdvanceWriteGroupQueue(cart);
        
        Assert(cart->currstate == PRIMED || cart->currstate == READY);
        
        cart->currstate = RUNNING;
        
        UnlockWriteGroup(cart);
        
        releasecount = LogWriteGroup(cart);
        
        if ( GetProcessingMode() == NormalProcessing && !cart->flush_run && cart->loggable && (sync_buffers < max_logcount) ) {
            /*  move buffer syncs to the sync cart */
            sync_buffers += MergeWriteGroups(sync_group, cart);
        } else {
            /* transfer any saved writes back to the current cart and sync all buffers */
            if ( sync_buffers > 0 ) {
                MergeWriteGroups(cart, sync_group);
                ResetWriteGroup(sync_group);
                sync_buffers = 0;
            }
            releasecount += SyncWriteGroup(cart);
        }
        
        FinishWriteGroup(cart);
        
        last = cart;
        /* no invalids generated by DBWriter mean anything  */
        DiscardInvalid();
    }
    
    if ( sync_buffers > 0 ) {
        MergeWriteGroups(cart->next, sync_group);
        ResetWriteGroup(sync_group);
        sync_buffers = 0;
    }
    
    CleanupWriteGroup(cart->next);
    CommitPackage(cart->next);
    CleanupWriteGroup(cart);
    CommitPackage(cart);
    
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
                waittime.tv_sec = time(NULL) + (sync_timeout / 1000);
                waittime.tv_nsec = (sync_timeout % 1000) * 1000000;
                timerr = pthread_cond_timedwait(&cart->gate, &cart->checkpoint, &waittime);
                if (timerr == ETIMEDOUT) {
                    cart->currstate = PRIMED;
                    cart->loggable = false;
                    return false;
                }
            } else {
                /*  waitng for a write signal */
                if (pthread_cond_wait(&cart->gate, &cart->checkpoint) != 0) {
                    perror("DBWRITER:");
                    elog(FATAL, "[DBWriter]could not wait for write signal\n");
                }
            }
            return true;
        case READY:
            timerr = 0;
            if (cart->isTransFriendly &&
                    !(stopped) &&
                    cart->numberOfTrans < maxtrans) {
                struct timespec waittime;
                waittime.tv_sec = time(NULL) + (timeout / 1000);
                waittime.tv_nsec = (timeout % 1000) * 1000000;
                cart->currstate = WAITING;
                timerr = pthread_cond_timedwait(&cart->gate, &cart->checkpoint, &waittime);
                if (timerr == ETIMEDOUT) {
                    cart->currstate = PRIMED;
                    return false;
                } else {
                    return true;
                }
            }
            return false;
        case PRIMED:
            return false;
        case FLUSHING:
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
    } else {
        elog(FATAL, "DB write group in the wrong state");
    }
    
    if ( groups != cart ) {
        elog(FATAL, "Advance Writer Group called with the wrong group");
    }
    
    groups = cart->next;
    pthread_mutex_unlock(&cart->next->checkpoint);    
}

int LogWriteGroup(WriteGroup cart) {
    int releasecount = 0;
    int trans_logged = 0;
    
    if ( logging ) {
        /*  insert the buffers in the shadow log
         * and log the transaction state, then release
         * waiters
         */
        
/*
        releasecount += LogBuffers(cart, NONINDEX_PASS);
 *      releasecount = LogBuffers(cart, INDEX_PASS);
*/
        releasecount = LogBuffers(cart,ALL_PASS);
        
        pthread_mutex_lock(&cart->checkpoint);
        cart->currstate = LOGGED;
        pthread_mutex_unlock(&cart->checkpoint);
        
        if ( cart->dotransaction ) {
            trans_logged = LogTransactions(cart);
        }
        
        pthread_mutex_lock(&cart->checkpoint);
        cart->dotransaction = false;
        pthread_cond_broadcast(&cart->broadcaster);
        pthread_mutex_unlock(&cart->checkpoint);
    }
    
    return releasecount;
}

int SyncWriteGroup(WriteGroup cart) {
    /*  syncing the buffers */
    int releases = SyncBuffers(cart);
    int trans_logged = 0;
    
    /* need to lock to release  */
    pthread_mutex_lock(&cart->checkpoint);
    cart->currstate = SYNCED;
    pthread_mutex_unlock(&cart->checkpoint);
    
    CommitPackage(cart);
    
    if ( logging ) {
        ClearLogs(cart);
    }
    
    if (cart->dotransaction && TransactionSystemInitialized) {
        trans_logged = LogTransactions(cart);
    }
    
    return releases;
}

int FinishWriteGroup(WriteGroup cart) {
    if ( cart->snapshot ) {
        TakeFileSystemSnapshot(cart->snapshot);
    }
    
    pthread_mutex_lock(&cart->checkpoint);
    cart->currstate = COMPLETED;
    cart->flush_run = false;
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
    
    memset(cart->buffers, 0, sizeof(bool) * NBuffers);
    memset(cart->logged, 0, sizeof(bool) * NBuffers);
    memset(cart->release, 0, sizeof(int) * NBuffers);
    memset(cart->descriptions, 0, sizeof(BufferTag) * NBuffers);
    
    memset(cart->transactions, 0, sizeof(TransactionId) * maxtrans);
    memset(cart->transactionState, 0, sizeof(int) * maxtrans);
    memset(cart->WaitingThreads, 0, sizeof(Env *) * maxtrans);
    
    cart->numberOfTrans = 0;
    cart->dotransaction = true;
//    cart->currstate = NOT_READY;
    cart->isTransFriendly = true;
    cart->flush_run = false;
    cart->loggable = true;
    
    if ( cart->snapshot != NULL ) {
        os_free(cart->snapshot);
        cart->snapshot = NULL;
    }
}

int MergeWriteGroups(WriteGroup target, WriteGroup src) {
    BufferDesc*  bufHdr;
    int i = 0;
    int moved = 0;
    bool    multiuser = IsMultiuser();
    
    pthread_mutex_lock(&target->checkpoint);
    for (i = 0, bufHdr = BufferDescriptors; i < NBuffers; i++, bufHdr++) {
        
        if ( !src->buffers[i] ) {
            Assert(src->release[i] == 0);
            continue;
        } else {
            moved++;
        }
        
        if ( !target->buffers[i] ) {
            memmove(&target->descriptions[i], &src->descriptions[i], sizeof(BufferTag));
            target->buffers[i] = true;
        }
        
        if ( target->descriptions[i].relId.dbId != src->descriptions[i].relId.dbId ||
                target->descriptions[i].relId.relId != src->descriptions[i].relId.relId ||
                target->descriptions[i].blockNum != src->descriptions[i].blockNum ) {
            elog(NOTICE, "investigate buffer write group merge");
            elog(NOTICE, "dbid:%d relid:%d blk:%d\n",
                    target->descriptions[i].relId.dbId,
                    target->descriptions[i].relId.relId,
                    target->descriptions[i].blockNum);
            elog(NOTICE, "dbid:%d relid:%d blk:%d\n",
                    src->descriptions[i].relId.dbId,
                    src->descriptions[i].relId.relId,
                    src->descriptions[i].blockNum);
        } else {
            target->release[i] += src->release[i];
        }
    }
    pthread_mutex_unlock(&target->checkpoint);
    
    return moved;
}
 
int LogTransactions(WriteGroup cart) {
    int             i = 0;
    Buffer          buffer = InvalidBuffer;	/* buffer associated with block */
    Block           block;	/* block containing xstatus */
    
    if (cart->numberOfTrans == 0)
        return 0;
    
    for (i = 0; i < cart->numberOfTrans; i++) {
        BlockNumber     localblock = InvalidBlockNumber;
        
        if (cart->transactions[i] == DisabledTransactionId)
            continue;
        if (cart->transactions[i] == 0) {
            elog(FATAL, "zero transaction id");
        }
        DTRACE_PROBE1(mtpg, dbwriter__commit, cart->transactions[i]);
        localblock = TransComputeBlockNumber(cart->LogRelation, cart->transactions[i]);
        
        if (buffer == InvalidBuffer || localblock != BufferGetBlockNumber(buffer)) {
            if (buffer != InvalidBuffer) {
                FlushBuffer(cart->LogRelation,buffer,true);
            }
            buffer = ReadBuffer(cart->LogRelation, localblock);
            if (!BufferIsValid(buffer))
                elog(ERROR, "[DBWriter]bad buffer read in transaction logging");
            
            block = BufferGetBlock(buffer);
        }
        /*
         * ---------------- get the block containing the transaction
         * status ----------------
         */
        
        TransBlockSetXidStatus(block, cart->transactions[i], cart->transactionState[i]);
        if ( cart->WaitingThreads[i] != NULL ) ResetThreadState(cart->WaitingThreads[i]);
    }
    
    FlushBuffer(cart->LogRelation,buffer,true);
    
    DTRACE_PROBE1(mtpg, dbwriter__logged, i);
    
    return i;
}


void RegisterBufferWrite(BufferDesc * bufHdr, bool release) {
    WriteGroup     cart = GetCurrentWriteGroup(false);
    
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
            elog(FATAL, "Invalid buffer registered for write bufid:%d dbid:%d relid:%d blk:%d\n",
                    bufHdr->buf_id,
                    bufHdr->tag.relId.dbId,
                    bufHdr->tag.relId.relId,
                    bufHdr->tag.blockNum);
        }
    } else {
        if (
                (
                bufHdr->tag.relId.dbId != cart->descriptions[bufHdr->buf_id].relId.dbId ||
                bufHdr->tag.relId.relId != cart->descriptions[bufHdr->buf_id].relId.relId ||
                bufHdr->tag.blockNum != cart->descriptions[bufHdr->buf_id].blockNum
                )
                ) {
            elog(NOTICE, "register write should not happen");
            elog(FATAL, "dbid:%d relid:%d blk:%d\n",
                    bufHdr->tag.relId.dbId,
                    bufHdr->tag.relId.relId,
                    bufHdr->tag.blockNum);
        }
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
}
 
void CommitDBBufferWrites(TransactionId xid, int setstate) {
    WriteGroup     cart;
    
    TransactionId   soft_xid = InvalidTransactionId;
    
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
        if ( !IsLoggable() ) {
            cart->loggable = false;
        }
        
        cart->transactions[position] = xid;
        cart->transactionState[position] = setstate;
        
        SignalDBWriter(cart);
        
        /* no need to wait around if we are aborting */
        if ( AMI_OVERRIDE || IsTransactionSystemDisabled() || (setstate == XID_COMMIT && IsTransactionCareful()) ) {
            cart->WaitingThreads[position] = GetMyThread();
            if (pthread_cond_wait(&cart->broadcaster, &cart->checkpoint)) {
                UnlockWriteGroup(cart);
                elog(FATAL, "[DBWriter]cannot attach to db write thread");
            }
            setxid = false;
        } else {
            soft_xid = cart->LastSoftXid;
            cart->LastSoftXid = xid;
        }
    }
    
    UnlockWriteGroup(cart);
    
    if ( setxid ) {
        Relation        LogRelation = RelationNameGetRelation(LogRelationName, DEFAULTDBOID);
        if (setstate == XID_COMMIT ) {
            setstate = XID_SOFT_COMMIT;
            DTRACE_PROBE1(mtpg, dbwriter__softcommit, xid);
        }
        
        TransBlockNumberSetXidStatus(LogRelation, xid, setstate);
        
        RelationClose(LogRelation);
        /*  this makes sure that all soft commits come though this section of code in a
         * serial fashion.  Basically, check to make sure that the prior
         * soft commit is done commiting by checking the thread state
         */
        if ( setstate == XID_SOFT_COMMIT ) {
            /* wait for the soft_xid just before this one to commit before proceeding
             * to insure proper serialization */
            XactLockTableWait(soft_xid);
            ResetThreadState(GetMyThread());
        }
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

int LogBuffers(WriteGroup list, PassType type) {
    int             i;
    BufferDesc     *bufHdr;
    int             releasecount = 0;
    int             buffer_hits = 0;
    IOStatus        iostatus;

    smgrbeginlog();
    for (i = 0, bufHdr = BufferDescriptors; i < NBuffers; i++, bufHdr++) {
        
        /* Ignore buffers that were not dirtied by me */
        if (!list->buffers[i])
            continue;
        
        
        if ( CheckBufferId(bufHdr,
                list->descriptions[i].blockNum,
                list->descriptions[i].relId.relId,
                list->descriptions[i].relId.dbId) ) {
            
            if (list->descriptions[i].relId.relId == list->LogRelation->rd_id ||
                    list->descriptions[i].relId.relId == list->VarRelation->rd_id) {
                /* skip these, they do not belong in the
             log and we don't want them replayed
                 */
                continue;
            }
            
            if ( type == NONINDEX_PASS ) {
                if ( bufHdr->kind == RELKIND_INDEX ) continue;
            } else if ( type == INDEX_PASS ) {
                if ( bufHdr->kind != RELKIND_INDEX ) continue;
            } else if ( type == HEAP_PASS ) {
                if ( bufHdr->kind != RELKIND_RELATION ) continue;
            }

            iostatus = LogBufferIO(bufHdr);
            if ( iostatus ) {
                PageInsertChecksum((Page) MAKE_PTR(bufHdr->data));
                buffer_hits++;
                if ( SM_FAIL == smgrlog(
                        DEFAULT_SMGR,
                        bufHdr->blind.dbname,
                        bufHdr->blind.relname,
                        list->descriptions[i].relId.dbId,
                        list->descriptions[i].relId.relId,
                        bufHdr->tag.blockNum,
                        bufHdr->kind,
                        (char *) MAKE_PTR(bufHdr->data)
                    )
                ) {
                    ErrorBufferIO(iostatus,bufHdr);
                } else {
                    list->logged[i] = true;
                    TerminateBufferIO(iostatus,bufHdr);
                }
            } else {
                /*
                 * buffer has already be written out
                 * go ahead and forget about writing out again
                 * and make sure to release the buffer because it
                 * won't be transfered to the sync_group
                 */
                if (!list->logged[i]) {
                    list->buffers[i] = false;
                    while (list->release[i] > 0) {
                        ManualUnpin(bufHdr, false);
                        list->release[i]--;
                        releasecount++;
                    }
                }
            }
        } else {
            iostatus = LogBufferIO(bufHdr);
            
            if (iostatus) {
                elog(NOTICE, "log buffers - this should not happen");
                elog(NOTICE, "dbid:%d relid:%d blk:%d\n",
                        list->descriptions[i].relId.dbId,
                        list->descriptions[i].relId.relId,
                        list->descriptions[i].blockNum);
                elog(NOTICE, "dbid:%d relid:%d blk:%d\n",
                        list->descriptions[i].relId.dbId,
                        list->descriptions[i].relId.relId,
                        bufHdr->tag.blockNum);
                TerminateBufferIO(iostatus,bufHdr);
            }
        }
    }
    
    smgrcommitlog();
    
    DTRACE_PROBE2(mtpg, dbwriter__loggedbuffers, buffer_hits, releasecount);
    return releasecount;
}

int ClearLogs(WriteGroup list) {
  /*  in init processing mode, don't clear the logs
   *  just add to it 
   */
    if ( GetProcessingMode() != InitProcessing ) {
        smgrexpirelogs();
    }
}

int SyncBuffers(WriteGroup list) {
    int             i;
    BufferDesc     *bufHdr;
    int             releasecount = 0;
    int buffer_hits = 0;
    IOStatus        iostatus;
    int status = STATUS_OK;


    for (i = 0, bufHdr = BufferDescriptors; i < NBuffers; i++, bufHdr++) {
      /* Ignore buffers that were not dirtied by me */
        if (!list->buffers[i])
            continue;
        
        /* no need to lock mutex, buffer is referenced by sync grp */
        if ( CheckBufferId(bufHdr, bufHdr->tag.blockNum, bufHdr->tag.relId.relId, bufHdr->tag.relId.dbId) ) {
            /*
             * skip over any log relation
             * buffer
             */
            if (
                bufHdr->tag.relId.relId == list->LogRelation->rd_id ||
                bufHdr->tag.relId.relId == list->VarRelation->rd_id
                ) {
                /* VarRel should always be flushing out writes */
                /* LogRel should only get here due to soft commits holding
                 * a reference to the buffer though actually write to
                 * disk of the sync group
                 */

                iostatus = WriteBufferIO(bufHdr, true);
                if (iostatus) {
                    SmgrInfo target = NULL;
                    
                    if ( bufHdr->tag.relId.relId == list->VarRelation->rd_id ) {
                        elog(NOTICE, "this should not happen");
                        target = list->VarRelation->rd_smgr;
                    } else if ( bufHdr->tag.relId.relId == list->LogRelation->rd_id ) {
                        /*  these are soft commits but flush them out */
                        target = list->LogRelation->rd_smgr;
                    }
                    
                    status = smgrflush(target, bufHdr->tag.blockNum, (char *) MAKE_PTR(bufHdr->data));
                    
                    if (status == SM_FAIL) {
                        ErrorBufferIO(iostatus, bufHdr);
                        elog(FATAL, "BufferSync: cannot write %lu for %s-%s",
                                bufHdr->tag.blockNum, bufHdr->blind.relname, bufHdr->blind.dbname);
                    } else {
                        buffer_hits++;
                        TerminateBufferIO(iostatus, bufHdr);
                    }
                    
                }
            } else {
                PathCache*   cache = NULL;

                cache = GetPathCache(list, bufHdr->blind.relname, bufHdr->blind.dbname, bufHdr->tag.relId.relId, bufHdr->tag.relId.dbId);
                if (cache->keepstats && cache->tolerance > 0.0) {
                    /*
                     * use the release count as
                     * approximate number of writes to
                     * this page and factor it with the
                     * tolerance MKS  02.16.2003
                     */
                    cache->accesses += ((list->release[i] * cache->tolerance) * (hgc_update / hgc_factor));
                }

                iostatus = WriteBufferIO(bufHdr, false);
                if ( iostatus ) {
                    
                    buffer_hits++;
                    cache->commit = true;
                    PageInsertChecksum((Page) MAKE_PTR(bufHdr->data));
                    
                    if (bufHdr->data == 0)
                        elog(FATAL, "[DBWriter]bad buffer block in buffer sync");
                    
                    if ( cache == NULL ) {
                        elog(FATAL, "NULL PathCache");
                    }
                    
                    status = smgrwrite(cache->smgrinfo, bufHdr->tag.blockNum,
                            (char *) MAKE_PTR(bufHdr->data));
                    
                    if ( bufHdr->kind == RELKIND_INDEX && IsDirtyBufferIO(bufHdr) ) {
            /* can't delete the log b/c an index was dirtied after a log  */
                        DTRACE_PROBE2(mtpg, dbwriter__indexdirty,NameStr(cache->relname),bufHdr->tag.blockNum);
                    }
                    
                    if ( status == SM_FAIL ) {
                        ErrorBufferIO(iostatus, bufHdr);
                        elog(FATAL, "BufferSync: cannot write %lu for %s-%s",
                                bufHdr->tag.blockNum, bufHdr->blind.relname, bufHdr->blind.dbname);
                    } else {
                        TerminateBufferIO(iostatus, bufHdr);
                    }
                }
            }
        } else {
            iostatus = WriteBufferIO(bufHdr, true);
            if (iostatus) {
                elog(NOTICE, "already out dbid:%d relid:%d blk:%d\n",
                        list->descriptions[i].relId.dbId,
                        list->descriptions[i].relId.relId,
                        list->descriptions[i].blockNum);
                elog(NOTICE, "now dbid:%d relid:%d blk:%d\n",
                        bufHdr->tag.relId.dbId, bufHdr->tag.relId.relId, bufHdr->tag.blockNum);
                TerminateBufferIO(iostatus, bufHdr);
            }
        }
        
        while(list->release[i] > 0) {
            ManualUnpin(bufHdr, false);
            list->release[i]--;
            releasecount++;
        }
    }
    
    DTRACE_PROBE2(mtpg, dbwriter__syncedbuffers, buffer_hits, releasecount);
    return releasecount;
}

void FlushAllDirtyBuffers() {
    int             i;
    WriteGroup     cart = NULL;
    int             releasecount = 0;
    
    if (IsDBWriter()) {
        
        while ( releasecount == 0 ) {
            int possible = sync_buffers;
            WriterState  cstate;
            cart = GetNextTarget(cart);
            cstate = cart->currstate;
            cart->currstate = FLUSHING;
            UnlockWriteGroup(cart);
            
            MergeWriteGroups(cart, sync_group);
            ResetWriteGroup(sync_group);
            sync_buffers = 0;
            
            if ( logging ) {
/*
                LogBuffers(cart, INDEX_PASS);
                LogBuffers(cart, NONINDEX_PASS);
*/
                LogBuffers(cart,ALL_PASS);
            }
            releasecount = SyncBuffers(cart);
            if ( logging ) {
                ClearLogs(cart);
            }
            /*  clear the buffer information
             * manually, transaction infor needs to be kept.
             */
            /*
             * releasecount = ReleaseBuffers(cart);
             */
            pthread_mutex_lock(&cart->checkpoint);
            memset(cart->buffers, 0, sizeof(bool) * NBuffers);
            memset(cart->logged, 0, sizeof(bool) * NBuffers);
            memset(cart->release, 0, sizeof(int) * NBuffers);
            memset(cart->descriptions, 0, sizeof(BufferTag) * NBuffers);
            cart->currstate = cstate;
            pthread_mutex_unlock(&cart->checkpoint);
            
            DTRACE_PROBE2(mtpg, dbwriter__circularflush, possible, releasecount);
        }
    } else {
        cart = GetCurrentWriteGroup(false);
        
        cart->flush_run = true;
        cart->isTransFriendly = false;
        SignalDBWriter(cart);
        
        while ( cart->flush_run ) {
            if (pthread_cond_wait(&cart->broadcaster, &cart->checkpoint)) {
                UnlockWriteGroup(cart);
                elog(FATAL, "[DBWriter] cannot attach to db write thread");
            }
        }
        
        UnlockWriteGroup(cart);
    }
}

static PathCache* GetPathCache(WriteGroup  cart, char *relname, char *dbname, Oid bufrel, Oid bufdb) {
    PathCache*       target = NULL;
    DBKey           key;
    PathCache      *pc = NULL;
    bool            found;
    
    key.relid = bufrel;
    key.dbid = bufdb;
    
    target = hash_search(db_table, &key, HASH_ENTER, &found);
    
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
    WriteGroup     cart = (last) ? last->next : groups;
    
    pthread_mutex_lock(&cart->checkpoint);
    
    if ( cart->VarRelation == NULL )  {
        cart->VarRelation = RelationNameGetRelation(VariableRelationName, DEFAULTDBOID);
    }
    if ( cart->LogRelation == NULL )  {
        cart->LogRelation = RelationNameGetRelation(LogRelationName, DEFAULTDBOID);
    }
    cart->owner = pthread_self();
    cart->locked = true;    
    return cart;
}

void ShutdownDBWriter(void) {
    bool            alldone = false;
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
        WriteGroup     target = groups->next;
        
        groups->next = NULL;
        while (target != NULL) {
            groups = DestroyWriteGroup(target);
            os_free(target);
            target = groups;
        }
        
        DestroyWriteGroup(sync_group);
        os_free(sync_group);
        sync_group == NULL;
    }
    /* free id memory */
    os_free(writerid);
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
                elog(NOTICE, "[DBWriter]problem waking db writer");
            }
            break;
        case (READY):
            break;
        case (PRIMED):
            break;
        default:
            elog(FATAL, "DBWriter in the wrong state");
            break;
    }
}

WriteGroup GetCurrentWriteGroup(bool forcommit) {
    WriteGroup     cart = NULL;
    
    cart = GetNextTarget(cart);
    while (
            cart != groups || 
            (
                cart->currstate == RUNNING ||
                cart->currstate == LOGGED ||
                cart->currstate == SYNCED ||
                cart->currstate == COMPLETED ||
                cart->currstate == DEAD ||
                cart->currstate == FLUSHING 
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
    
    if (err = pthread_mutex_unlock(&cart->checkpoint)) {
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
    double          turnstyle = NBuffers;
    
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
    PathCache* entry;
    
    tag.relid = relid;
    tag.dbid = dbid;
    
    entry = hash_search(db_table, (char*)&tag, HASH_REMOVE, &found);
    
    if ( !found ) {
        /*
         * elog(NOTICE,"de-referencing unknown freespace %s-%s",RelationGetRelationName(rel),GetDatabaseName());
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
    int             position = 0;
    
    if (cart->currstate == RUNNING) {
        UnlockWriteGroup(cart);
        elog(FATAL, "[DBWriter]commit in running state");
    }
    
    if ( cart->snapshot != NULL ) {
        UnlockWriteGroup(cart);
        elog(ERROR, "[DBWriter] Snapshot already requested");
    }
    
    position = cart->numberOfTrans++;
        
    if ( !IsTransactionFriendly() ) {
        cart->isTransFriendly = false;
    }
    if ( !IsLoggable() ) {
        cart->loggable = false;
    }

    cart->transactions[position] = GetCurrentTransactionId();
    cart->transactionState[position] = XID_COMMIT;
    
    cart->snapshot = strdup(cmd);
        
    SignalDBWriter(cart);

    cart->WaitingThreads[position] = GetMyThread();
    if (pthread_cond_wait(&cart->broadcaster, &cart->checkpoint)) {
        UnlockWriteGroup(cart);
        elog(FATAL, "[DBWriter]cannot attach to db write thread");
    }
    UnlockWriteGroup(cart);
    
    return NULL;
}
