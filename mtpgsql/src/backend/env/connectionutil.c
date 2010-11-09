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

static Env* 				env;
static int 					PostPortName  = 5432;
static const char* 			progname  = "mtpg";
static IpcMemoryKey 		ipc_key;
static 	void*				ipc_addr;
static 	bool				master;
static  bool				isPrivate;

extern void					StartupXLOG(void);
extern void					ShutdownXLOG(void);

extern int 					NBuffers;
extern int 					IndexBufferReserve;
extern int 					DebugLvl;
static int 					MaxBackends = MAXBACKENDS;

extern bool             disable_crc;
extern bool             DelegatedIndexBuild;
extern bool             FastIndexBuild;
extern bool             lingeringbuffers;

static int					exclusive_lock = -1;
static char					lock_name[255];

static HTAB*        properties;

static bool initialized = false;

static void CreateProperties();

static void checkDataDir(const char *DataDir, bool *DataDirOK);
static int checklockfile();

pthread_mutex_t    init_lock;

extern void initweaverbackend(char* vars)
{
        char*       dbname = NULL;
	char	   *reason;
	char	   *fullpath,datpath[MAXPGPATH],control[MAXPGPATH],xlogdir[MAXPGPATH];
        char*       lasts;

        char*       key = strtok_r(vars,"=",&lasts);
        char*       val = strtok_r(NULL,";",&lasts);

        pthread_mutex_init(&init_lock, NULL);
        pthread_mutex_lock(&init_lock);
            
        char* nbuff = getenv("PG_BUFFERCOUNT");
	char* ibufreserve = getenv("PG_INDEXBUFFERRESERVE");
	char* dbug = getenv("PG_DEBUGLEVEL");
	char* back = getenv("PG_MAXBACKENDS");
	char*  output = getenv("PG_LOGFILE");
	char*  nofsync = getenv("PG_NOFSYNC");
	char*  stdlog = getenv("PG_STDLOG");
	char*  transc = getenv("PG_TRANSCAREFUL");
	char*  servertype = getenv("PG_SERVERTYPE");
	char*  maxtrans = getenv("PG_MAXGROUPTRANS");
	char*  wtime = getenv("PG_WAITTIME");
	char*  hgc = getenv("PG_GCTHRESHOLD");
	char*  gcfactor = getenv("PG_GCSIZEFACTOR");
	char*  gcupdate = getenv("PG_GCUPDATEFACTOR");
	char*  force = getenv("PG_FORCE");
	char*  usegc = getenv("PG_USEGC");
	char*  loader = getenv("PG_OBJECTLOADER");
	DataDir = getenv("PGDATA");

	struct timeval     timer;	
	struct timezone     tz;
	
	long 	seed 		= 0;	
	int64_t 	sptime 		= 0;

        int maxxactgroup = -1;
	int timeout = 400;
	int hgci = -1;
	int gcfi = -1;
	int gcui = -1;
        int start_delay = 0;

        struct  varlena           align;

        CreateProperties();
        
        while ( key != NULL ) {
/*
            printf("%s=%s\n",key,val);
*/
            if ( strcmp(key,"buffercount") == 0 ) {
                nbuff = val;
            } else if ( strcmp(key,"indexbuffers") == 0 ) {
                ibufreserve = val;
            } else if ( strcmp(key,"debuglevel") == 0 ) {
                dbug = val;
            } else if ( strcmp(key,"maxbackends") == 0 ) {
                back = val;
            } else if ( strcmp(key,"logfile") == 0 ) {
                output = val;
            } else if ( strcmp(key,"nofsync") == 0 ) {
                nofsync = val;
            } else if ( strcmp(key,"stdlog") == 0 ) {
                stdlog = val;
            } else if ( strcmp(key,"transcareful") == 0 ) {
                transc = val;
            } else if ( strcmp(key,"servertype") == 0 ) {
                servertype = val;
            } else if ( strcmp(key,"maxgrouptrans") == 0 ) {
                maxtrans = val;
            } else if ( strcmp(key,"waittime") == 0 ) {
                wtime = val;
            } else if ( strcmp(key,"gcsizefactor") == 0 ) {
                gcfactor = val;
            } else if ( strcmp(key,"gcupdatefactor") == 0 ) {
                gcupdate = val;
            } else if ( strcmp(key,"force") == 0 ) {
                force = val;
            } else if ( strcmp(key,"usegc") == 0 ) {
                usegc = val;
            } else if ( strcmp(key,"objectloader") == 0 ) {
                SetJavaObjectLoader(val);
            } else if ( strcmp(key,"datadir") == 0 ) {
                DataDir = strdup(val);
            } else if ( strcmp(key,"delegatedtransfermax") == 0 ) {
                DelegatedSetTransferMax(atoi(val));
            } else if ( strcmp(key,"disable_crc") == 0 ) {
		disable_crc = (toupper(val[0]) == 'T') ? true : false;
            } else if ( strcmp(key,"fastindexbuild") == 0 ) {
		FastIndexBuild = (toupper(val[0]) == 'T') ? true : false;
            } else if ( strcmp(key,"delegatedindexbuild") == 0 ) {
		DelegatedIndexBuild = (toupper(val[0]) == 'T') ? true : false;
            } else if ( strcmp(key,"start_delay") == 0 ) {
		start_delay = atoi(val);
            } else if ( strcmp(key,"lingeringbuffers") == 0 ) {
		lingeringbuffers = (toupper(val[0]) == 'T') ? true : false;
            } else {
                NameData nkey;
                Name nval;
                bool found;
                
                namestrcpy(&nkey,key);
                nval = hash_search(properties,(char*)&nkey,HASH_ENTER,&found);
                namestrcpy(nval + 1,val);
/*
                printf("database parameter %s=%s inserting into properties table\n",key,val);
*/
            }
            key = strtok_r(NULL,"=",&lasts);
            val = strtok_r(NULL,";",&lasts);
        }

        if ( start_delay ) {
            printf("startup delay %d\n",start_delay);
            sleep(start_delay);
        }
        
	master = false;
/*  this is the only route to start multithreaded, multiuser  */	
	GoMultiuser();
	

	if ( servertype != NULL ) {
		isPrivate = !(strcasecmp(servertype,"SHARED") == 0);
	}    

        /*  only private mode is supported now  */
#ifndef NOTUSED
	if ( isPrivate ) {
		ipc_key = PrivateIPCKey;
	} else {
		ipc_key = PostPortName * 1000;
	}
#else  /*  Mac OSX does not have system global pthread structures so MacOSX is private */
	/*  memory space only  MKS 11.28.2001  */
    ipc_key = PrivateIPCKey;
#endif
    
/*  create an exclusive semaphore so only one backend is using the
        data dir at a time   */
        if ( ipc_key == PrivateIPCKey ) {
            checklockfile();
	}
    
    
	if ( dbug != NULL ) {
            DebugLvl = ( strcasecmp(dbug,"DEBUG") == 0 ) ? DEBUG : NOTICE;
	}
	if ( nbuff != NULL ) {
		NBuffers = atoi(nbuff);
	}
	if ( ibufreserve != NULL ) {
		IndexBufferReserve = atof(ibufreserve);
	}
	if ( back != NULL ) {
		MaxBackends = atoi(back);
                if ( MaxBackends > MAXBACKENDS ) MaxBackends = MAXBACKENDS;
	}
	disableFsync = false;
	if ( nofsync != NULL ) {
		disableFsync = (toupper(nofsync[0]) == 'T') ? true : false;
	}
	if ( transc != NULL ) {
		if (toupper(transc[0]) == 'T') 
			SetTransactionCommitType(CAREFUL_COMMIT);
		else 
			SetTransactionCommitType(SOFT_COMMIT);
	} else {
            SetTransactionCommitType(SOFT_COMMIT);
        }
        
        if ( GetProperty("enable_softcommits") != NULL ) {
            if ( toupper(*GetProperty("enable_softcommits")) == 'T') {
                SetTransactionCommitType(SOFT_COMMIT);
            } else {
                SetTransactionCommitType(CAREFUL_COMMIT);
            }
            
        }

	gettimeofday(&timer,NULL);	

	InitSystem(isPrivate);	
        env = GetEnv();
        
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
/*
	EnablePortalManager();   
*/

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


	GetRawDatabaseInfo(dbname,&GetEnv()->DatabaseId, datpath);
	elog(DEBUG,"Database id is %u",GetEnv()->DatabaseId);
	elog(DEBUG,"Build date is %s", BUILDTIME);
	elog(DEBUG,"Build byte order is %d", BYTE_ORDER);

	memset(datpath,0,MAXPGPATH);

	/* Verify if DataDir is ok */
	if (access(DataDir, F_OK) == -1)
                elog(FATAL, "Database system not found. Data directory '%s' does not exist.",DataDir);
/*  change to the database base   */
/*        chdir(DataDir);       */
	ValidatePgVersion(DataDir, &reason);
		if (reason != NULL)
			elog(FATAL, reason);


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

	AmiTransactionOverride(IsBootstrapProcessingMode());

/*  this starts up the DBwriter thread.  */
	if ( maxtrans != NULL ) maxxactgroup = atoi(maxtrans);
	if ( wtime != NULL ) timeout = atoi(wtime);
	if ( hgc != NULL ) hgci = atoi(hgc);
        hgc = GetProperty("gcthreshold");
        if ( hgc != NULL ) hgci = atoi(hgc);
	if ( gcfactor != NULL ) gcfi = atoi(gcfactor);
	if ( gcupdate != NULL ) gcui = atoi(gcupdate);

	LockDisable(true);
        smgrinit();
	RelationInitialize(); 
        DBWriterInit(maxxactgroup,timeout,hgci,gcui,gcfi);	
        
	DBCreateWriterThread();
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

	if ( usegc != NULL && toupper(usegc[0]) == 'F' ) {
		
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

	GetEnv()->Mode = NormalProcessing;   
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
                    int size = read(exclusive_lock,check,255);

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

extern void 
prepareforshutdown()
{
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
}

extern void
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
    return atoi(val);
}

double GetFloatProperty(char* key) {
    char* val = GetProperty(key);
    if ( val == NULL ) return 0;
    return atof(val);
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

