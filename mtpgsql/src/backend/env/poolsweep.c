/*
 * poolsweep.c
 *
 * Goal is to auto vacuum relations upon signal of the db writer
 */


#include <pthread.h>
#include <sys/types.h>
#include <signal.h>
#include <time.h>
#include <errno.h>


#include "c.h"
#include "postgres.h"
#include "env/env.h"
#include "config.h"
#include "miscadmin.h"
#include "utils/relcache.h"
#include "utils/memutils.h"
#include "utils/inval.h"
#include "utils/portal.h"
#include "env/poolsweep.h"
#include "env/connectionutil.h"
#include "env/freespace.h"
#include "commands/vacuum.h"
#include "storage/smgr.h"
#include "storage/multithread.h"
#include "utils/tqual.h"
#include "utils/syscache.h"
#include "storage/sinvaladt.h"
#include "storage/sinval.h"
#include "catalog/index.h"

#include "access/heapam.h"
#include "access/htup.h"
#include "catalog/catname.h"
#include "catalog/pg_database.h"

#include "access/genam.h"


typedef enum jobtype {
    VACUUM_JOB,
    REINDEX_JOB,
    SCAN_JOB,
    FREESPACE_JOB,
    DEFRAG_JOB,
    ANALYZE_JOB,
    VACUUMDB_JOB,
    TRIM_JOB,
    RESPAN_JOB,
    RELINK_JOB,
    MOVE_JOB,
    COMPACT_JOB,
    ALLOCATE_JOB,
    WAIT_JOB,
    RECOVER_JOB
}               JobType;

typedef struct joblist {
    char            relname[256];
    char            dbname[256];
    Oid             relid;
    Oid             dbid;
    JobType             jobtype;
    bool            activejob;
    void*           arg;
    void           *next;
} JobList;

typedef struct poolargs {
    int       length;
    bool      copy;
    void*     args;
} PoolArgs;

typedef struct fragargs {
        bool        useblobs;
        int         max;
} FragArgs;

typedef struct sweeps {
    Oid             dbid;
    char            dbname[255];
    JobList        *requests;
    pthread_t       thread;
    pthread_cond_t  gate;
    void           *next;
    bool            activesweep;
    int             idle_count;
    Env*            env;
    MemoryContext   context;
} Sweeps;

typedef struct waiter {
    pthread_mutex_t      guard;
    pthread_cond_t       gate;
    bool                done;
} Waiter;


static pthread_mutex_t list_guard;
static pthread_attr_t sweeperprops;
static bool     paused = false;
static bool     inited = false;
static int     concurrent = 1;

static Sweeps  *sweeplist;
static MemoryContext sweep_cxt;

static Sweeps  *StartupPoolsweep(char *dbname, Oid dbid);
static Sweeps  *ShutdownPoolsweep(Sweeps * job);
static int      AddJobRequest(JobType type, char *relname, char *dbname, Oid relid, Oid dbid, PoolArgs* extra);
static int      CheckSweepForJob(Sweeps* sweep, JobType type, Oid relid);
static int      AddJobToSweep(Sweeps* sweep, JobType type, char *relname, char *dbname, Oid relid, Oid dbid, PoolArgs* extra);
static void  poolsweep_log(Oid rel, char* pattern, ...);

static void* Poolsweep(void* arg);

void
PoolsweepInit(int priority) {
    struct sched_param sched;
    int             sched_policy;
    List*           item;
    memset(&sweeperprops, 0, sizeof(pthread_attr_t));
    memset(&sched, 0, sizeof(struct sched_param));
    /* init thread attributes  */
    pthread_attr_init(&sweeperprops);
    
    char* cc = GetProperty("sweeps");
    
    if ( cc != NULL ) {
        concurrent = atoi(cc);
    }
#ifndef MACOSX
/*
 * pthread_attr_setscope(&sweeperprops,PTHREAD_SCOPE_SYSTEM);
 *
 * pthread_attr_setstacksize(&sweeperprops,(0x200000));
 * pthread_attr_setguardsize(&sweeperprops,(0x4000));
 *
 * pthread_attr_getschedpolicy(&sweeperprops,&sched_policy);
 * sched.sched_priority = sched_get_priority_min(sched_policy);
 *
 * pthread_attr_setschedparam(&sweeperprops,&sched);
 */
#endif
    pthread_mutex_init(&list_guard, NULL);

    sweep_cxt = AllocSetContextCreate((MemoryContext) NULL,
            "SweepMemoryContext",
            ALLOCSET_DEFAULT_MINSIZE,
            ALLOCSET_DEFAULT_INITSIZE,
            ALLOCSET_DEFAULT_MAXSIZE);
    
    inited = true;

}

void
PoolsweepDestroy() {
    Sweeps         *next = NULL;
    MemoryContext   cxt = sweep_cxt;
        
    if (!inited)
        return;
    pthread_mutex_lock(&list_guard);
    sweep_cxt = NULL;
    next = sweeplist;
    while (next != NULL) {
        next = ShutdownPoolsweep(next);
    }
    MemoryContextDelete(cxt);
    pthread_mutex_unlock(&list_guard);
}

Sweeps         *
StartupPoolsweep(char *dbname, Oid dbid) {
    Sweeps         *inst = NULL;
    Sweeps         *next = NULL;
    char            name[256];
    /*  already holding listguard  */
    snprintf(name,256,"SweepInstanceCxt -- dbid: %d",dbid);
    
    if ( sweep_cxt == NULL ) {
        elog(ERROR, "Sweep is shutting down");
    } 
    
    inst = (Sweeps *) MemoryContextAlloc(sweep_cxt, sizeof(Sweeps));
    inst->dbid = dbid;
    strncpy(inst->dbname, dbname, 255);
    inst->next = NULL;
    inst->requests = NULL;
    inst->activesweep = true;
    pthread_cond_init(&inst->gate, NULL);
    
    inst->context = AllocSetContextCreate(sweep_cxt,
            name,
            512,
            512,
            1024 * 1024);
    
    if (pthread_create(&inst->thread, &sweeperprops, Poolsweep, inst) != 0) {
        elog(FATAL, "could not create pool sweep thread\n");
    }
    if (sweeplist == NULL) {
        sweeplist = inst;
        return inst;
    } else {
        next = sweeplist;
        while (next->next != NULL) {
            next = next->next;
        }
        next->next = inst;
    }
    return inst;
}

Sweeps*
ShutdownPoolsweep(Sweeps * job) {
    void           *ret = NULL;
    Sweeps*         next = job->next;
    
    job->activesweep = false;
    pthread_cond_broadcast(&job->gate);
    pthread_mutex_unlock(&list_guard);
    
    pthread_join(job->thread, &ret);
    
    pthread_mutex_lock(&list_guard);
    
    next = job->next;
    MemoryContextDelete(job->context);
    pfree(job);
    
    return next;
}

void           *
Poolsweep(void *args) {
    
    Sweeps         *tool = (Sweeps *) args;
    int             timerr;
    char            dbuser[255];
    int             i = 0;
    bool            leak = false;
    bool            activated = true;
    bool            invalidate = false;
    Env*            env;
    
    env = CreateEnv(NULL);

    tool->env = env;
    
    SetEnv(env);
    env->Mode = InitProcessing;
    
    MemoryContextInit();
    
    SetDatabaseName(tool->dbname);
    env->DatabaseId = tool->dbid;
    
    InitThread(POOLSWEEP_THREAD);
    
    RelationInitialize();

    InitCatalogCache();
    
    CallableInitInvalidationState();
    
    env->Mode = NormalProcessing;
    
    while (activated && !IsShutdownProcessingMode()) {
        JobList        *item = NULL;
        int             err = 0;
        
        err = setjmp(env->errorContext);
        if (err) {
            pthread_mutex_lock(&list_guard);
            tool->requests = item->next;
            pthread_mutex_unlock(&list_guard);
            pfree(item);
            item = NULL;
            if (CurrentXactInProgress()) {
                AbortTransaction();
            }
        } else {
            pthread_mutex_lock(&list_guard);
            
            if ( !tool->activesweep ) {
                pthread_mutex_unlock(&list_guard);
                activated = false;
                continue;
            }
            
            MemoryContextSwitchTo(MemoryContextGetEnv()->QueryContext);
            
            if (tool->requests == NULL) {
                struct timespec tv;
                int             result = 0;
                tv.tv_sec = time(NULL) + 60;
                tv.tv_nsec = 0;
                /*
                 * no jobs on list so now is a good time to
                 * update all caches on other backends
                 */
                /*
                 * if ( invalidate ) { InvalidateAllCaches();
                 * invalidate = false; }
                 */
                result = pthread_cond_timedwait(&tool->gate, &list_guard, &tv);
                if (result == ETIMEDOUT) {
                    if ( tool->idle_count++== 5 ) {
                        tool->activesweep = false;
                        activated = false;
                    } else {
                        DiscardInvalid();
                    }
                } else {
                    tool->idle_count = 0;
                }
            } else {
        /* current request has already been run, go to the next one */
                if ( tool->requests->activejob ) 
                    tool->requests = tool->requests->next;
            }
            
            if (paused || tool->requests == NULL) {
                pthread_mutex_unlock(&list_guard);
                continue;
            } else {
                item = tool->requests;
                item->activejob = true;
                invalidate = true;
                pthread_mutex_unlock(&list_guard);
            }
            
            SetTransactionCommitType(TRANSACTION_CAREFUL_COMMIT);
            
            StartTransaction();
            
            SetQuerySnapshot();
            
            if (item->jobtype == VACUUM_JOB) {
                poolsweep_log(item->relid, "starting vacuum job");
                lazy_open_vacuum_rel(item->relid, false, false);
            } else if (item->jobtype == REINDEX_JOB) {
                poolsweep_log(item->relid, "starting reindex job");
                reindex_index(item->relid, true);
            } else if (item->jobtype == SCAN_JOB) {
                poolsweep_log(item->relid, "starting scan job");
                lazy_open_vacuum_rel(item->relid, false, true);
            } else if (item->jobtype == FREESPACE_JOB) {
                poolsweep_log(item->relid, "starting freespace scan job");
                lazy_freespace_scan_rel(item->relid);                
            } else if (item->jobtype == DEFRAG_JOB) {
                FragArgs* args = item->arg;
                poolsweep_log(item->relid, "starting defrag job");
                lazy_fragmentation_scan_rel(item->relid, false,(args->useblobs) ? BLOB_MOVE : NORMAL, args->max);
                pfree(args);
            } else if (item->jobtype == ANALYZE_JOB) {
                poolsweep_log(item->relid, "starting analyze job");
                analyze_rel(item->relid);
            } else if (item->jobtype == TRIM_JOB) {
                poolsweep_log(item->relid, "starting trim job");
                lazy_open_vacuum_rel(item->relid, true, false);
            } else if (item->jobtype == RESPAN_JOB) {
                poolsweep_log(item->relid, "starting respan job");
                lazy_respan_blobs_rel(item->relid, true, false); /* don't exclude self moves */
            } else if (item->jobtype == RELINK_JOB) {
/*
                poolsweep_log(item->relid, "starting relink job");
                lazy_fragmentation_scan_rel(item->relid, true, RELINKING, 1024 * 1024);
*/
            } else if (item->jobtype == MOVE_JOB) {
                poolsweep_log(item->relid, "starting move job");
                lazy_respan_blobs_rel(item->relid, true, true);/* exclude self moves */
            } else if (item->jobtype == VACUUMDB_JOB) {
                poolsweep_log(item->relid, "starting vacuumdb job");
                lazy_vacuum_database(false);
            } else if (item->jobtype == COMPACT_JOB) {
                FragArgs* args = item->arg;
                poolsweep_log(item->relid, "starting compact job");
                lazy_fragmentation_scan_rel(item->relid, true,(args->useblobs) ? BLOB_MOVE : NORMAL, args->max);
                pfree(args);
             } else if ( item->jobtype == ALLOCATE_JOB ) {
                Relation rel = RelationIdGetRelation(item->relid, DEFAULTDBOID);
                poolsweep_log(item->relid, "starting space allocation job");
/*
                AllocateMoreSpace(rel);
*/
                RelationClose(rel);
            } else if ( item->jobtype == WAIT_JOB ) {
                poolsweep_log(item->relid, "starting wait notification");
                Waiter*     w = item->arg;
                pthread_mutex_lock(&w->guard);
                w->done = true;
                pthread_cond_broadcast(&w->gate);
                pthread_mutex_unlock(&w->guard);
            } else  if ( item->jobtype == RECOVER_JOB ) {
                List* pages = smgrgetrecoveredlist(GetDatabaseId());
                index_recoverpages(pages);
            } else {
                poolsweep_log(item->relid, "unknown job type %d", item->jobtype);
            }
            
            MemoryContextResetAndDeleteChildren(MemoryContextGetEnv()->QueryContext);
            CommitTransaction();
            if (item != NULL) {
                pfree(item);
                item = NULL;
            }
        }
    }
    
    /* all done cleaning, we should have no valid threads or write groups  */
#ifdef  USE_ASSERT_CHECKING  
        if ( BufferPoolCheckLeak() ) {
            elog(NOTICE,"Buffer leak in poolsweep");
            ResetBufferPool(false);
        }
#endif
    
    CallableCleanupInvalidationState();
    
    RelationCacheShutdown();
    
    ThreadReleaseLocks(false);
    ThreadReleaseSpins(GetMyThread());
    DestroyThread();
    
     SetEnv(NULL);
     DestroyEnv(env);
    
    return env;
}

static int
CheckSweepForJob(Sweeps* sweep, JobType type, Oid relid) {
    JobList         *search;
    int pos = 1;
    bool relid_is_valid = false;
    search = sweep->requests;

    /* make sure the request isn't already in queue  */
    while (search != NULL) {
        pos++;
        if ( relid == search->relid) {
            if ( type == search->jobtype ) return -1;
            relid_is_valid = true;
        }
        search = search->next;
    }
    if ( relid_is_valid && type != FREESPACE_JOB ) return 0;
  /*  this relid is not hosted on this list  */
    return pos;
}

static int
AddJobToSweep(Sweeps* sweep, JobType type, char *relname, char *dbname, Oid relid, Oid dbid, PoolArgs* extra) {
    JobList* item = (JobList *) MemoryContextAlloc(sweep->context, sizeof(JobList));
    
    strncpy(item->relname, relname, 255);
    item->relid = relid;
    item->dbid = dbid;
    strncpy(item->dbname, dbname, 255);
    item->activejob = false;
    item->jobtype = type;
    item->arg = NULL;
    if ( extra != NULL ) {
        if ( extra->copy ) {
            item->arg = MemoryContextAlloc(sweep->context, extra->length);
            memmove(item->arg,extra->args,extra->length);
        } else {
            item->arg = extra->args;
        }
    } 
    
    item->next = NULL;
    
    if (sweep->requests == NULL) {
        sweep->requests = item;
        if (!paused) pthread_cond_signal(&sweep->gate);
        return 0;
    } else {
        JobList        *save, *search;
        
        save = search = sweep->requests;
        
        int pos = 0;
        while (search != NULL) {
            /* put reindex jobs before anything else  */
            if (item->jobtype == REINDEX_JOB && search->jobtype != REINDEX_JOB) {
                poolsweep_log(item->relid, "doing head swap request for reindex %s", relname);
                break;
            }
            pos++;
            save = search;
            search = search->next;
        }
        if (item != NULL) {
            item->next = save->next;
            save->next = item;
        }
        return pos;
    }
}

static int
AddJobRequest(JobType type, char *relname, char *dbname, Oid relid, Oid dbid, PoolArgs* extra) {
    Sweeps         *sweep = NULL;
    int             sweepcount = 0;
    Sweeps         *add = NULL;
    Sweeps	   *save = NULL;
    int             depth = 0;
    
    if (!inited)
        return -1;
    if (dbid == 0) {
        dbid = GetDatabaseId();
        dbname = GetDatabaseName();
    }
    
    if ( IsShutdownProcessingMode() ) return -1;

    pthread_mutex_lock(&list_guard);
    sweep = sweeplist;

    /* first member is now active or null, proceed with the
    rest of the list */
    while (sweep != NULL) {        
	if ( IsShutdownProcessingMode() ) {
            pthread_mutex_unlock(&list_guard);
            return -1;
        }
        
        if ( !sweep->activesweep ) {
            sweep = ShutdownPoolsweep(sweep);
            if ( save == NULL ) sweeplist = sweep;
	    else save->next = sweep;
   /* have a new sweep, skip getting a new one below  */
            continue;
        } 
        
        if ( sweep->dbid == dbid ) {
            int space = CheckSweepForJob(sweep, type, relid);
        
            sweepcount++;

            if ( space < 0 ) {
                pthread_mutex_unlock(&list_guard);
                return -1; /*  already in a list, just return  */
            } else if ( space == 0 ) {
                add = sweep; 
		break;
            } else if ( add == NULL ) {
                depth = space;
                add = sweep;
            } else if( space < depth ) {
                depth = space;
                add = sweep;
            }
        }
        
        save = sweep;
        sweep = sweep->next;
    }
    
    if ( sweep == NULL && sweepcount < concurrent ) {
/*  this means that the relid was not found in the sweeps and 
 *  there is room for another concurrent thread
 */
        add = StartupPoolsweep(dbname, dbid);
    } else if ( add == NULL ) {
        Assert(sweepcount == 0);
        add = StartupPoolsweep(dbname, dbid);
    }
    
    sweepcount = AddJobToSweep(add, type, relname, dbname, relid, dbid, extra);
    
    pthread_mutex_unlock(&list_guard);
    
    return ( sweepcount >= 0 ) ? 0 : 1;
}

void
AddAnalyzeRequest(char *relname, char *dbname, Oid relid, Oid dbid) {
    int             result = AddJobRequest(ANALYZE_JOB, relname, dbname, relid, dbid, NULL);
    if (result == 0)
        poolsweep_log(relid, "added heap analyze request %s-%s rel:%d db:%d", relname, dbname, relid, dbid);
}

void
AddScanRequest(char *relname, char *dbname, Oid relid, Oid dbid) {
    int             result = AddJobRequest(SCAN_JOB, relname, dbname, relid, dbid, NULL);
    if (result == 0)
        poolsweep_log(relid, "added heap scan request %s-%s rel:%d db:%d", relname, dbname, relid, dbid);
}

void
AddReindexRequest(char *relname, char *dbname, Oid relid, Oid dbid) {
    int             result = AddJobRequest(REINDEX_JOB, relname, dbname, relid, dbid, NULL);
    if (result == 0)
        poolsweep_log(relid, "added reindex request %s-%s rel:%d db:%d", relname, dbname, relid, dbid);
}

void
AddVacuumRequest(char *relname, char *dbname, Oid relid, Oid dbid) {
    
    int             result = AddJobRequest(VACUUM_JOB, relname, dbname, relid, dbid, NULL);
    if (result == 0)
        poolsweep_log(relid, "added vacuum request %s-%s rel:%d db:%d", relname, dbname, relid, dbid);
}

void
AddDefragRequest(char *relname, char *dbname, Oid relid, Oid dbid,bool useblobs, int max) {
    int             result = 0;
    PoolArgs        args;
    FragArgs        frag;
    
    args.length = sizeof(FragArgs);
    args.copy = true;
    args.args = &frag;
    
    frag.useblobs = useblobs;
    frag.max = max;

    result = AddJobRequest(DEFRAG_JOB, relname, dbname, relid, dbid, &args);
    if (result == 0)
        poolsweep_log(relid, "added defrag request %s-%s rel:%d db:%d", relname, dbname, relid, dbid);
}

void
AddCompactRequest(char *relname, char *dbname, Oid relid, Oid dbid,bool useblobs, int max) {
    int             result = 0;
    PoolArgs        args;
    FragArgs        frag;
    
    args.length = sizeof(FragArgs);
    args.copy = true;
    args.args = &frag;
    
    frag.useblobs = useblobs;
    frag.max = max;

    result = AddJobRequest(COMPACT_JOB, relname, dbname, relid, dbid, &args);
    if (result == 0)
        poolsweep_log(relid, "added compact request %s-%s rel:%d db:%d", relname, dbname, relid, dbid);
    else 
        poolsweep_log(relid, "error adding compact request %s-%s rel:%d db:%d", relname, dbname, relid, dbid);
}

void
AddTrimRequest(char *relname, char *dbname, Oid relid, Oid dbid) {
    int             result = AddJobRequest(TRIM_JOB, relname, dbname, relid, dbid, NULL);
    if (result == 0)
        poolsweep_log(relid, "added trim request %s-%s rel:%d db:%d", relname, dbname, relid, dbid);
}

void
AddRespanRequest(char *relname, char *dbname, Oid relid, Oid dbid) {
    int             result = AddJobRequest(RESPAN_JOB, relname, dbname, relid, dbid, NULL);
    if (result == 0)
        poolsweep_log(relid, "added respan request %s-%s rel:%d db:%d", relname, dbname, relid, dbid);
}

void
AddRelinkBlobsRequest(char *relname, char *dbname, Oid relid, Oid dbid) {
    int             result = AddJobRequest(RELINK_JOB, relname, dbname, relid, dbid, NULL);
    if (result == 0)
        poolsweep_log(relid, "added relink request %s-%s rel:%d db:%d", relname, dbname, relid, dbid);
}

void
AddMoveRequest(char *relname, char *dbname, Oid relid, Oid dbid) {
    int             result = AddJobRequest(MOVE_JOB, relname, dbname, relid, dbid, NULL);
    if (result == 0)
        poolsweep_log(relid, "added move request %s-%s rel:%d db:%d", relname, dbname, relid, dbid);
}

void
AddVacuumDatabaseRequest(char *relname, char *dbname, Oid relid, Oid dbid) {
    int             result = AddJobRequest(VACUUMDB_JOB, relname, dbname, relid, dbid, NULL);
    if (result == 0)
        poolsweep_log(relid, "added database vacuum request %s db:%d", dbname, dbid);
}


void
AddAllocateSpaceRequest(char *relname, char *dbname, Oid relid, Oid dbid) {
    int             result = AddJobRequest(ALLOCATE_JOB, relname, dbname, relid, dbid, NULL);
    if (result == 0)
        poolsweep_log(relid, "added allocate space request %s-%s rel:%d db:%d", relname, dbname, relid, dbid);
}

void
AddRecoverRequest(char* dbname, Oid dbid) {
    int result = AddJobRequest(RECOVER_JOB, "", dbname, 0, dbid, NULL);
    if (result == 0)
        poolsweep_log(0, "added recover request db:%d", dbid);
}


void
AddFreespaceScanRequest(char *relname, char *dbname, Oid relid, Oid dbid) {
    int result = AddJobRequest(FREESPACE_JOB, relname, dbname, relid, dbid, NULL);
    if (result == 0)
        poolsweep_log(relid, "added freespace scan request %s-%s rel:%d db:%d", relname, dbname, relid, dbid);
}

void
AddWaitRequest(char* dbname, Oid dbid) {
    PoolArgs args;
    Waiter w;
    int result = 0;
    
    
    pthread_cond_init(&w.gate, NULL);
    pthread_mutex_init(&w.guard, NULL);
    w.done = false;
    
    args.length = sizeof(w);
    args.copy = false;
    args.args = &w;
    
    result = AddJobRequest(WAIT_JOB, "", dbname, 0, dbid, &args);
    
    pthread_mutex_lock(&w.guard);
    while ( !w.done ) {
        pthread_cond_wait(&w.gate, &w.guard);
    }
    pthread_mutex_unlock(&w.guard);
    pthread_cond_destroy(&w.gate);
    pthread_mutex_destroy(&w.guard);
}

bool
IsPoolsweep() {
    THREAD*         thread = GetMyThread();
    if (thread != NULL) {
        return (thread->ttype == POOLSWEEP_THREAD) ? true : false;
    } else {
        Sweeps         *job = NULL;
        bool            valid = FALSE;
        pthread_mutex_lock(&list_guard);
        job = sweeplist;
        while (job != NULL) {
            if (pthread_equal(job->thread, pthread_self())) {
                valid = TRUE;
                break;
            }
            job = job->next;
        }
        pthread_mutex_unlock(&list_guard);
        return valid;
    }
    
}

void
DropVacuumRequests(Oid relid, Oid dbid) {
    Sweeps         *job = NULL;
    bool            valid = false;
    
    pthread_mutex_lock(&list_guard);
    
    job = sweeplist;
    while (job != NULL ) {
        if ( job->dbid == dbid && job->activesweep ) {
            JobList* search = job->requests;
            JobList* tail = NULL;
            JobList* head = NULL;
            
            while (search != NULL) {
                JobList* target = search;
                search = search->next;
                if (relid == target->relid || relid == InvalidOid) {
                    if (target->activejob) {
                        target->next = head;
                        head = target;
                        job->env->cancelled = true;
                    } else {
                        pfree(target);
                    }
                } else {
                    if ( head == NULL ) {
                        head = target;
                    } else if ( tail == NULL ) {
                        head->next = target;
                        tail = target;
                    } else {
                        tail->next = target;
                        tail = target;
                    }
                }
            }
            job->requests = head;
        }
        job = job->next;
    }

    pthread_mutex_unlock(&list_guard);
    return;
}


void
PausePoolsweep() {
    paused = true;
}

bool
IsPoolsweepPaused() {
    return paused;
}

void
ResumePoolsweep() {
    paused = false;
}

void PrintPoolsweepMemory( ) 
{
	pthread_mutex_lock(&list_guard);

        size_t total = MemoryContextStats(sweep_cxt);
        user_log("Total sweep memory: %d",total);
                
	pthread_mutex_unlock(&list_guard);

}

void  poolsweep_log(Oid rel, char* pattern, ...) {
    char            msg[256];
    va_list         args;

    va_start(args, pattern);
    vsprintf(msg,pattern,args);
#ifdef SUNOS
    DTRACE_PROBE3(mtpg,poolsweep__msg,msg,rel,GetDatabaseId());  
#endif
#ifdef DEBUGLOGS
    elog(DEBUG,"poolsweep: %d/%d %s",rel,GetDatabaseId(),msg);
#endif
    va_end(args);
}

