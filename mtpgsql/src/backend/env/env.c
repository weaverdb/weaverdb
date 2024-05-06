
#include <pthread.h>
#include <unistd.h>

#ifdef MACOSX
#include <semaphore.h>
#endif
#include <string.h>
#include <signal.h>
#ifdef SUNOS
#include <umem.h>
#endif
#ifdef _GNU_SOURCE
#include <mcheck.h>
#endif

#include "postgres.h"
#include "env/env.h"
#include "env/connectionutil.h"
#include "miscadmin.h"
#include "utils/catcache.h"
#include "access/xact.h"
#include "optimizer/cost.h"
#include "utils/relcache.h"   /* defines DEFAULTDBOID */
#include "catalog/catname.h"	/* defines LogRelationName */
#include "utils/memutils.h"
#include "utils/inval.h"
#include "storage/multithread.h"



#undef UserId
#undef UserName

#define INITENVCACHESIZE  30

static int                      envcount;
static pthread_key_t		envkey;
static CommitType               default_type = SOFT_COMMIT;

static Env*                     *envmap;


pthread_condattr_t		process_cond_attr;
pthread_mutexattr_t             process_mutex_attr;
masterlock_t*			masterlock = NULL;

pthread_mutex_t                 envlock;

bool 				multiuser = false;

static ProcessingMode		CurrentMode = InitProcessing;
#ifdef USE_ASSERT_CHECKING

int            assert_enabled = 1;

#endif

#ifdef HAVE_SIGPROCMASK
sigset_t    UnBlockSig,
            BlockSig;

#else
int            UnBlockSig,
            BlockSig;

#endif

#define WRITELOCK_MASK	0x04
#define READLOCK_MASK	0x02
#define TRANSACTIONLOCK_MASK	0x01


typedef struct global_env_entry {
    SectionId	env_id;
    void*	global_pointer;
    int		global_size;
} EnvEntry;

static void* EnvAlloc(Size size,void* cxt);
static void EnvFree(void* pointer,void* cxt);
static HTAB* CreateHash(MemoryContext context);
#ifdef UNUSED
static int DestroyHash(HTAB* hash);
#endif
static long sectionid_hash(void* key, int size);
static int memory_fail(void);
#ifdef _GNU_SOURCE
static void glibc_memory_fail(enum mcheck_status err);
#endif
static void  env_log(Env* env, char* pattern, ...);

#ifdef ENV_TLS
static __thread Env* env_cache = NULL;
#endif


Env* InitSystem(bool  isPrivate) {
	int counter = 0;

        MyProcPid = getpid();
#ifdef SUNOS
        umem_nofail_callback(memory_fail);
#elif defined _GNU_SOURCE
        mcheck(glibc_memory_fail);
#endif
        
	pthread_mutex_init(&envlock,NULL);
	
        envmap = os_malloc(sizeof(Env*) * GetMaxBackends());
	
	envcount = 0;
    	for (counter=0;counter<GetMaxBackends();counter++) {
     	   envmap[counter] = NULL;
    	}
          
        pthread_mutexattr_init(&process_mutex_attr);
        pthread_condattr_init(&process_cond_attr);
        if ( isPrivate ) {
        	pthread_condattr_setpshared(&process_cond_attr,PTHREAD_PROCESS_PRIVATE);
        	pthread_mutexattr_setpshared(&process_mutex_attr,PTHREAD_PROCESS_PRIVATE);
        } else {
        	pthread_condattr_setpshared(&process_cond_attr,PTHREAD_PROCESS_SHARED);
        	pthread_mutexattr_setpshared(&process_mutex_attr,PTHREAD_PROCESS_SHARED);
        }
         
        counter = pthread_key_create(&envkey,NULL);
                
        SetEnv(CreateEnv(NULL));
	InitVirtualFileSystem();  
        
        return GetEnv();
}

int DestroySystem(void) {
/*  here we make sure we clean every env we make 
	MKS 12.27.2000  
*/
    if ( envcount != 0 ) {
        printf("all system environments not shutdown\n");
        exit(-1);
    }
	
	pthread_mutex_destroy(&envlock);

        pthread_condattr_destroy(&process_cond_attr);
        pthread_mutexattr_destroy(&process_mutex_attr);

	return 0;
}


extern Env* GetEnv(void) {
#ifdef ENV_TLS
        return env_cache;
#else
	EnvPointer* 		envp;
	EnvPointer 		env;
        
        envp = (EnvPointer*)pthread_getspecific(envkey);
	if ( envp == NULL ) return NULL;
	env = *envp;

	return env;
#endif

}

extern bool SetEnv(void* envp)
{
#ifdef ENV_TLS
        if ( envp != NULL ) {
            Assert(env_cache == NULL || env_cache->parent == env || env->parent == env_cache);
            env_cache = envp;
        } else {
            Assert(env_cache != NULL);
            env_cache = envp;
        }
        return TRUE;
#else
	EnvPointer env = envp;
        EnvPointer current = GetEnv();
        if ( env != NULL ) {
            Assert(current == NULL || current == env);
            pthread_mutex_lock(env->env_guard);
            if ( env->owner != 0 && env->owner != pthread_self() ) {
                pthread_mutex_unlock(env->env_guard);
                return FALSE;
/*
                printf("Environment already owned, make sure the connection is owned by a single thread\n");
                abort();
*/
            } else {
                pthread_setspecific(envkey,&envmap[env->eid]);
                env->owner = pthread_self();
                if ( env->print_memory ) {
                    env->print_memory = FALSE;
                    size_t amt = MemoryContextStats(env->global_context);
                    env_log(env,"Total env memory: %ld",amt);     
                }
                pthread_mutex_unlock(env->env_guard);
            }
        } else {
            if ( current == NULL ) return FALSE;
           if ( current->parent != NULL ) {
/* sub-connections cannot jump threads b/c we need to be able to join */
                
            } else {
                pthread_mutex_lock(current->env_guard);
                if ( current->print_memory ) {
                    current->print_memory = FALSE;
                    size_t amt = MemoryContextStats(current->global_context);
                    env_log(current,"Total env memory: %ld",amt);     
                }
                current->owner = 0;
                pthread_setspecific(envkey,NULL);
                pthread_mutex_unlock(current->env_guard);
            }
        }
        return TRUE;
#endif
}

pthread_t FindChildThread(Env* env) {
    pthread_t   child = 0;
    int counter =0;
    pthread_mutex_lock(&envlock);
    for (counter=0;counter<GetMaxBackends();counter++) {
        if ( envmap[counter] == env ) {
            child = env->owner;
            break;
        }
    }
    pthread_mutex_unlock(&envlock);
    return child;
}

void
CancelEnvAndJoin(Env* env) {
    pthread_t  id = 0;
    void*      result;
    pthread_mutex_lock(env->env_guard);
    if ( env->owner != 0 ) {
        if ( env->in_transaction ) {
            env->cancelled = true;
        }
        id = env->owner;
    }
    pthread_mutex_unlock(env->env_guard);  
    if ( id != 0 ) pthread_join(id,&result);
}

Env* CreateEnv(Env* parent) {
	MemoryContext top = ( parent == NULL ) ? NULL : parent->global_context;

	Env* env = ( top == NULL ) ? os_malloc(sizeof(Env)) : MemoryContextAlloc(top,sizeof(Env));
        memset(env,0,sizeof(Env));

        env->parent = parent;

/* be safe and use the mutex here, borrowing from masterlock  */	
/*  set snapshot data  */
	env->PortalHashTable = NULL;
/* initialize for rewrite  */
	env->LastOidProcessed = InvalidOid;
/*    lock.c  */
	env->holdLock = 0;
    	
	env->UserId = 0;
	
	env->UserName = NULL;
	env->UserId = InvalidOid;
		
	env->DatabaseId = InvalidOid;
	
	env->system_type = DEFAULT_COMMIT;
	env->user_type = DEFAULT_COMMIT;
	
	env->cartposition = -1;
        
        env->global_context = AllocSetContextCreate(top,
					"TopMemoryContext",
                                         8 * 1024,
                                         8 * 1024,
                                         8 * 1024);
                                                                                         
    env->env_guard = MemoryContextAlloc(env->global_context,sizeof(pthread_mutex_t));
    pthread_mutex_init(env->env_guard,NULL);
    
    env->global_hash = CreateHash(env->global_context);
/*  create global hashtable and migrate to this model  */

/*  insert the env into envmap  */
	{
            int counter = 0;
            pthread_mutex_lock(&envlock);
            for (counter=0;counter<GetMaxBackends();counter++) {
                if ( envmap[counter] == NULL ) break;
            }
		
            if ( counter != GetMaxBackends() ) {
		envmap[counter] = env;
		env->eid = counter;
		envcount++;
	    } else {
		printf("too many connections\n");
                pthread_mutex_destroy(env->env_guard);
                MemoryContextDelete(env->global_context);    
                if ( top ) {
                    pfree(env);
                } else {
                    os_free(env);
                }
		env = NULL;
            }
            pthread_mutex_unlock(&envlock);
	}
	return env;
}

void DiscardAllInvalids()
{
    int counter = 0;
    Env* home = GetEnv();
    SetEnv(NULL);
    elog(DEBUG,"discarding invalids for all backends, message queue close to capacity");
    pthread_mutex_lock(&envlock);
    for (counter = 0;counter <GetMaxBackends();counter++) {
        if ( envmap[counter] != NULL ) {
            pthread_mutex_lock(envmap[counter]->env_guard);
            if ( !envmap[counter]->in_transaction ) {
            /*  trick GetEnv() to use the correct env for printing out logging messages */
                pthread_setspecific(envkey,&envmap[counter]);
                DiscardInvalid();
                pthread_setspecific(envkey,NULL);
           }
            pthread_mutex_unlock(envmap[counter]->env_guard);
        }
    }
    pthread_mutex_unlock(&envlock);
    SetEnv(home);
}


void DestroyEnv(void* p) {
    Env* env = (Env*)p;
       
    pthread_mutex_lock(&envlock);
    envmap[env->eid] = NULL;
    envcount--;
    pthread_mutex_unlock(&envlock);
    
    pthread_mutex_destroy(env->env_guard);
    MemoryContextDelete(env->global_context);    

    if ( env->parent == NULL ) {
        os_free(env);
    } else {
        pfree(env);
    }
}

static void* EnvAlloc(Size size,void* cxt)
{
    return MemoryContextAlloc(cxt,size);
/*    return os_malloc(size);    */
}

static void EnvFree(void* pointer,void* cxt)
{
    pfree(pointer);
/*    free(pointer);   */
}

HTAB* CreateHash(MemoryContext context)
{
    HASHCTL		ctl;

    MemSet(&ctl, 0, (int) sizeof(ctl));
    ctl.keysize = SectionIdSize;
    ctl.entrysize = sizeof(EnvEntry);
    ctl.alloc = EnvAlloc;
    ctl.free = EnvFree;
    ctl.hash = sectionid_hash;
    ctl.hcxt = context;
    return hash_create("environment hash",INITENVCACHESIZE, &ctl, HASH_ELEM | HASH_ALLOC | HASH_FUNCTION | HASH_CONTEXT);
}
#ifdef UNUSED
int DestroyHash(HTAB* hash) 
{
    HASH_SEQ_STATUS   seq;
    EnvEntry*          entry;

/*  manually free the hashtable b/c hash_destroy uses contexts
*    and doesn't know how to free this hashtable MKS 2.18.2002 
*/    
    hash_seq_init(&seq,hash);
    while ( (entry = (EnvEntry*)hash_seq_search(&seq)) != NULL ) {
        if ( entry->global_size >0 ) {
            pfree(entry->global_pointer);
        }
    }

    hash_destroy(hash);

    return 0;
}
#endif


int MasterWriteLock()
{
	Env* env = GetEnv();
	pthread_mutex_lock(&masterlock->guard);
	if ( env->masterlock & WRITELOCK_MASK ) {
		pthread_mutex_unlock(&masterlock->guard);
		return env->masterlock;
	}
	if ( env->masterlock & READLOCK_MASK ) {
		masterlock->readcount--;
		env->masterlock &= ~READLOCK_MASK;
	}
	if ( env->masterlock & TRANSACTIONLOCK_MASK ) {
		masterlock->transcount--;
	}
	while ( masterlock->readcount > 0 || 
		masterlock->transcount > 0 ||
		masterlock->writelock
	) {
/*  if we are blocked on a writer then signal will come 
	with a broadcast , else we need to mark as waiting
	for a read or transaction lock so that when both
	equal zero we get a signal MKS 11.28.2001  */
		masterlock->waitcount++;
		masterlock->blocked = true;
		pthread_cond_wait(&masterlock->gate,&masterlock->guard);
		masterlock->blocked = false;
		masterlock->waitcount--;

	}
	if ( env->masterlock & TRANSACTIONLOCK_MASK ) {
		masterlock->transcount++;
	}
	masterlock->writelock = true;
        masterlock->owner = pthread_self();
	pthread_mutex_unlock(&masterlock->guard);

	env->masterlock |= WRITELOCK_MASK;
	return env->masterlock;
}

int MasterReadLock()
{
	Env* env = GetEnv();
	pthread_mutex_lock(&masterlock->guard);
	if ( env->masterlock & WRITELOCK_MASK ) {
		masterlock->writelock = false;
		if ( masterlock->waitcount > 0 ) 
			pthread_cond_broadcast(&masterlock->gate);
		env->masterlock &= ~WRITELOCK_MASK;
	}
	if ( env->masterlock & READLOCK_MASK ) {
		masterlock->readcount--;
		env->masterlock &= ~READLOCK_MASK;
	}
	if ( env->masterlock & TRANSACTIONLOCK_MASK ) {

	}
	while ( masterlock->writelock || masterlock->blocked ) {
		masterlock->waitcount++;
		pthread_cond_wait(&masterlock->gate,&masterlock->guard);
		masterlock->waitcount--;
	}
	masterlock->readcount++;
	pthread_mutex_unlock(&masterlock->guard);

	env->masterlock |= READLOCK_MASK;
	return env->masterlock;
}

int MasterUnLock()
{
	Env* env = GetEnv();
	pthread_mutex_lock(&masterlock->guard);
	if ( env->masterlock & WRITELOCK_MASK ) {
            	masterlock->owner = 0;
		masterlock->writelock = false;
		if ( masterlock->waitcount > 0 ) 
			pthread_cond_broadcast(&masterlock->gate);
		env->masterlock &= ~WRITELOCK_MASK;
	}
	if ( env->masterlock & READLOCK_MASK ) {
		masterlock->readcount--;
		if ( masterlock->readcount == 0 && 
		masterlock->transcount == 0 && 
		!masterlock->writelock && 
		masterlock->waitcount > 0 ) {
			pthread_cond_broadcast(&masterlock->gate);
		}
		env->masterlock &= ~READLOCK_MASK;
	}
	if ( env->masterlock & TRANSACTIONLOCK_MASK ) {

	}
	pthread_mutex_unlock(&masterlock->guard);

	return env->masterlock;
}

int TransactionLock() 
{
	Env* env = GetEnv();
	if (IsShutdownProcessingMode())
	{
		elog(ERROR,"System is shutting down code: %d",998);
	}

/*  do nothing if we already have the a transaction lock  */
	if ( !(env->masterlock & TRANSACTIONLOCK_MASK) ) {
		pthread_mutex_lock(&masterlock->guard);
/*  
	if there is a write lock and the thread is not the 
	holder, wait MKS 11.28.2001  
*/		
		while ( (masterlock->blocked || 
			masterlock->writelock) && 
			!(env->masterlock & WRITELOCK_MASK) 
		) {
			masterlock->waitcount++;
			pthread_cond_wait(&masterlock->gate,&masterlock->guard);
			masterlock->waitcount--;
		}
		masterlock->transcount++;
		pthread_mutex_unlock(&masterlock->guard);
		env->masterlock |= TRANSACTIONLOCK_MASK;
	}
        /*  tell DiscardAllInvalids that this env is in a transaction  */
        pthread_mutex_lock(env->env_guard);
        env->in_transaction = true;
        pthread_mutex_unlock(env->env_guard);
        
	return env->masterlock;
}

int TransactionUnlock()
{
	Env* env = GetEnv();

        pthread_mutex_lock(env->env_guard);
        env->in_transaction = false;
        pthread_mutex_unlock(env->env_guard);

	pthread_mutex_lock(&masterlock->guard);
	if ( env->masterlock & TRANSACTIONLOCK_MASK ) {
		masterlock->transcount--;
		if ( masterlock->transcount == 0 && 
			masterlock->readcount == 0 &&
			!masterlock->writelock && 
			masterlock->waitcount > 0
		) {
			pthread_cond_broadcast(&masterlock->gate);
		}
		env->masterlock &= ~TRANSACTIONLOCK_MASK;
	}
	pthread_mutex_unlock(&masterlock->guard);
	return env->masterlock;
}

void* AllocateEnvSpace(SectionId id,size_t size)
{
    HTAB* env = GetEnv()->global_hash;
    bool	found = FALSE;
    EnvEntry*    entry;
    
    if ( env == NULL ) {
        elog(FATAL,"no global environment");
    }
    
    entry = (EnvEntry*)hash_search(env,id,HASH_ENTER,&found);
    if ( found ) {
        elog(ERROR,"environment space already created");
    } else {
        entry->global_pointer = MemoryContextAlloc(GetEnvMemoryContext(),size);
        MemSet(entry->global_pointer,0x00,size);
        entry->global_size = size;
    }
    return entry->global_pointer;
}

long 
sectionid_hash(void* key,int size) {
        char* check = key;
        int put = 0;
        put |= check[0];
        put <<= 24;
        put |= check[1];
        put <<= 16;
        put |= check[2];
        put <<= 8;
        put |= check[3];
	return TRANSFORMSID(put);
}

void* GetEnvSpace(SectionId id)
{
    HTAB* env = GetEnv()->global_hash;
    bool found =FALSE;
    EnvEntry* entry;
    
    if ( env == NULL ) {
        elog(FATAL,"no global environment");
    }
    
    entry = (EnvEntry*)hash_search(env,id,HASH_FIND,&found);
    if ( !found ) {
        return NULL;
    } 
    
    return entry->global_pointer;
}

int MasterUpgradeLock()
{
	Env* env = GetEnv();
	if ( env->masterlock & 0x00000004 )
		return env->masterlock;
	if ( env->masterlock & 0x00000002 ) 
		return MasterWriteLock();
	if ( env->masterlock == 0x00000001 ) 
		return MasterReadLock();
	if ( env->masterlock == 0x00000000 ) 
		return MasterReadLock();

    return 0;
}

int MasterDowngradeLock()
{
	Env* env = GetEnv();

	if ( env->masterlock & 0x00000004 )
		return MasterReadLock();
	if ( env->masterlock & 0x00000002 ) 
		return MasterUnLock();
	if ( env->masterlock == 0x00000001 ) 
		return TransactionUnlock();
	if ( env->masterlock == 0x00000000 ) 
		return TransactionUnlock();

    return 0;
}

void
GoMultiuser()
{
	multiuser = true;
}

bool
IsMultiuser()
{
	return multiuser;
}

bool CheckForCancel() {
	EnvPointer* envp = pthread_getspecific(envkey);
        
        if ( CurrentMode == ShutdownProcessing ) return true;
	if ( envp == NULL ) return false;
	EnvPointer env = *envp; 
        if ( env!= NULL && (env)->cancelled ) {
            return TRUE;
        }
        if ( env->parent != NULL ) {
            return (env->parent->cancelled || !env->parent->in_transaction);
        }
        
        return FALSE;
}

void clearerror(Env* env) {
    env->cancelled = false;
	env->InError = false;
	memset(env->errortext,0,256);	
	memset(env->state,0,40);
	env->errorcode = 0;
}

/*  this is for non durable transactions in
case of system failure.  Gives us significant speed
up but loses on ACID test.  MKS   2.14.2001   */

bool IsTransactionCareful() {
	Env* env = GetEnv();
        
        if ( !IsMultiuser() ) {
            return true;
        }
        
        CommitType check = DEFAULT_COMMIT;

	if ( env->system_type != DEFAULT_COMMIT ) {
            check = env->system_type;   
        } else if ( env->user_type != DEFAULT_COMMIT ) {
            check = env->user_type;   
        } else {
            check = default_type;
        }

        switch ( check ) {
                case CAREFUL_COMMIT:
                case SYNCED_COMMIT:
                case FAST_CAREFUL_COMMIT:
                    return true;
                default:
                    return false;
        }
        return false;
	
}

bool IsLoggable(void) {
    	Env* env = GetEnv();
        if ( !IsMultiuser() ) {
            return false;
        }
        
        CommitType check = DEFAULT_COMMIT;

	if ( env->system_type != DEFAULT_COMMIT ) {
            check = env->system_type;   
        } else if ( env->user_type != DEFAULT_COMMIT ) {
            check = env->user_type;   
        } else {
            check = default_type;
        }

        switch ( check ) {
            case SYNCED_COMMIT:
                return false;
            default: 
                return true;
        }        
}

bool IsTransactionFriendly() {
	Env* env = GetEnv();
        CommitType check = DEFAULT_COMMIT;
        if ( !IsMultiuser() ) {
            return false;
        }
	if ( env->system_type != DEFAULT_COMMIT ) {
            check = env->system_type;   
        } else if ( env->user_type != DEFAULT_COMMIT ) {
            check = env->user_type;   
        } else {
            check = default_type;
        }

        switch ( check ) {
            case FAST_SOFT_COMMIT:
            case FAST_CAREFUL_COMMIT:
                return false;
            default:
                return true;
        }
        return true;
	
}

CommitType GetTransactionCommitType() {
	Env* env = GetEnv();
	if ( env->system_type != DEFAULT_COMMIT ) {
            return env->system_type;
	} else if ( env->user_type != DEFAULT_COMMIT ) {
            return env->user_type;
        } else {
            return default_type;
        }
}

void SetTransactionCommitType(CommitType trans) {
        Env*  env = GetEnv();
	switch ( trans ) {
                case DEFAULT_COMMIT: 
                    env->user_type = DEFAULT_COMMIT;
                    break;
		case USER_SOFT_COMMIT:
			env->user_type = SOFT_COMMIT;
			break;
		case USER_CAREFUL_COMMIT:
			env->user_type = CAREFUL_COMMIT;
			break;
		case USER_FAST_CAREFUL_COMMIT:
			env->user_type = FAST_CAREFUL_COMMIT;
			break;
		case TRANSACTION_SOFT_COMMIT:
			if (env->system_type < SOFT_COMMIT )
                            env->system_type = SOFT_COMMIT;
			break;
		case TRANSACTION_CAREFUL_COMMIT:
			if (env->system_type < CAREFUL_COMMIT )
                            env->system_type = CAREFUL_COMMIT;
			break;
		case TRANSACTION_FAST_CAREFUL_COMMIT:
			if (env->system_type < FAST_CAREFUL_COMMIT )
                            env->system_type = FAST_CAREFUL_COMMIT;
			break;
                case TRANSACTION_SYNCED_COMMIT:
                        env->system_type = SYNCED_COMMIT;
			break;
/*  these come from initialization  */
		case CAREFUL_COMMIT:
			default_type = CAREFUL_COMMIT;
			break;
		case SOFT_COMMIT:
			default_type = SOFT_COMMIT;
			break;
		case FAST_SOFT_COMMIT:
			default_type = FAST_SOFT_COMMIT;
			break;
                case FAST_CAREFUL_COMMIT:
			default_type = FAST_CAREFUL_COMMIT;
                        break;
		case SYNCED_COMMIT:
			default_type = SYNCED_COMMIT;                        
			break;
            default: 
                default_type = trans;
                
	}
}

void ResetTransactionCommitType( void ) {
    GetEnv()->system_type = DEFAULT_COMMIT;
}


MemoryContext GetEnvMemoryContext( void ) {
    return GetEnv()->global_context;
}

void SetProcessingMode(ProcessingMode mode) {
    if ( CurrentMode == InitProcessing && mode == NormalProcessing ) {
        CurrentMode = NormalProcessing;
    } else if ( mode == BootstrapProcessing || mode == ShutdownProcessing ) {
        CurrentMode = mode;
    } else {
        GetEnv()->Mode = mode;
    }
}

ProcessingMode GetProcessingMode(void)
{
    CheckForCancel();
    if ( CurrentMode == InitProcessing || CurrentMode == BootstrapProcessing || CurrentMode == ShutdownProcessing ) {
        return CurrentMode;
    } else {
        return ((GetEnv()->Mode == NormalProcessing) ? CurrentMode : GetEnv()->Mode);
    }
}

long prandom(void) {
	return lrand48();
}

void sprandom(unsigned int seed) {
	srand48(seed);
}

void ptimeout(struct timespec* timeval,int to) {
    clock_gettime(WHICH_CLOCK,timeval);
    timeval->tv_nsec += ((to % 1000) * 1000000);
    timeval->tv_sec += ((to / 1000) + (timeval->tv_nsec/1000000000));
    timeval->tv_nsec = timeval->tv_nsec % 1000000000;    
}

CommBuffer* ConnectCommBuffer(void* args,int (*mover)(void*, int, char*, int)) {
    CommBuffer* comm = palloc(sizeof(CommBuffer));
    comm->args = args;
    comm->pipe = mover;
    comm->header = sizeof(CommBuffer);
    SETBUFFERED(comm);
    return comm;
}

void* DisconnectCommBuffer(CommBuffer* buffer) {
    void* args = buffer->args;
    pfree(buffer);
    return args;
}

void 
PrintEnvMemory( void ) {
    int counter = 0;
    Env* home = GetEnv();
    SetEnv(NULL);
    pthread_mutex_lock(&envlock);
    for (counter = 0;counter <GetMaxBackends();counter++) {
        if ( envmap[counter] != NULL ) {
            pthread_mutex_lock(envmap[counter]->env_guard);
            if ( envmap[counter]->owner != 0 ) {
            /*  trick GetEnv() to use the correct env for printing out logging messages */
                pthread_setspecific(envkey,&envmap[counter]);
                size_t amt = MemoryContextStats(envmap[counter]->global_context);
                env_log(envmap[counter],"Total env memory: %ld",amt);     
                pthread_setspecific(envkey,NULL);
           } else {
                envmap[counter]->print_memory = true;
            }
            pthread_mutex_unlock(envmap[counter]->env_guard);
        }
    }
    pthread_mutex_unlock(&envlock);
    SetEnv(home);
}

void 
PrintUserMemory( void ) {
        size_t amt = MemoryContextStats(GetEnv()->global_context);
        env_log(GetEnv(),"Total env memory: %ld",amt);     
}
#ifdef _GNU_SOURCE
void 
glibc_memory_fail(enum mcheck_status err) {
    printf("memory allocation failed");
    abort();
}
#endif
int 
memory_fail(void) {
    printf("memory allocation failed");
    abort();
}

void*
base_mem_alloc(size_t size) {
#ifdef SUNOS
    size_t* pointer = umem_alloc(size + sizeof(size_t), UMEM_NOFAIL);
#else 
    size_t* pointer = malloc(size + sizeof(size_t));
#endif
    if ( pointer == NULL ) {
        memory_fail();
    } 
    *pointer = size;
    return (pointer + 1);
}

void
base_mem_free(void * pointer) {
    size_t* mark = pointer;
    mark -= 1;
#ifdef SUNOS
    umem_free(mark, *mark + sizeof(size_t));
#else
#ifdef _GNU_SOURCE
    enum mcheck_status status = mprobe(mark);
    switch (status) {
        case MCHECK_DISABLED:
        case MCHECK_OK:
            free(mark);
            break;
        case MCHECK_HEAD:
        case MCHECK_TAIL:
        case MCHECK_FREE:
            abort();
    }
#else
    free(mark);
#endif
#endif
}


void*
base_mem_realloc(void * pointer, size_t size) {
    size_t* mark = pointer;
#ifdef SUNOS
    size_t*  moved = umem_alloc(size + sizeof(size_t), UMEM_NOFAIL);
#else
    size_t*  moved = malloc(size + sizeof(size_t));
#endif
    *moved = size;

    if ( mark != NULL ) {
        memmove((moved + 1), mark, *(mark - 1));
        base_mem_free(pointer);
    }

    return (moved + 1);
}

void user_log(char* pattern, ...) {
     char            msg[256];
   va_list         args;

    va_start(args, pattern);
    vsprintf(msg,pattern,args);
#ifdef SUNOS
    DTRACE_PROBE2(mtpg,env__msg,env->eid,msg);  
#endif
#ifdef DEBUGLOGS
    elog(DEBUG,"eid:%d -- %s",env->eid,msg); 
#endif
    va_end(args);
}


void  env_log(Env* env, char* pattern, ...) {
    char            msg[256];
    va_list         args;

    va_start(args, pattern);
    vsprintf(msg,pattern,args);
#ifdef SUNOS
    DTRACE_PROBE2(mtpg,env__msg,env->eid,msg);  
#endif
#ifdef DEBUGLOGS
    elog(DEBUG,"eid:%d -- %s",env->eid,msg); 
#endif
    va_end(args);
}
