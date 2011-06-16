/*-------------------------------------------------------------------------
 *
 * BaseWeaverConnection.cc
 *	  Base code to connect weaver to java
 *
 * Portions Copyright (c) 2002-2006, Myron K Scott
 *
 *
 * IDENTIFICATION
 *	  $Header: /cvs/weaver/pgjava/BaseWeaverConnection.cc,v 1.8 2007/03/21 19:09:08 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */

#include <jni.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <strings.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <pthread.h>
#include <math.h>

#include "env/connectionutil.h"

#include "WeaverStmtManager.h"
#include "utils/java.h"
#include "WeaverValueExtractor.h"

#include "driver_weaver_WeaverInitializer.h"
#include "driver_weaver_BaseWeaverConnection.h"

#define BINDNULL	driver_weaver_BaseWeaverConnection_bindNull
#define BINDINTEGER  driver_weaver_BaseWeaverConnection_bindInteger
#define BINDSTRING  driver_weaver_BaseWeaverConnection_bindString
#define BINDDOUBLE  driver_weaver_BaseWeaverConnection_bindDouble
#define BINDCHARACTER  driver_weaver_BaseWeaverConnection_bindCharacter
#define BINDBOOLEAN  driver_weaver_BaseWeaverConnection_bindBoolean
#define BINDBINARY  driver_weaver_BaseWeaverConnection_bindBinary
#define BINDBLOB  driver_weaver_BaseWeaverConnection_bindBLOB
#define BINDDATE  driver_weaver_BaseWeaverConnection_bindDate
#define BINDDOUBLE  driver_weaver_BaseWeaverConnection_bindDouble
#define BINDLONG  driver_weaver_BaseWeaverConnection_bindLong
#define BINDFUNCTION  driver_weaver_BaseWeaverConnection_bindFunction
#define BINDSLOT driver_weaver_BaseWeaverConnection_bindSlot
#define BINDJAVA  driver_weaver_BaseWeaverConnection_bindJava
#define BINDTEXT  driver_weaver_BaseWeaverConnection_bindText
#define BINDSTREAM driver_weaver_BaseWeaverConnection_bindStream
#define BINDDIRECT driver_weaver_BaseWeaverConnection_bindDirect

JavaVM*   jvm;

jobject 		javaSideLog[MAXBACKENDS];
StmtMgr                 theManagers[MAXBACKENDS];
static          javacache*      Cache;

static pthread_mutex_t		allocator;
static pthread_mutexattr_t	allocatt;

static bool                     debug = false;
static bool                     shuttingdown = false;

extern int                      DebugLvl;

typedef struct commargs {
    JNIEnv*  env;
    jobject  target;
} CommArgs;

static int pipeout(void* args,char* buff,int start,int run);
static int pipein(void* args,char* buff,int start,int run);
static int direct_pipeout(void* args,char* buff,int start,int run);
static int direct_pipein(void* args,char* buff,int start,int run);

static int clean_output(StmtMgr mgr, int type, void* arg);
static int clean_input(StmtMgr mgr,int type, void* arg);

static jint getProperAgent(JNIEnv* env,jobject talker);
static StmtMgr getStmtMgr(JNIEnv* env,jobject id);
static bool confirmAgent(JNIEnv* env,jobject talker,StmtMgr stmt);
static jint  clearError(JNIEnv* env,jobject talker,StmtMgr link);
static jint  reportError(JNIEnv* env,jobject talker,StmtMgr link);
static int   translateType(jint type);

static StmtMgr  allocateWeaver(JNIEnv* env,jstring username,jstring password,jstring database);

static char object_loader[512];

JNIEXPORT void JNICALL Java_driver_weaver_WeaverInitializer_init(JNIEnv *env,jobject talkerObject, jstring jd)
{
	char		datapass[2048];
	memset(datapass,0,2048);
	
	memset(javaSideLog,0,sizeof(javaSideLog));
	memset(theManagers,0,sizeof(theManagers));
	
	pthread_mutexattr_init(&allocatt);
	pthread_mutex_init(&allocator,&allocatt);
	
	if ( jd != NULL ) {
            int len = (*env)->GetStringUTFLength(env,jd);
            (*env)->GetStringUTFRegion(env,jd,0,len,datapass);
        }

	initweaverbackend(datapass);
        Cache = CreateCache(env);
		
	(*env)->GetJavaVM(env,&jvm);

        SetJVM(jvm,NULL);
}

JNIEXPORT void JNICALL Java_driver_weaver_WeaverInitializer_close(JNIEnv *env,jobject talkerObject)
{
/*  shutdown any threads resources still hanging around */
	if ( prepareforshutdown() ) {
            shuttingdown = true;
                
            pthread_mutex_lock(&allocator);

            for (int x = 0;x<GetMaxBackends();x++) {
                    if (theManagers[x] != NULL ) {
                            Init(theManagers[x],clean_input,clean_output);
                            DestroyWeaverConnection(theManagers[x]);
                            theManagers[x] = NULL;
                    }
                    if ( javaSideLog[x] != NULL ) {
                            (*env)->DeleteGlobalRef(env,javaSideLog[x]);
                            javaSideLog[x] = NULL;    
                    }
            }
                
            pthread_mutex_unlock(&allocator);

            wrapupweaverbackend();
        }
        DropCache(env);
        
	pthread_mutex_destroy(&allocator);
}

JNIEXPORT jboolean JNICALL Java_driver_weaver_BaseWeaverConnection_grabConnection
  (JNIEnv * env, jobject talkerObject, jstring theName, jstring thePassword, jstring theConnect)
 {
	int			x;

        if ( shuttingdown ) {            
            (*env)->ThrowNew(env,Cache->exception,"shutting down");
            return JNI_FALSE;
        }
        
	pthread_mutex_lock(&allocator);
	for(x=0;x<GetMaxBackends();x++)
	{
		if (theManagers[x] == NULL) break;
	}

        if ( x < GetMaxBackends() ) {
            jobject tracker = (*env)->GetObjectField(env,talkerObject,Cache->idfield);
            (*env)->SetIntField(env,tracker,Cache->tracker,x);

            theManagers[x] = allocateWeaver(env,theName,thePassword,theConnect);

            if ( theManagers[x] != NULL ) {
                if ( IsValid(theManagers[x]) ) {
                    javaSideLog[x] = (*env)->NewGlobalRef(env,talkerObject);
                } else {
                    DestroyWeaverConnection(theManagers[x]);
                    theManagers[x] = NULL;
                }
            } 
            if ( theManagers[x] == NULL ) {
                   if (!(*env)->ExceptionOccurred(env) ) 
                        (*env)->ThrowNew(env,Cache->exception,"User not valid");
            }
       } else {
           if (!(*env)->ExceptionOccurred(env) ) 
                (*env)->ThrowNew(env,Cache->exception,"Too many users");
       }
//  done grabbing
//  logging the id and creating statement space if logon is valid
	
	pthread_mutex_unlock(&allocator);

        return !(*env)->ExceptionOccurred(env);
    }

JNIEXPORT void JNICALL Java_driver_weaver_BaseWeaverConnection_connectSubConnection
  (JNIEnv * env, jobject talkerObject, jobject parent) {
	int			x;
        int                     target;
        StmtMgr    cparent = NULL;

        if ( shuttingdown ) {
            (*env)->ThrowNew(env,Cache->exception,"shutting down");
            return;
        }
        pthread_mutex_lock(&allocator);
	for(x=0;x<GetMaxBackends();x++)
	{
		if (theManagers[x] == NULL) break;
                else if ((*env)->IsSameObject(env,javaSideLog[x],parent)) cparent = theManagers[x];
	}
        target = x;
        while ( cparent == NULL && x < GetMaxBackends() ) {
            if ((*env)->IsSameObject(env,javaSideLog[x],parent)) cparent = theManagers[x];
            x++;
        }
        x = target;

        if ( x < GetMaxBackends() ) {
            jobject tracker = (*env)->GetObjectField(env,talkerObject,Cache->idfield);
            (*env)->SetIntField(env,tracker,Cache->tracker,x);

            theManagers[x] = CreateSubConnection(cparent);

            if ( theManagers[x] != NULL ) {
                if ( IsValid(theManagers[x]) ) {
                    javaSideLog[x] = (*env)->NewGlobalRef(env,talkerObject);
                } else {
                    DestroyWeaverConnection(theManagers[x]);
                    theManagers[x] = NULL;
                }
            }
            
            if ( theManagers[x] == NULL ) {
                   if (!(*env)->ExceptionOccurred(env) ) 
                        (*env)->ThrowNew(env,Cache->exception,"User not valid");
            }
        } else {
           if (!(*env)->ExceptionOccurred(env) ) 
                (*env)->ThrowNew(env,Cache->exception,"Too many users");
       }

	pthread_mutex_unlock(&allocator);
}

JNIEXPORT void JNICALL Java_driver_weaver_BaseWeaverConnection_dispose
  (JNIEnv *env, jobject talkerObject, jobject linkid)
{
	if ( (*env)->ExceptionOccurred(env) ) (*env)->ExceptionClear(env);
    //	get proper agent	
	jint link = getProperAgent(env,talkerObject);
        if ( link < 0 ) return;
        
        
	if ( (*env)->ExceptionOccurred(env) ) (*env)->ExceptionClear(env);
        
        if ( (*env)->IsInstanceOf(env,linkid,Cache->linkid) ) {
//  free all resources associated with this connection
            pthread_mutex_lock(&allocator);

            if (javaSideLog[link] != NULL && (*env)->IsSameObject(env,talkerObject, javaSideLog[link]))
            {
                if ( theManagers[link] != NULL ) {
                    Init(theManagers[link],clean_input,clean_output);
                    DestroyWeaverConnection(theManagers[link]);
                    theManagers[link] = NULL;
                }
                (*env)->DeleteGlobalRef(env,javaSideLog[link]);
                javaSideLog[link] = NULL;
            }
            else
            {
                long count = 0;
                for(count=0;count<MAXBACKENDS;count++)
                {
                    if ( (*env)->IsSameObject(env,talkerObject, javaSideLog[count]) )
                    {
                        Init(theManagers[link],clean_input,clean_output);
                        (*env)->DeleteGlobalRef(env,javaSideLog[count]);
                        javaSideLog[count] = NULL;
                        DestroyWeaverConnection(theManagers[count]);
                        theManagers[count] = NULL;
                    }
                }
            }
                pthread_mutex_unlock(&allocator);
                
        } else {
            if (javaSideLog[link] != NULL && (*env)->IsSameObject(env,talkerObject, javaSideLog[link])) {
                StmtMgr ref = getStmtMgr(env,linkid);
                Init(ref,clean_input,clean_output);
                DestroyWeaverStmtManager(ref); 
            }
        }

}

JNIEXPORT jlong JNICALL Java_driver_weaver_BaseWeaverConnection_beginTransaction(JNIEnv *env, jobject talkerObject)
{
    StmtMgr ref = getStmtMgr(env,talkerObject);
//	mark the beginning of the transaction
    if ( Begin(ref) ) {
        reportError(env,talkerObject,ref);
        return 0;
    } else {
        clearError(env,talkerObject,ref);
        return GetTransactionId(ref);
    }
}

JNIEXPORT jobject JNICALL Java_driver_weaver_BaseWeaverConnection_prepareStatement(JNIEnv *env, jobject talkerObject, jstring statement)
{
	const char* 	pass_stmt;
	jsize           statementlen;
        int x;	

        StmtMgr base = getStmtMgr(env,talkerObject);


        if ( statement == NULL ) {
            (*env)->ThrowNew(env,Cache->exception,"no statement");
            return 0;
        }
        
        base = CreateWeaverStmtManager(GetWeaverConnection(base));
        pass_stmt = (*env)->GetStringUTFChars(env,statement,NULL);
        ParseStatement(base,pass_stmt);
        (*env)->ReleaseStringUTFChars(env,statement,pass_stmt);
        
        jbyteArray ref = (*env)->NewByteArray(env,sizeof(StmtMgr));
        (*env)->SetByteArrayRegion(env,ref,0,sizeof(StmtMgr),(jbyte*)&base);
	
        return ref;
}

JNIEXPORT jlong JNICALL Java_driver_weaver_BaseWeaverConnection_parseStatement(JNIEnv *env, jobject talkerObject, jstring statement)
{
	const char* 	pass_stmt;
	jsize           statementlen;
        
        StmtMgr base = getStmtMgr(env,talkerObject);


        if ( statement == NULL ) {
            (*env)->ThrowNew(env,Cache->exception,"no statement");
            return 0;
        }

        pass_stmt = (*env)->GetStringUTFChars(env,statement,NULL);
	
// found reference and initing a statement space
        Init(base,clean_input,clean_output);
	short code = ParseStatement(base, pass_stmt);
        (*env)->ReleaseStringUTFChars(env,statement,pass_stmt);
        
	if ( code == -2 ) {
            if (!(*env)->ExceptionOccurred(env) ) 
                (*env)->ThrowNew(env,Cache->exception,"out of memory");
            return 0;
	}
        
        reportError(env,talkerObject,base);
        return GetCommandId(base);
}

JNIEXPORT void JNICALL Java_driver_weaver_BaseWeaverConnection_setInput(JNIEnv *env, jobject talkerObject, jobject linkid, jstring theVar, jint javaType, jobject bindPass)
{
 	char        var[64];
        jsize       varsize = 0;
        Input        bound;
        short       type = translateType(javaType);

        StmtMgr        base = getStmtMgr(env,linkid);
    
	varsize = (*env)->GetStringLength(env,theVar);	
	if ( varsize > 63 ) {
            if (!(*env)->ExceptionOccurred(env) ) 
                (*env)->ThrowNew(env,Cache->exception,"bind name too long");
            return;
	}
	(*env)->GetStringUTFRegion(env,theVar,0, varsize, var); 		
	var[varsize] = 0;
        
        if ( (*env)->IsSameObject(env,bindPass,NULL) ) {
            bound = SetInputValue(base,var,type,NULL,0);
        } else if ( type == STREAMTYPE ) {
            Pipe pipe = PipeConnect(base,(*env)->NewWeakGlobalRef(env,bindPass),direct_pipein);
            bound = SetInputValue(base,var,type,pipe, -1); /*  -1 length means use the maxlength for the type */
            pipe = SetUserspace(base,InputToBound(bound),pipe);
            if ( pipe != NULL ) {
                jobject ref = PipeDisconnect(base,pipe);
                (*env)->DeleteWeakGlobalRef(env,ref);
            }
        } else {
            PassInValue(env,base,var,type,bindPass);
        }
//  report errors
	reportError(env,talkerObject,base);
}

JNIEXPORT void JNICALL Java_driver_weaver_BaseWeaverConnection_getOutput
  (JNIEnv *env, jobject talkerObject, jobject linkid, jint index, jint javaType, jobject target)
{
    Output bound = NULL;
    short type = translateType(javaType);
    //	get proper agent

    StmtMgr base = getStmtMgr(env,linkid);

// check for valid link
        if ( type == STREAMTYPE ) {
            Pipe pipe = PipeConnect(base,(*env)->NewWeakGlobalRef(env,target),direct_pipeout);
            bound = SetOutputValue(base,index,type,pipe,-1);
            pipe = SetUserspace(base,OutputToBound(bound),pipe);
            if ( pipe != NULL ) {
                jobject ref = PipeDisconnect(base,pipe);
                (*env)->DeleteWeakGlobalRef(env,ref);
            }
        } else {
            bound = OutputLink(base,index,type);
            (*env)->DeleteWeakGlobalRef(env,SetUserspace(base,OutputToBound(bound),(*env)->NewWeakGlobalRef(env,target)));
        }
//  report errors
	reportError(env,talkerObject,base);
}


JNIEXPORT jlong JNICALL Java_driver_weaver_BaseWeaverConnection_executeStatement
  (JNIEnv *env, jobject talkerObject, jobject linkid)
{
	jclass classID = (*env)->GetObjectClass(env,talkerObject);
    //	get proper agent	
        StmtMgr ref = getStmtMgr(env,linkid);
// exec
        if ( Exec(ref) ) {
// report errors
            reportError(env,talkerObject,ref);
            return 0;
        } else {
            clearError(env,talkerObject,ref);
            return Count(ref);
        }
}

JNIEXPORT jboolean JNICALL Java_driver_weaver_BaseWeaverConnection_fetchResults
  (JNIEnv *env, jobject talkerObject,jobject linkid)
{
    //	get proper agent	
	StmtMgr ref = getStmtMgr(env,linkid);
//	fetch        
//  pass results to java if there are no errors
	if ( !Fetch(ref) ) {
            PassResults(env, ref);
            return JNI_TRUE;
        } else {
// report errors
            if ( GetErrorCode(ref) == 0 ) return JNI_FALSE;

            if ( GetErrorCode(ref) == 102 ) {
                (*env)->ThrowNew(env,Cache->truncation,GetErrorText(ref));
            } else {
                reportError(env,talkerObject,ref);
            }
            return JNI_FALSE;
        }
}

JNIEXPORT void JNICALL Java_driver_weaver_BaseWeaverConnection_cancelTransaction
  (JNIEnv *env, jobject talkerObject)
{
    //	get proper agent	
	StmtMgr ref = getStmtMgr(env,talkerObject);

	Cancel(ref);
}

JNIEXPORT void JNICALL Java_driver_weaver_BaseWeaverConnection_prepareTransaction(JNIEnv *env, jobject talkerObject)
{
    //	get proper agent	
        StmtMgr base = getStmtMgr(env,talkerObject);
	
// prepase statement for commit
	if ( base == NULL ) return;
	if ( Prepare(base) ) {
// report errors
	    reportError(env,talkerObject,base);
	}
}


JNIEXPORT void JNICALL Java_driver_weaver_BaseWeaverConnection_commitTransaction
  (JNIEnv *env, jobject talkerObject)
{
    //	get proper agent	
        StmtMgr base = getStmtMgr(env,talkerObject);

        
	if ( base == NULL ) return;
//  initialize to statement area for a new statement and free bound globals
        Init(base,clean_input,clean_output);
//  commit the previous statement
	if ( Commit(base) ) {
            reportError(env,talkerObject,base);
	}
}

JNIEXPORT void JNICALL Java_driver_weaver_BaseWeaverConnection_abortTransaction
  (JNIEnv *env, jobject talkerObject)
{
    //	get proper agent	
        StmtMgr base = getStmtMgr(env,talkerObject);

        
	if ( base == NULL ) return;
//  initialize to statement area for a new statement and free bound globals
        Init(base,clean_input,clean_output);
//  commit the previous statement
	if ( Rollback(base) ) {
		reportError(env,talkerObject,base);
	}
}


JNIEXPORT void JNICALL Java_driver_weaver_BaseWeaverConnection_beginProcedure
  (JNIEnv *env, jobject talkerObject)
{
    //	get proper agent	
        StmtMgr base = getStmtMgr(env,talkerObject);
	
        
	if ( base == NULL ) return;

//  commit the previous statement
	if ( BeginProcedure(base) ) {
// report errors
		reportError(env,talkerObject,base);
	}
}


JNIEXPORT void JNICALL Java_driver_weaver_BaseWeaverConnection_endProcedure
  (JNIEnv *env, jobject talkerObject)
{
    //	get proper agent	
        StmtMgr base = getStmtMgr(env,talkerObject);
	
        
	if ( base== NULL ) return;
//  commit the previous statement
	if ( EndProcedure(base) ) {
// report errors
		reportError(env,talkerObject,base);
	}
}

JNIEXPORT jlong JNICALL Java_driver_weaver_BaseWeaverConnection_getCommandId
  (JNIEnv *env, jobject talkerObject)
{
    //	get proper agent	
        StmtMgr base = getStmtMgr(env,talkerObject);

        
	if ( base == NULL ) return -1;

	return GetCommandId(base);
}

JNIEXPORT jlong JNICALL Java_driver_weaver_BaseWeaverConnection_getTransactionId
  (JNIEnv *env, jobject talkerObject)
{
    //	get proper agent	
        StmtMgr base = getStmtMgr(env,talkerObject);
	
        
	if ( base == NULL ) return -1;

	return GetTransactionId(base);
}

JNIEXPORT void JNICALL Java_driver_weaver_BaseWeaverConnection_userLock
  (JNIEnv *env, jobject talkerObject, jstring group, jint val, jboolean lock)
{
	char 		lockswitch = (char)(lock == JNI_TRUE);
	int		sqlerror = 0;
	
    //	get proper agent	
        StmtMgr base = getStmtMgr(env,talkerObject);

        
	jsize namelen = (*env)->GetStringLength(env,group);
	char  name[64];

	if ( namelen > 63 ) {
            if (!(*env)->ExceptionOccurred(env) ) 
                (*env)->ThrowNew(env,Cache->exception,"userlock name is too long");
            return;
	}
	
	name[namelen] = 0;

	(*env)->GetStringUTFRegion(env,group,0,namelen,name);

        if ( UserLock(base,name,(uint32_t)val,lockswitch) ) {
// report errors
		reportError(env,talkerObject,base);
	}
}

JNIEXPORT void JNICALL Java_driver_weaver_BaseWeaverConnection_streamExec
  (JNIEnv * env, jobject talkerObject, jstring statement)
{	
    const char*  state;
    jboolean     copy;		
    CommArgs     commenv;
    
    commenv.env = env;
    commenv.target = talkerObject;
    //	get proper agent	
        StmtMgr base = getStmtMgr(env,talkerObject);
	

    state = (*env)->GetStringUTFChars(env,statement,&copy);
                
    ConnectStdIO(base,&commenv,pipein,pipeout);

    if ( StreamExec(base,(char*)state ) ) {
        reportError(env,talkerObject,base);
    } else {
       (*env)->ReleaseStringUTFChars(env,statement,state);
    }

    DisconnectStdIO(base);
}

static jchar* convertStringBytes(char* target,char* source,int limit)
{

	return NULL;
}

static StmtMgr getStmtMgr(JNIEnv* env, jobject linkid) {
    StmtMgr ref;
            
    if ( (*env)->IsInstanceOf(env,linkid,Cache->talker) ) {
        ref = theManagers[getProperAgent(env,linkid)];
    } else if ( (*env)->IsInstanceOf(env,linkid,Cache->linkid) ) {
        ref = theManagers[getProperAgent(env,linkid)];
    } else {
        jbyte* pointer = (*env)->GetByteArrayElements(env,linkid,NULL);
        ref = *(StmtMgr*)pointer;
        (*env)->ReleaseByteArrayElements(env,linkid,pointer,NULL);
    }
    if ( !IsValid(ref) ) {
        if (!(*env)->ExceptionOccurred(env) ) 
            (*env)->ThrowNew(env,Cache->exception,"agent not valid");   
        ref = NULL;
    }
    return ref;
}

static jint clearError(JNIEnv* env,jobject talkerObject,StmtMgr base)
{
        if ( (*env)->ExceptionOccurred(env) ) return 2;
        
	(*env)->SetIntField(env,talkerObject,Cache->result,0);
	
	return 0;
}

static jint reportError(JNIEnv* env,jobject talkerObject,StmtMgr base)
{
        jint code = (jint)GetErrorCode(base);
        
        if ( (*env)->ExceptionOccurred(env) ) {
            (*env)->ExceptionDescribe(env);
            (*env)->ExceptionClear(env);
        }
        
	(*env)->SetIntField(env,talkerObject,Cache->result,code);
        
        if (code != 0 ) {
            const char* errtxt = GetErrorText(base);
            const char* statetxt = GetErrorState(base);
            char combo[255];

            if ( !errtxt ) errtxt = "no error text";
            if ( statetxt ) statetxt = "NOSTATE";

            jstring et = (*env)->NewStringUTF(env,errtxt);
            jstring st = (*env)->NewStringUTF(env,statetxt);

            (*env)->SetObjectField(env,talkerObject,Cache->eText,et);
            (*env)->SetObjectField(env,talkerObject,Cache->eState,st);

            snprintf(combo,255,"%s: %s -- err: %d",statetxt,errtxt,code);
            (*env)->ThrowNew(env,Cache->exception,combo);
        }
	
	return 0;
}

static jint getProperAgent(JNIEnv * env,jobject talker)
{
        if (  (*env)->ExceptionOccurred(env) ) {
            (*env)->ExceptionDescribe(env);
            return -1;
        }
         
        if ( shuttingdown ) {
            (*env)->ThrowNew(env,Cache->exception,"shutting down");
            return -1;
        }
        
        jobject tracker = NULL;
        
        if ( (*env)->IsInstanceOf(env,talker,Cache->talker) ) {
            tracker =  (*env)->GetObjectField(env,talker,Cache->idfield);
        } else if ( (*env)->IsInstanceOf(env,talker,Cache->linkid) ) {
            tracker = talker;
        }
        
	jint 	        link =  (*env)->GetIntField(env,tracker,Cache->tracker);

        if ( link >= MAXBACKENDS || link < 0 ) {
            link = -1;
        }

        if ( link < 0 ) {
            if (!(*env)->ExceptionOccurred(env) ) 
                (*env)->ThrowNew(env,Cache->exception,"agent not valid");
        }
        
        return link;
}

static bool confirmAgent(JNIEnv* env,jobject talker,StmtMgr stmt) {
    jint link = getProperAgent(env,talker);
    
    if ( !(*env)->IsSameObject(env,talker,javaSideLog[link]) ) return false;
    if ( theManagers[link] == NULL || !IsValid(theManagers[link]) ) return false;
    if ( stmt == NULL || IsValid(stmt) ) return false;
    return true;
}


static int direct_pipeout(void* arg,char* buff,int start,int run)
{
    JNIEnv*  env = NULL;
    jobject target = arg;
    
    (*jvm)->AttachCurrentThread(jvm, (void **)&env, NULL);

    if ( (*env)->IsSameObject(env,target,NULL) ) return PIPING_ERROR;

    jobject jb = (*env)->NewDirectByteBuffer(env,buff + start,run);
    
    if ( jb != NULL ) {
    	(*env)->CallVoidMethod(env,target,Cache->pipeout,jb);
        if ( (*env)->ExceptionOccurred(env) ) {
            return PIPING_ERROR;
        } else {
            return run;
        }
    } else {
        if ( (*env)->ExceptionOccurred(env) ) {
            return PIPING_ERROR;
        }
        return -1;
    }
}

static int direct_pipein(void* arg,char* buff,int start,int run)
{
    JNIEnv*  env = NULL;
    jobject target = arg;
        
    (*jvm)->AttachCurrentThread(jvm, (void **)&env, NULL);
    
    if ( (*env)->IsSameObject(env,target,NULL) ) return PIPING_ERROR;

    jobject jb = (*env)->NewDirectByteBuffer(env,buff + start,run);

    if ( jb != NULL ) {
    	jint count = (*env)->CallIntMethod(env,target,Cache->pipein,jb);
        if ( (*env)->ExceptionOccurred(env) ) {
            return PIPING_ERROR;
        }
        
    	return count;
    } else {
        if ( (*env)->ExceptionOccurred(env) ) {
            return PIPING_ERROR;
        }

        return -1;
    }
}

static int pipeout(void* args,char* buff,int start,int run)
{
    CommArgs*  commargs = args;
    JNIEnv*    env = commargs->env;
    jobject    target = commargs->target;

    if ( (*env)->IsSameObject(env,target,NULL) ) return PIPING_ERROR;
    
    jbyteArray jb = (*env)->NewByteArray(env,run);

    if ( jb != NULL ) {
    	(*env)->SetByteArrayRegion(env,jb,0,run,(jbyte*)(buff + start));
    	(*env)->CallVoidMethod(env,target,Cache->infoout,jb);
        if ( (*env)->ExceptionOccurred(env) ) {
            return PIPING_ERROR;
        } else {
            return run;
        }
    } else {
        if ( (*env)->ExceptionOccurred(env) ) {
            return PIPING_ERROR;
        }
        return -1;
    }
}

static int pipein(void* args,char* buff,int start,int run)
{
    CommArgs*  commargs = args;
    JNIEnv*    env = commargs->env;
    jobject    target = commargs->target;

    if ( (*env)->IsSameObject(env,target,NULL) ) return PIPING_ERROR;
    
    jbyteArray jb = (*env)->NewByteArray(env,run);

    if ( jb != NULL ) {
    	jint count = (*env)->CallIntMethod(env,target,Cache->infoin,jb);
        if ( (*env)->ExceptionOccurred(env) ) {
            return PIPING_ERROR;
        }
        if ( count > 0 ) {
            (*env)->GetByteArrayRegion(env,jb,0,count,(jbyte*)(buff + start));
        }
    	return count;
    } else {
        if ( (*env)->ExceptionOccurred(env) ) {
            return PIPING_ERROR;
        }
        return -1;
    }
}

int clean_output(StmtMgr mgr, int type, void* arg) {
    JNIEnv*  env = NULL;    
    (*jvm)->AttachCurrentThread(jvm, (void **)&env, NULL);
    
    if ( type == STREAMTYPE && arg != NULL ) {
        arg = PipeDisconnect(mgr,arg);
    }

    (*env)->DeleteWeakGlobalRef(env,arg);

    return 0;
}

int clean_input(StmtMgr mgr,int type, void* arg) {
    JNIEnv*  env = NULL;    
    (*jvm)->AttachCurrentThread(jvm, (void **)&env, NULL);
    
    if ( type == STREAMTYPE && arg != NULL ) {
        arg = PipeDisconnect(mgr,arg);
    }

    (*env)->DeleteWeakGlobalRef(env,arg);
    
    return 0;
}

static StmtMgr allocateWeaver(JNIEnv* env, jstring username,jstring password,jstring connection) {
	jsize passlen = (*env)->GetStringUTFLength(env,password);
	jsize namelen = (*env)->GetStringUTFLength(env,username);
	jsize connlen = (*env)->GetStringUTFLength(env,connection);

	if (( passlen > 63 || namelen > 63 || connlen > 63 ) ||
        ( passlen < 0 || namelen < 0 || connlen <= 0 ) )
        {
            if (!(*env)->ExceptionOccurred(env) ) 
                (*env)->ThrowNew(env,Cache->exception,"User not valid");
            return NULL;
	}

	char pass[64];
	char name[64];
	char conn[64];

	pass[passlen] = 0;
	name[namelen] = 0;
	conn[connlen] = 0;

	(*env)->GetStringUTFRegion(env,password,0,passlen,pass);
	(*env)->GetStringUTFRegion(env,username,0,namelen,name);
	(*env)->GetStringUTFRegion(env,connection,0,connlen,conn);

	return CreateWeaverStmtManager(CreateWeaverConnection(name, pass, conn));
         
}

static int translateType(jint type) {
      switch(type)
        {
            case BINDINTEGER:
                return INT4TYPE;
            case BINDSTRING:
                return VARCHARTYPE;
            case BINDCHARACTER:
                return CHARTYPE;                   
            case BINDBOOLEAN:
                return  BOOLTYPE;     
            case BINDBINARY:
                return  BYTEATYPE;  
            case BINDJAVA:
                return  JAVATYPE;
            case BINDBLOB:
                return  BLOBTYPE;
            case BINDTEXT:
                return  TEXTTYPE;
            case BINDDATE:
                return  TIMESTAMPTYPE;                   
            case BINDDOUBLE:
                return  DOUBLETYPE;
            case BINDLONG:
                return  LONGTYPE;
            case BINDFUNCTION:
                return  FUNCTIONTYPE;                             
            case BINDSTREAM:
            case BINDDIRECT:
                return  STREAMTYPE;
            default:
                return BINDNULL;
        }    
}

