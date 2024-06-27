/*-------------------------------------------------------------------------
*
 * env.h
 *	  this is an attempt to encapsulate all global variables 
 *	in an environment so that we can thread in one process
 *
 *-------------------------------------------------------------------------
 */

#ifndef _ENV_H_
#define _ENV_H_

#include <setjmp.h>

#include "postgres.h"
#include "storage/fd.h"
#include "utils/hsearch.h"


#ifdef __cplusplus
extern "C" {
#endif

typedef char SectionId[4];

#define SectionIdSize sizeof(SectionId)

#define SECTIONID(id) (id)
#define TRANSFORMSID(id) ( (PRIME1 ^ (id) * PRIME2) )

typedef enum ProcessingMode
{
	BootstrapProcessing,		/* bootstrap creation of template database */
	InitProcessing,				/* initializing system */
	NormalProcessing,			/* normal processing */
        ReadOnlyProcessing,
	ShutdownProcessing			/* processing */
} ProcessingMode;

/* the maximum number of sub environment helpers  */
#define  MAXSUBS    4

typedef enum CommitType 
{
	DEFAULT_COMMIT,
	SOFT_COMMIT,
        FAST_SOFT_COMMIT,
	CAREFUL_COMMIT,
        FAST_CAREFUL_COMMIT,
        SYNCED_COMMIT,
	USER_SOFT_COMMIT,
	USER_CAREFUL_COMMIT,
	USER_FAST_CAREFUL_COMMIT,
	TRANSACTION_SOFT_COMMIT,
	TRANSACTION_CAREFUL_COMMIT,
	TRANSACTION_FAST_CAREFUL_COMMIT,
	TRANSACTION_SYNCED_COMMIT
} CommitType;


typedef struct modifiedrwlock {
	int			waitcount;
	int  			readcount;
	int			transcount;
	bool			writelock;
        pthread_t               owner;
	bool			blocked;
	pthread_mutex_t		guard;
	pthread_cond_t		gate;
} masterlock_t;

/* ----------------
 *		private invalidation structures
 * ----------------
 */
typedef struct Environment* EnvPointer;

typedef void*  GlobalsCache;

typedef struct Environment {
        double                  version;   
        int                     eid;
        
        pthread_mutex_t*	env_guard;
        pthread_t               owner;
        
        bool                    print_memory;
    /*	from copy.c   */
	bool                    fe_eof;
	int                     lineno;			/* used by elog() -- dz */
	Oid                     LastOidProcessed;
/*   for lock.c  */
	int                     holdLock;
/*  error codes  */
        bool                    cancelled;
	bool			InError;
	char			errortext[256];
	char			state[40];
	int 			errorcode;
	Size 			tupleSize;
        jmp_buf			errorContext;
/*  global sets  */
	char* 			DatabaseName;  
	char* 			DatabasePath;  
	Oid			DatabaseId;
	char* 			UserName;
	int			UserId;
/*  processing mode */
	ProcessingMode  	Mode;           
/*  prepkeyset.c   */
	int			TotalExpr;
	int			insleep;
/*   masterlock status   */
	int 			masterlock;
        bool			in_transaction;
	int 			cartposition;

    void*			pipeout;
    void*			pipein;

 /*  files checking */
    File			temps[MAX_PRIVATE_FILES];
    FILE*			falloc[MAX_PRIVATE_FILES];	
/*  debug */
    char			CommandInfo[32];
/*  from redef.c  static variable   */
    char			buffer[1024];
/*  read.c  static var moved  */
    char*			saved_str;
    CommitType                  system_type;
    CommitType                  user_type;
    int*   			stackmark;
        
    HTAB*			PortalHashTable;    
 
   MemoryContext 		global_context;
   MemoryContext                current_context;
#ifndef TLS
   GlobalsCache                  stats_global;
   GlobalsCache                  transaction_info;
   GlobalsCache                  snapshot_holder;
   GlobalsCache                  memory_globals;
   GlobalsCache                  syscache_global;
   GlobalsCache                  relationcache_global;
   GlobalsCache                  cache_global;
   GlobalsCache                  thread_globals;
   GlobalsCache                  parser_info;   
   GlobalsCache                  cost_info;   
   GlobalsCache                  parse_expr_global;
   GlobalsCache                  temp_globals;
   GlobalsCache                  optimizer_globals;
   GlobalsCache                  buffers_global;
   GlobalsCache                  index_globals;
   GlobalsCache                  heap_globals;
   GlobalsCache                  operator_globals;
   GlobalsCache                  type_globals;
   GlobalsCache                  sequence_globals;
   GlobalsCache                  trigger_globals;
   GlobalsCache                  analyze_globals;
   GlobalsCache                  dol_globals;
   GlobalsCache                  platcat_globals;
    GlobalsCache                 parseranalyze_globals;
    GlobalsCache                 localbuffer_globals;
    GlobalsCache                 destination_globals;
    GlobalsCache                 regexp_globals;
    GlobalsCache                 invalidation_globals;
    GlobalsCache                 hash_globals;
    GlobalsCache                 tuplesort_globals;
    GlobalsCache                 xlog_globals;
    GlobalsCache                 pathcat_globals;
    GlobalsCache                 smgr_globals;    
#endif
    HTAB*   			global_hash;
    EnvPointer                  parent;
} Env;   

typedef struct commbuffer {
/*  allows us to set the buffer information as indirect if passing around as Datum  */
    int                 header;   
    void*                args;
    int			(*pipe)(void* args,int varType, void* buffer,int run);
} CommBuffer;

#define COMM_ERROR  -2

extern pthread_condattr_t	process_cond_attr;
extern pthread_mutexattr_t	process_mutex_attr;
extern masterlock_t*		masterlock;


#define IsBootstrapProcessingMode() ((bool)(GetProcessingMode() == BootstrapProcessing))
#define IsReadOnlyProcessingMode() ((bool)(GetProcessingMode() == ReadOnlyProcessing))
#define IsInitProcessingMode() ((bool)(GetProcessingMode() == InitProcessing))
#define IsNormalProcessingMode() ((bool)(GetProcessingMode() == NormalProcessing))
#define IsShutdownProcessingMode() ((bool)(GetProcessingMode() == ShutdownProcessing))

/*  Tried to do this with defines but had too many problems so
I modified flex to create mflex  ( modified the skel file to use 
thread local variable.  These defines do not work b/c flex uses a bunch of
defines itself which include the variables listed below.  It became 
easier just to modify the flex.skel, gen.c and main.c. and compile that

MKS  12-11-2000
 */

Env* CreateEnv(Env* parent);
Env* GetEnv(void);
bool SetEnv(void* env);
void DestroyEnv(void* env);

void* AllocateEnvSpace(SectionId id,size_t size);
void* GetEnvSpace(SectionId id);

Env* InitSystem(bool  isPrivate);
int DestroySystem(void);

void DiscardAllInvalids(void);

int MasterWriteLock(void);
int MasterReadLock(void);
int MasterUnLock(void);
int MasterUpgradeLock(void);
int MasterDowngradeLock(void);
int TransactionLock(void);
int TransactionUnlock(void);

void GoMultiuser(void);
bool IsMultiuser(void);

void clearerror(Env* env);
bool CheckForCancel(void);
void CancelEnvAndJoin(Env* env);
pthread_t FindChildThread(Env* env);

bool IsLoggable(void);
bool IsTransactionCareful(void);
bool IsTransactionFriendly(void);
void SetTransactionCommitType(CommitType careful);
CommitType GetTransactionCommitType(void);
void ResetTransactionCommitType(void);

MemoryContext GetEnvMemoryContext(void);
void PrintEnvMemory(void);
void PrintUserMemory(void);

ProcessingMode GetProcessingMode(void);
void SetProcessingMode(ProcessingMode mode);

long prandom(void);
void sprandom(unsigned int seed);
void ptimeout(struct timespec* ts,int to);

void user_log(char* pattern, ...);

CommBuffer* ConnectCommBuffer(void* args,int (*mover)(void*, int, void*, int));
void* DisconnectCommBuffer(CommBuffer* buffer);

#ifdef __cplusplus
}
#endif

#endif

