/*-------------------------------------------------------------------------
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
*		Myron Scott, mkscott@sacadia.com, 2.05.2001 
 *
 *-------------------------------------------------------------------------
 */
#include <unistd.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/types.h>
#ifdef SUNOS
#include <sys/pset.h>
#endif
#include <sys/time.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <ctype.h>

#include "postgres.h"  
#include "env/env.h"

#include "env/connectionutil.h"
#include "env/timestamp.h"

#include "utils/builtins.h"

#include "env/freespace.h"
#include "env/poolsweep.h"
#include "storage/multithread.h"
#include "utils/tqual.h"
#include "utils/syscache.h"

#include "c.h"

#include "access/htup.h"
#include "access/hash.h"
#include "access/heapam.h"
#include "miscadmin.h"
#include "utils/exc.h"
#include "utils/portal.h"
#include "storage/smgr.h"
#include "storage/fd.h"
#include "storage/sinvaladt.h"
#include "access/xlog.h"
#include "env/dbwriter.h"
#include "env/dolhelper.h"
#include "commands/vacuum.h"
#include "utils/relcache.h"
#include "utils/java.h"
#include "env/delegatedscan.h"

#include "storage/bufpage.h"
#include "version.h"

#ifdef USEGC
#include "gc/gc.h"
#endif

static Env* 				env;
static int 					PostPortName  = 5432;
//static const char* 			progname  = "mtpg";
static IpcMemoryKey 		ipc_key;
static 	bool				master;
static  bool				isPrivate = true;

extern void					StartupXLOG(void);
extern void					ShutdownXLOG(void);

extern int 					DebugLvl;
static int 					MaxBackends = MAXBACKENDS;

extern bool             DelegatedIndexBuild;
extern bool             FastIndexBuild;

static int					exclusive_lock = -1;
static char					lock_name[255];

static HTAB*        properties;

static bool initialized = false;

static void CreateProperties(void);

//static void checkDataDir(const char *DataDir, bool *DataDirOK);
static int checklockfile(void);

pthread_mutex_t    init_lock;

LIB_EXTERN bool initweaverbackend(const char* vars)
{    
        char*       dbname = NULL;
	char	   *reason;
	char	   datpath[MAXPGPATH],control[MAXPGPATH],xlogdir[MAXPGPATH];
        char*       lasts;
        char       *tofree, *token, *cursor;

#ifdef USEGC
        GC_INIT();
#endif
        tofree = cursor = strdup(vars);

        pthread_mutex_init(&init_lock, NULL);
        pthread_mutex_lock(&init_lock);
            
	char* dbug = getenv("PG_DEBUGLEVEL");
	char*  output = getenv("PG_LOGFILE");
	char*  nofsync = getenv("PG_NOFSYNC");
	char*  stdlog = getenv("PG_STDLOG");
	char*  servertype = getenv("PG_SERVERTYPE");
	DataDir = getenv("PGDATA");

	struct timeval     timer;	
	
	long 	seed 		= 0;	
	int64_t 	sptime 		= 0;

        int start_delay = 0;
        
        CreateProperties();
        
        while ( (token = strsep(&cursor, "=")) != NULL ) {
            const char* key = token;
            char* val = strsep(&cursor, ";");
            if ( strcmp(key,"debuglevel") == 0 ) {
                dbug = val;
            } else if ( strcmp(key,"logfile") == 0 ) {
                output = val;
            } else if ( strcmp(key,"nofsync") == 0 ) {
                nofsync = val;
            } else if ( strcmp(key,"stdlog") == 0 ) {
                stdlog = val;
            } else if ( strcmp(key,"servertype") == 0 ) {
                servertype = val;
            } else if ( strcmp(key,"objectloader") == 0 ) {
                SetJavaObjectLoader(val);
            } else if ( strcmp(key,"datadir") == 0 ) {
                DataDir = strdup(val);
            } else if ( strcmp(key,"delegatedtransfermax") == 0 ) {
                DelegatedSetTransferMax(atoi(val));
            } else if ( strcmp(key,"fastindexbuild") == 0 ) {
		FastIndexBuild = (toupper(val[0]) == 'T') ? true : false;
            } else if ( strcmp(key,"delegatedindexbuild") == 0 ) {
		DelegatedIndexBuild = (toupper(val[0]) == 'T') ? true : false;
            } else if ( strcmp(key,"start_delay") == 0 ) {
		start_delay = atoi(val);
            } else {
                NameData nkey;
                Name nval;
                bool found;
                
                namestrcpy(&nkey,key);
                nval = hash_search(properties,(char*)&nkey,HASH_ENTER,&found);
                namestrcpy(nval + 1,val);
            }
        }

        if ( start_delay ) {
            printf("startup delay %d on pid:%d\n",start_delay,getpid());
            fflush(stdout);
            sleep(start_delay);
        }
        
	master = false;
/*  this is the only route to start multithreaded, multiuser  */	
	GoMultiuser();

    /*  Mac OSX does not have system global pthread structures so MacOSX is private */
	/*  memory space only  MKS 11.28.2001  */
        ipc_key = PrivateIPCKey;
    
/*  create an exclusive semaphore so only one backend is using the
        data dir at a time   */
        if ( ipc_key == PrivateIPCKey ) {
            checklockfile();
	}    
	if ( dbug != NULL ) {
            DebugLvl = ( strcasecmp(dbug,"DEBUG") == 0 ) ? DEBUG : NOTICE;
	}
	if ( PropertyIsValid("buffers") ) {
		NBuffers = GetIntProperty("buffers");
	} else if ( PropertyIsValid("page_buffers") ) {
                NBuffers = GetIntProperty("page_buffers");
        } else if ( PropertyIsValid("buffercount") ) {
                NBuffers = GetIntProperty("buffercount");
        }
    
        MaxBuffers = NBuffers;
        if ( PropertyIsValid("maxbuffers") ) {
		MaxBuffers = GetIntProperty("maxbuffers");
        }
    
	if ( PropertyIsValid("maxbackends") ) {
            MaxBackends = GetIntProperty("maxbackends");
            if ( MaxBackends > MAXBACKENDS ) MaxBackends = MAXBACKENDS;
	}
	disableFsync = false;
	if ( nofsync != NULL ) {
		disableFsync = (toupper(nofsync[0]) == 'T') ? true : false;
	}
	if ( PropertyIsValid("transcareful") ) {
            if (GetBoolProperty("transcareful")) {
                SetTransactionCommitType(CAREFUL_COMMIT);
            } else {
                SetTransactionCommitType(SOFT_COMMIT);
            }
	} else {
            SetTransactionCommitType(SOFT_COMMIT);
        }
        
        if ( PropertyIsValid("enable_softcommits") ) {
            if (GetBoolProperty("enable_softcommits")) {
                SetTransactionCommitType(SOFT_COMMIT);
            } else {
                SetTransactionCommitType(CAREFUL_COMMIT);
            }
        }
        
        if ( PropertyIsValid("disable_crc") ) {
            DisableCRC(GetBoolProperty("disable_crc"));
        }
        
	gettimeofday(&timer,NULL);	

	env = InitSystem(isPrivate);	
        
	if ( output != NULL && strlen(output) > 0 ) {
		 strncpy(OutputFileName,output,MAXPGPATH);
	} else {
/*  standard output  */	
	}
	
	if ( stdlog != NULL ) {
		if ((toupper(stdlog[0]) == 'T')) DebugFileOpen();
	}
		
	SetProcessingMode(InitProcessing);   

        MemoryContextInit();

	if ( shmget(IPCKeyGetBufferMemoryKey(ipc_key),0,0) < 0 ) {
		CreateSharedMemoryAndSemaphores(ipc_key, MaxBackends);
		master = true;
	} else {
                AttachSharedMemoryAndSemaphores(ipc_key); 
	}

/*  this code sets up the proper directory for the database 
	most of it is lifted from postinit
	but I hacked some of it.  If the database doesn't exist
	or the directory is wrong, it dumps core.  I need to fix this.
	
*/

	if ( dbname == NULL ) dbname = "template1";
	SetDatabaseName(dbname);    

/*  change to the database base   */
/*        chdir(DataDir);       */
	ValidatePgVersion(DataDir, &reason);
        if (reason != NULL) {
                elog(NOTICE, "%s", reason);
                pthread_mutex_unlock(&init_lock);
                unlink(lock_name);
                return FALSE;
        }

	GetRawDatabaseInfo(dbname,&GetEnv()->DatabaseId, datpath);
	elog(DEBUG,"Database id is %lu",GetEnv()->DatabaseId);
	elog(DEBUG,"Build date is %s", BUILDTIME);
	elog(DEBUG,"Build byte order is %d", BYTE_ORDER);

	memset(datpath,0,MAXPGPATH);

	/* Verify if DataDir is ok */
	if (access(DataDir, F_OK) == -1) {
                elog(NOTICE, "Database system not found. Data directory '%s' does not exist.",DataDir);
                pthread_mutex_unlock(&init_lock);
                unlink(lock_name);
                return FALSE;
        }

	strncpy(datpath,DataDir,strlen(DataDir));
	strncpy(datpath+strlen(DataDir),(char*)"/base/",6);

	strncpy(datpath+strlen(DataDir)+6,dbname,strlen(dbname));

	snprintf(control, MAXPGPATH, "%s%cpg_control",
				 DataDir, SEP_CHAR);
	snprintf(xlogdir, MAXPGPATH, "%s%cpg_xlog",
				 DataDir, SEP_CHAR);

	SetControlFilePath(control);
	SetXLogDir(xlogdir);

	if ( master ) StartupXLOG();

	LockDisable(true);
        smgrinit();
	RelationInitialize(); 
        DBWriterInit();	
        
	DBCreateWriterThread(LOG_MODE);
        InitializeTransactionSystem();		/* pg_log,etc init/crash recovery here */
	InitFreespace();
        LockDisable(false);

	InitThread(DAEMON_THREAD);  

	/*
	 * Part of the initialization processing done here sets a read lock on
	 * pg_log.	Since locking is disabled the set doesn't have intended
	 * effect of locking out writers, but this is ok, since we only lock
	 * it to examine AMI transaction status, and this is never written
	 * after initdb is done. -mer 15 June 1992
	 */
/*  we have to do this here to see if we want to recover from a crash in the 
transaction system  */

        InitializeDol();                              /* Division of Labor System init */

	InitCatalogCache();  

	if ( PropertyIsValid("usegc") && !GetBoolProperty("usegc") ) {
		
	} else {
		PoolsweepInit(0);
	}

	/*
	 * Initialize the access methods. Does not touch files (?) - thomas
	 * 1997-11-01
	 */
	initam();  

/*  if there are recovered pages are present,
 *  index pages need to be scanned and items 
 *  that point to unused heap items need to be 
 *  removed.
 */
        {
            List* dbids = smgrdbrecoverylist();
            List*  item;

            if ( dbids != NULL ) {
                foreach(item, dbids) {
                    AddRecoverRequest(smgrdbrecoveryname(lfirsti(item)), lfirsti(item));
                }
                foreach(item, dbids) {
                    AddWaitRequest(smgrdbrecoveryname(lfirsti(item)), lfirsti(item));
                }   
                
                smgrcompleterecovery();
            }
        }
    
	dbname = NULL;

 	SetProcessingMode(NormalProcessing);   
        
    if ( DebugLvl > 1 ) {
	TransactionId  dd;	
	dd = GetNewTransactionId();
	elog(DEBUG,"Current Transaction %llu",dd);
	elog(DEBUG,"BLCKSZ size %d",BLCKSZ);
    }
	
	
/*   only do this if you are the master process */
if ( master ) {
	char		un[255];
        uid_t		uid;
        struct passwd*		uinfo;
        memset(un,0,sizeof(un));
#ifndef MACOSX
	cuserid(un);
#else
        if ( strlen(un) == 0 ) {
            uid = getuid();
            uinfo = getpwuid(uid);
            strncpy(un,uinfo->pw_name,sizeof(un));
            uid = getuid();
            if ( uid > 0 ) {
                uinfo = getpwuid(uid);
                strncpy(un,uinfo->pw_name,sizeof(un));     
            } else {
                strncpy(un,getenv("LOGNAME"),sizeof(un));
            }
        }
#endif

	SetPgUserName(un);
/*	SetUserId(); */
}
	sptime -= (( timer.tv_sec * 1000000 ) + timer.tv_usec);	
	seed = timer.tv_usec;
	gettimeofday(&timer,NULL);
	sptime += (( timer.tv_sec * 1000000 ) + timer.tv_usec);
	seed ^= timer.tv_usec;
	if ( DebugLvl > 1 ) {	
		elog(DEBUG,"startup time %.2f",(double)(sptime / 1000000.0));
	}
	sprandom(seed);
        
    initialized = true;
        
    pthread_mutex_unlock(&init_lock);
        
    SetEnv(NULL);

    free(tofree);
        
    return true;
}

bool 
isinitialized() {
        bool check = false;
        pthread_mutex_lock(&init_lock);
         check = initialized;
        pthread_mutex_unlock(&init_lock);
        
        return check;
}

int
GetMaxBackends() {
    return MaxBackends;
}

int
GetProcessorCount() {
    int procs = 1;
    char* prop = NULL;
    
    if ( initialized ) prop = GetProperty("processors");
    if ( prop != NULL ) {
        procs = atoi(prop);
    } else {
#ifdef SUNOS
        psetid_t  pset;
        procs = sysconf(_SC_NPROCESSORS_ONLN);
        if (pset_bind(PS_QUERY, P_PID, getpid(), &pset) == 0) {
         if (pset != PS_NONE) {
           uint_t pset_cpus;
           if (pset_info(pset, NULL, &pset_cpus, NULL) == 0) {
                procs = pset_cpus;
           }
         }
       }
#endif
    }
    
    return procs;
}

static int 
checklockfile(void) {
        char  pid[255];

        memset(pid,0x00,255);
        sprintf(pid,"%d",getpid());

        snprintf(lock_name,sizeof(lock_name),"%s%cLOCK",DataDir,SEP_CHAR);

        while ( exclusive_lock < 0 ) {
            exclusive_lock = open((const char*)lock_name,(O_WRONLY | O_EXCL | O_CREAT),0500);
            if ( exclusive_lock < 0 ) {
                char  check[255];
                memset(check,0x00,255);
                exclusive_lock = open((const char*)lock_name,(O_RDONLY),0500);
                if ( exclusive_lock < 0 ) {
                    printf("Data Directory in use.  System is Exiting...\n");
                    printf("delete %s to force startup\n",lock_name);
                    exit(2);
                } else {
                    pid_t checkid = 0;
                    read(exclusive_lock,check,255);

                    checkid = atoi(check);
                    checkid = getpgid(checkid);

                    if ( checkid < 0 && errno == EPERM ) {
                        printf("Permissions for group lookup not allowed \n");
                        printf("delete %s to force startup\n",lock_name);
                        exit(3);
                    } else if ( checkid < 0 && errno == ESRCH ) {
                        close(exclusive_lock);
                        unlink((const char*)lock_name);
                        exclusive_lock = -1;
                    } else {
                        printf("Data Directory in use by process %s.  System is Exiting...\n",check);
                        printf("delete %s to force startup\n",lock_name);
                        exit(4);
                    }
                }
            } else {
                write(exclusive_lock,pid,strlen(pid));
                fsync(exclusive_lock);
                close(exclusive_lock);
            }
        }
        return 1;
}

LIB_EXTERN bool
prepareforshutdown()
{
    if ( !isinitialized() ) return false;
	
    SetEnv(env);
        
    pthread_mutex_lock(&init_lock);
    initialized = false;
    pthread_mutex_unlock(&init_lock);
        
	SetProcessingMode(ShutdownProcessing);
/*  stop the poolsweep processing */
  
	PoolsweepDestroy();	
/*  wait for client threads to reach a safe spot to 
    exit */
	MasterWriteLock(); 
        
    SetEnv(NULL);
    
    return true;
}

LIB_EXTERN void
wrapupweaverbackend()
{
/*  not part of the inval message queue  */	
/*	CallableCleanupInvalidationState();    */
	SetEnv(env);
        
	ShutdownDBWriter();

	RelationCacheShutdown();  
        smgrshutdown();
        
	ShutdownProcess(master);
	if ( master ) {
		ShutdownXLOG();
	} 	

	ThreadReleaseLocks(false);
	ThreadReleaseSpins(GetMyThread());
		
	DestroyThread();
	
	proc_exit(-1);  
	
	MasterUnLock();  

        ShutdownVirtualFileSystem();

        elog(DEBUG,"system shutdown successful");
        
        SetEnv(NULL);
        DestroyEnv(env);
	DestroySystem();
    
        unlink(lock_name);
}
#ifdef UNUSED
static void
checkDataDir(const char *DataDir, bool *DataDirOK)
{
	if (DataDir == NULL)
	{
		fprintf(stderr, "%s does not know where to find the database system "
				"data.  You must specify the directory that contains the "
				"database system either by specifying the -D invocation "
			 "option or by setting the PGDATA environment variable.\n\n",
				progname);
		*DataDirOK = false;
	}
	else
	{
		char		path[MAXPGPATH];
		FILE	   *fp;

		snprintf(path, sizeof(path), "%s%cbase%ctemplate1%cpg_class",
				 DataDir, SEP_CHAR, SEP_CHAR, SEP_CHAR);
#ifndef __CYGWIN32__
		fp = AllocateFile(path, (char *)"r");
#else
		fp = AllocateFile(path, "rb");
#endif
		if (fp == NULL)
		{
			fprintf(stderr, "%s does not find the database system.  "
					"Expected to find it "
			   "in the PGDATA directory \"%s\", but unable to open file "
					"with pathname \"%s\".\n\n",
					progname, DataDir, path);
			*DataDirOK = false;
		}
		else
		{
			char	   *reason;

			/* reason ValidatePgVersion failed.  NULL if didn't */

			FreeFile(fp);

			ValidatePgVersion(DataDir, &reason);
			if (reason)
			{
				fprintf(stderr,
						"Database system in directory %s "
						"is not compatible with this version of "
						"Weaver, or we are unable to read the "
						"PG_VERSION file.  "
						"Explanation from ValidatePgVersion: %s\n\n",
						DataDir, reason);
				free(reason);
				*DataDirOK = false;
			}
			else
				*DataDirOK = true;
		}
	}
}
#endif

static void* PropertiesAlloc(Size size,void* cxt)
{
    return os_malloc(size);
}

static void PropertiesFree(void* pointer,void* cxt)
{
    os_free(pointer);   
}

void CreateProperties(void)
{
    HASHCTL		ctl;

    MemSet(&ctl, 0, (int) sizeof(ctl));
    ctl.keysize = sizeof(NameData);
    ctl.entrysize = sizeof(NameData) * 2;
    ctl.alloc = PropertiesAlloc;
    ctl.free = PropertiesFree;
    ctl.hcxt = NULL;
    properties = hash_create("system properties",100, &ctl, HASH_ELEM | HASH_ALLOC | HASH_CONTEXT);
}

char* GetProperty(char* key) {
    NameData   data;
    Name   val;
    bool found;
    
    if ( properties == NULL ) return NULL;
    namestrcpy(&data,key);
    
    val = hash_search(properties,(char*)&data,HASH_FIND,&found);
    
    if ( !found ) return NULL;
    
    return (char*)(val + 1);
}

int GetIntProperty(char* key) {
    char* val = GetProperty(key);
    if ( val == NULL ) return 0;
    if ( toupper(val[0]) == 'T') return 1;
    return atoi(val);
}

double GetFloatProperty(char* key) {
    char* val = GetProperty(key);
    if ( val == NULL ) return 0;
    return atof(val);
}

bool GetBoolProperty(char* key) {
    char* val = GetProperty(key);
    if ( val == NULL) return false;
    if ( toupper(val[0]) == 'T') return true;
    return false;
}

bool PropertyIsValid(char* key) {
    return ( GetProperty(key) != NULL );
}

void 
singleusershutdown(int code) {
        
        ShutdownDBWriter();
        smgrshutdown();
        ShutdownVirtualFileSystem();
        DestroyEnv(GetEnv());
        DestroySystem();
        
        exit(code);
}

