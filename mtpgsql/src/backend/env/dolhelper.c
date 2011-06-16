

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

#include "config.h"
#include "storage/lock.h"
#include "utils/mcxt.h"
#include "parser/parserinfo.h"

#include "storage/smgr.h"
#include "storage/multithread.h"
#include "utils/memutils.h"
#include "utils/relcache.h"
#include "utils/catcache.h"
#include "utils/syscache.h"
#include "storage/sinvaladt.h"
#include "utils/temprel.h"
#include "catalog/heap.h"
#include "utils/tqual.h"

#ifdef SPIN_IS_MUTEX
#include "storage/m_lock.h"
#else
#include "storage/s_lock.h"
#endif

#include "env/dolhelper.h"

static pthread_attr_t dolprops;

static SectionId dol_id = SECTIONID("DOLH");

typedef enum DolTypes {
    DOL_DELEGATE
} DolTypes;

typedef enum DolState {
    DOL_INITIALIZING,
    DOL_WAITING,
    DOL_PRIMED,
    DOL_RUNNING,
    DOL_MAINWAITING,
    DOL_SHUTDOWN
} DolState;

typedef struct dol_connection_data {
    DolTypes     type;
    DolState     state;
    int          id;
    Env*         env;
    Env*         parent;
    pthread_t   thread;
    void*        (*start)(void*);
    void*        args;
    void*        result;
    MemoryContext   dol_cxt;
    pthread_mutex_t   guard;
    pthread_cond_t    gate;
} DolConnectionData;

#define MAX_HELPERS 4

typedef struct DolHelperInfo {
    DolConnection           helpers[MAX_HELPERS];
    int                     count;
} DolHelperInfo;

#ifdef TLS
TLS DolHelperInfo* dol_globals = NULL;
#else
#define dol_globals GetEnv()->dol_globals
#endif

static DolConnection CreateDolConnection(Env* parent);
static void DolCreateThread(DolConnection connect);

static void* InitDolConnection(void* connection);
static void CloneTransactionEvironment(DolConnection conn);
static void StopDolConnection(DolConnection conn);
static long CloseDolConnection(DolConnection conn);

static DolHelperInfo* GetDolHelperInfo(void);
/*
static void CancelJump(void* arg);
*/
PG_EXTERN void InitializeDol(void) {
	struct sched_param sched;
	int             sched_policy;
	
        memset(&dolprops, 0, sizeof(pthread_attr_t));
	memset(&sched, 0, sizeof(struct sched_param));
	/* init thread attributes  */
	pthread_attr_init(&dolprops);
}

PG_EXTERN DolConnection GetDolConnection(void) {
    DolHelperInfo* info = GetDolHelperInfo();
    DolConnection conn = NULL;
    int spin = 0;

    for (spin = 0;spin<MAX_HELPERS;spin++) {
        if ( info->helpers[spin] != NULL ) {
            DolState state;
            pthread_mutex_lock(&info->helpers[spin]->guard);
            state = info->helpers[spin]->state;
            pthread_mutex_unlock(&info->helpers[spin]->guard);
            
            if ( state == DOL_WAITING ) {
                return info->helpers[spin];
            }
        }
    }

     conn = CreateDolConnection(GetEnv());

     if ( conn != NULL ) DolCreateThread(conn);

    return conn;
}


static DolConnection 
CreateDolConnection(Env* parent)
{
	int             sqlError = 0;
	long            opCode;
	char            dbpath[512];
	Oid             dbid = InvalidOid;
        DolHelperInfo*  info = GetDolHelperInfo();
        int             spin = 0;
        
        for (spin=0;spin<MAX_HELPERS;spin++) {
            if ( info->helpers[spin] == NULL ) break;
        } 
        if ( spin == MAX_HELPERS ) {
            return NULL;
        }

	DolConnection          connection = MemoryContextAlloc(parent->global_context,sizeof(DolConnectionData));

	memset(connection, 0x00, sizeof(DolConnectionData));

        connection->parent = parent;
        connection->env = CreateEnv(parent);

	if (connection->env == NULL) {
		sqlError = 99;
		strncpy(connection->env->errortext, "unsuccessful connection -- too many connections", 255);
		strncpy(connection->env->state, "DISCONNECTED", 39);
		return connection;
	}

        connection->state = DOL_INITIALIZING;
	pthread_mutex_init(&connection->guard,NULL);
        pthread_cond_init(&connection->gate,NULL);

        connection->id = spin;
        info->helpers[spin] = connection;

	return connection;
}

PG_EXTERN void*
InitDolConnection(void* connection) {
        DolConnection conn = (DolConnection)connection;
	SetEnv(conn->env);
        
	conn->env->Mode = InitProcessing;

	MemoryContextInit();

	conn->env->DatabaseName = conn->parent->DatabaseName;
	conn->env->DatabasePath = conn->parent->DatabasePath;
	conn->env->DatabaseId = conn->parent->DatabaseId;
	conn->env->UserName = conn->parent->UserName;
	conn->env->UserId = conn->parent->UserId;

	/* from Init Relations cache from RelationInitialize();   */

	InitThread(DOL_THREAD);

        if ( !CallableInitInvalidationState() ) {
            DestroyThread();
            SetEnv(NULL);
            DestroyEnv(conn->env);
            return NULL;
        }
        
	RelationInitialize();
	InitCatalogCache();

        conn->env->Mode = NormalProcessing;

        pthread_mutex_lock(&conn->guard);
        while ( conn->state != DOL_SHUTDOWN ) {
            bool switched = false;
            MemoryContext  dol_cxt;
            long sqlerr = 0;
            Assert(GetEnv() == conn->env);
            
            if ( conn->state == DOL_PRIMED ) {
            
            } else {
                if ( conn->state = DOL_MAINWAITING ) {
                    pthread_cond_signal(&conn->gate);
                }
                conn->state = DOL_WAITING;
                pthread_cond_wait(&conn->gate,&conn->guard);
            }

            if ( conn->start != NULL ) {
                sqlerr = setjmp(conn->env->errorContext);

                if ( sqlerr != 0 ) {
                    MemoryContextSwitchTo(MemoryContextGetTopContext());
                    MemoryContextDelete(dol_cxt);
                    conn->start = NULL;
                    conn->args = NULL;

                    MasterUnLock();
                    TransactionUnlock();

                    ThreadReleaseLocks(false);
                    ThreadReleaseSpins(GetMyThread());
                    
                    clearerror(conn->env);
                } else {
                    conn->state = DOL_RUNNING;


                    pthread_mutex_unlock(&conn->guard);   

                    dol_cxt =  AllocSetContextCreate(MemoryContextGetTopContext(),
                                                                   "DolMemoryContext",
                                                                ALLOCSET_DEFAULT_MINSIZE,
                                                           ALLOCSET_DEFAULT_INITSIZE,
                                                           ALLOCSET_DEFAULT_MAXSIZE);


                    CloneTransactionEvironment(conn);

                    clearerror(conn->env);

                    MemoryContextSwitchTo(dol_cxt);
                    switched = true;

                    conn->result = conn->start(conn->args);

                    MemoryContextSwitchTo(MemoryContextGetTopContext());
                    switched = false;
                    MemoryContextDelete(dol_cxt);

                    conn->start = NULL;
                    conn->args = NULL;
                }
                pthread_mutex_lock(&conn->guard);
            }
        }

        pthread_mutex_unlock(&conn->guard);

        CloseDolConnection(conn);
        
	SetEnv(NULL);
        DestroyEnv(conn->env);
        
        pthread_mutex_lock(&conn->guard);
        conn->env = NULL;
        pthread_mutex_unlock(&conn->guard);
        
        return conn->result;
/*  set the environment back to the parent before returning  */
}

PG_EXTERN void
ProcessDolCommand(DolConnection conn,void*(*start_routine)(void*),void* arg) {
        pthread_mutex_lock(&conn->guard);
        switch ( conn->state ) {
            case DOL_WAITING:
                pthread_cond_signal(&conn->gate);
        /*  fall through   */
            case DOL_INITIALIZING:
                conn->start = start_routine;
                conn->args = arg;
                conn->state = DOL_PRIMED;       
                break;
            default:
                pthread_mutex_unlock(&conn->guard);
                elog(ERROR,"Subordinate thread in the wrong state");
       }
        pthread_mutex_unlock(&conn->guard);

}

static void
StopDolConnection(DolConnection conn) {
        pthread_mutex_lock(&conn->guard);
/*  unblock the thread if it is waiting  */
        if ( conn->state == DOL_WAITING ) {
            pthread_cond_signal(&conn->gate);
        }
        conn->state = DOL_SHUTDOWN;
        conn->start = NULL;
        pthread_mutex_unlock(&conn->guard);
}

PG_EXTERN void 
CloneTransactionEvironment(DolConnection conn) {
    CloneParentTransaction();
}

PG_EXTERN long 
DestroyDolConnection(DolConnection conn)
{
	int             status = 0;
        void*           ret;
        int             spin = conn->id;
        DolHelperInfo*  dol = GetDolHelperInfo();

        StopDolConnection(conn);
        pthread_join(conn->thread, &ret);

        pthread_mutex_destroy(&conn->guard);
        pthread_cond_destroy(&conn->gate);
	
        conn->env = NULL;
        dol->helpers[spin] = NULL;
        
	pfree(conn);
}

static long 
CloseDolConnection(DolConnection conn) {
	int             sqlError = 0;
        bool            quick = false;

        pthread_mutex_lock(&conn->guard);
        quick = (conn->env == NULL);
        pthread_mutex_unlock(&conn->guard);

	if (quick) return sqlError;

	DropNoNameRels();

        MasterUnLock();
        TransactionUnlock();

        remove_all_temp_relations();
        RelationCacheShutdown();

#ifdef  USE_ASSERT_CHECKING  
        if ( BufferPoolCheckLeak() ) {
            ResetBufferPool(false);
        }
#endif
        
        ThreadReleaseLocks(false);
        ThreadReleaseSpins(GetMyThread());
        DestroyThread();
        CallableCleanupInvalidationState();
	
        return sqlError;
}

static void
DolCreateThread(DolConnection connect)
{
	int             prio;

        memset(&connect->thread,0x00,sizeof(pthread_t));
        connect->start = NULL;
        connect->args = NULL;
	if (pthread_create(&connect->thread, &dolprops, InitDolConnection, connect) != 0) {
		elog(FATAL, "could not create db writer\n");
	}
        
}
/*
static void CancelJump(void* arg) {        
        DolConnection conn = arg;

        CloseDolConnection(conn);

	SetEnv(NULL);
        DestroyEnv(conn->env);
        
        pthread_mutex_lock(&conn->guard);
        conn->env = NULL;
        pthread_mutex_unlock(&conn->guard);
}
*/

PG_EXTERN int
CheckDolHelperErrors(char* state,char* msg) {
        DolHelperInfo*  info = GetDolHelperInfo();
        int             spin = 0;
        int err = 0;

        for (spin=0;spin<MAX_HELPERS;spin++) {
            if ( info->helpers[spin] != NULL ) {
                pthread_mutex_lock(&info->helpers[spin]->guard);   
                if ( info->helpers[spin]->state == DOL_WAITING ) {

                } else {
                    if ( info->helpers[spin]->env->InError ) {
                        err = info->helpers[spin]->env->errorcode;
                    }
                }
                pthread_mutex_unlock(&info->helpers[spin]->guard);
                if ( err ) return err;
            }
        } 
        return 0;
}

PG_EXTERN int 
GetDolHelperErrorMessage(char* state,char* msg) {
        DolHelperInfo*  info = GetDolHelperInfo();
        int             spin = 0;
        int err = 0;

        for (spin=0;spin<MAX_HELPERS;spin++) {
            if ( info->helpers[spin] != NULL ) {
                pthread_mutex_lock(&info->helpers[spin]->guard);   
                if ( info->helpers[spin]->state == DOL_WAITING ) {

                } else {
                    if ( info->helpers[spin]->env->InError ) {
                        err = info->helpers[spin]->env->errorcode;
                        strncpy(msg,info->helpers[spin]->env->errortext,255);
                        strncpy(state,info->helpers[spin]->env->state,40);
                    }
                }
                pthread_mutex_unlock(&info->helpers[spin]->guard);
                if ( err ) return err;
            }
        } 
        return 0;
}


PG_EXTERN void
CancelDolHelpers(void) {
        DolHelperInfo*  info = GetDolHelperInfo();
        int             spin = 0;

        for (spin=0;spin<MAX_HELPERS;spin++) {
            if ( info->helpers[spin] != NULL ) {
                pthread_mutex_lock(&info->helpers[spin]->guard);   
                if ( info->helpers[spin]->state == DOL_WAITING ) {

                } else {
                    info->helpers[spin]->env->cancelled = true;
                    info->helpers[spin]->state = DOL_MAINWAITING;
                    pthread_cond_wait(&info->helpers[spin]->gate,&info->helpers[spin]->guard);
                }
                pthread_mutex_unlock(&info->helpers[spin]->guard);
            }
        } 
}

PG_EXTERN void
ShutdownDolHelpers(void) {
        DolHelperInfo*  info = GetDolHelperInfo();
        int             spin = 0;

        for (spin=0;spin<MAX_HELPERS;spin++) {
            if ( info->helpers[spin] != NULL ) {
                DestroyDolConnection(info->helpers[spin]);
            }
        } 

}

PG_EXTERN bool
IsDolConnectionAvailable(void) {
    DolHelperInfo* info = (DolHelperInfo*)GetDolHelperInfo();

    int spin = 0;


    for (spin = 0;spin<MAX_HELPERS;spin++) {
        if ( info->helpers[spin] != NULL ) {
            DolState state;

            pthread_mutex_lock(&info->helpers[spin]->guard);
            state = info->helpers[spin]->state;
            pthread_mutex_unlock(&info->helpers[spin]->guard);
            if ( state == DOL_WAITING ) {
                return true;
            }
        }
    }

    return false;
}



DolHelperInfo*
GetDolHelperInfo(void)
{
    DolHelperInfo* info = dol_globals;
    if ( info == NULL ) {
        int spin = 0;
        info = AllocateEnvSpace(dol_id,sizeof(DolHelperInfo));
        for (spin = 0;spin<MAX_HELPERS;spin++) {
            info->helpers[spin] = NULL;
        }
        dol_globals = info;
    }
    return info;
}

