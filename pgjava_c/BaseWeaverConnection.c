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
static jint  clearError(JNIEnv* env,jobject talker,jint link);
static jint  reportError(JNIEnv* env,jobject talker,jint link);
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
	prepareforshutdown();
        
        shuttingdown = true;
        
	for (int x = 0;x<GetMaxBackends();x++) {
		if (theManagers[x] != NULL ) {
			Init(theManagers[x],clean_input,clean_output);
			DestroyWeaverStmtManager(theManagers[x]);
			theManagers[x] = NULL;
		}
		if ( javaSideLog[x] != NULL ) {
			(*env)->DeleteGlobalRef(env,javaSideLog[x]);
			javaSideLog[x] = NULL;    
		}
	}

	wrapupweaverbackend();
        DropCache(env);
        
	pthread_mutex_destroy(&allocator);
}

JNIEXPORT jboolean JNICALL Java_driver_weaver_BaseWeaverConnection_grabConnection
  (JNIEnv * env, jobject talkerObject, jstring theName, jstring thePassword, jstring theConnect)
 {
	int			x;

        if ( shuttingdown ) {            (*env)->ThrowNew(env,Cache->exception,"shutting down");
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
                    DestroyWeaverStmtManager(theManagers[x]);
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
                    DestroyWeaverStmtManager(theManagers[x]);
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

JNIEXPORT jlong JNICALL Java_driver_weaver_BaseWeaverConnection_beginTransaction(JNIEnv *env, jobject talkerObject)
{
    jint link = getProperAgent(env,talkerObject);
    if ( link < 0 ) return 0;
//	mark the beginning of the transaction
    if ( Begin(theManagers[link]) ) {
        reportError(env,talkerObject,link);
        return 0;
    } else {
        clearError(env,talkerObject,link);
        return GetTransactionId(theManagers[link]);
    }
}

JNIEXPORT jlong JNICALL Java_driver_weaver_BaseWeaverConnection_parseStatement(JNIEnv *env, jobject talkerObject, jstring thePassword)
{
	const char* 	thePass;
	long	passLen;        
	jsize  statementlen;
	char  statement[8092];
        
	jint link = getProperAgent(env,talkerObject);
        if ( link < 0 ) return 0;

        if ( thePassword == NULL ) {
            (*env)->ThrowNew(env,Cache->exception,"no statement");
            return 0;
        }

        statementlen = (*env)->GetStringLength(env,thePassword);

	if ( statementlen > 8091 ) {
            (*env)->ThrowNew(env,Cache->exception,"statment too long");
            return 0;
	}

	statement[statementlen] = 0;
	
// found reference and initing a statement space
        Init(theManagers[link],clean_input,clean_output);
	(*env)->GetStringUTFRegion(env,thePassword,0,statementlen,statement);	
	short code = ParseStatement(theManagers[link], statement, statementlen);

	if ( code == -2 ) {
            if (!(*env)->ExceptionOccurred(env) ) 
                (*env)->ThrowNew(env,Cache->exception,"out of memory");
            return 0;
	}
        
        reportError(env,talkerObject,link);
        return GetCommandId(theManagers[link]);
}
/*
JNIEXPORT void JNICALL Java_driver_weaver_BaseWeaverConnection_bind(JNIEnv *env, jobject talkerObject, jstring theVar, jint theType)
{
	jsize size = (*env)->GetStringLength(env,theVar);
	char transfer[64];
        Input bound = NULL;


	jint link = getProperAgent(env,talkerObject);
        if ( link < 0 ) return;
        
	if ( size > 63 ) {
            (*env)->ThrowNew(env,Cache->exception,"bind name too long");
            return;
	}

	transfer[size] = 0;
//	Add Bind
	(*env)->GetStringUTFRegion(env,theVar,0, size, transfer); 

	bound = AddBind(theManagers[link],transfer,translateType(theType));
        
        if ( bound == NULL ) {
            reportError(env,talkerObject,link);
	} else {
            void* userspace = SetUserspace(theManagers[link],InputToBound(bound),NULL);
            if ( userspace != NULL ) {
                clean_input(theManagers[link],translateType(theType),userspace);
            }
        }
}
*/

JNIEXPORT void JNICALL Java_driver_weaver_BaseWeaverConnection_setInput(JNIEnv *env, jobject talkerObject, jstring theVar, jint javaType, jobject bindPass)
{
 	char        var[64];
        jsize       varsize = 0;
        Input        bound;
        short       type = translateType(javaType);

        jint        link = getProperAgent(env,talkerObject);
        if ( link < 0 )  return;
    
	varsize = (*env)->GetStringLength(env,theVar);	
	if ( varsize > 63 ) {
            if (!(*env)->ExceptionOccurred(env) ) 
                (*env)->ThrowNew(env,Cache->exception,"bind name too long");
            return;
	}
	(*env)->GetStringUTFRegion(env,theVar,0, varsize, var); 		
	var[varsize] = 0;
        
        if ( (*env)->IsSameObject(env,NULL,bindPass) ) {
            bound = SetInputValue(theManagers[link],var,type,NULL,0);
        } else if ( type == STREAMTYPE ) {
            Pipe pipe = PipeConnect(theManagers[link],(*env)->NewGlobalRef(env,bindPass),direct_pipein);
            bound = SetInputValue(theManagers[link],var,type,pipe, -1); /*  -1 length means use the maxlength for the type */
            pipe = SetUserspace(theManagers[link],InputToBound(bound),pipe);
            if ( pipe != NULL ) {
                jobject ref = PipeDisconnect(theManagers[link],pipe);
                (*env)->DeleteGlobalRef(env,ref);
            }
        } else {
            PassInValue(env,theManagers[link],var,type,bindPass);
        }
//  report errors
	reportError(env,talkerObject,link);
}

JNIEXPORT void JNICALL Java_driver_weaver_BaseWeaverConnection_getOutput
  (JNIEnv *env, jobject talkerObject, jint index, jint javaType, jobject target)
{
    Output bound = NULL;
    short type = translateType(javaType);
    //	get proper agent

	jint link = getProperAgent(env,talkerObject);
        if ( link < 0 ) return;

// check for valid link
        if ( type == STREAMTYPE ) {
            Pipe pipe = PipeConnect(theManagers[link],(*env)->NewGlobalRef(env,target),direct_pipeout);
            bound = SetOutputValue(theManagers[link],index,type,pipe,-1);
            pipe = SetUserspace(theManagers[link],OutputToBound(bound),pipe);
            if ( pipe != NULL ) {
                jobject ref = PipeDisconnect(theManagers[link],pipe);
                (*env)->DeleteGlobalRef(env,ref);
            }
        } else {
            bound = OutputLink(theManagers[link],index,type);
            (*env)->DeleteGlobalRef(env,SetUserspace(theManagers[link],OutputToBound(bound),(*env)->NewGlobalRef(env,target)));
        }
//  report errors
	reportError(env,talkerObject,link);
}


JNIEXPORT jlong JNICALL Java_driver_weaver_BaseWeaverConnection_executeStatement
  (JNIEnv *env, jobject talkerObject)
{
	jclass classID = (*env)->GetObjectClass(env,talkerObject);
    //	get proper agent	
	jint link = getProperAgent(env,talkerObject);
        if ( link < 0 ) return 0;
// exec
        if ( Exec(theManagers[link]) ) {
// report errors
            reportError(env,talkerObject,link);
            return 0;
        } else {
            clearError(env,talkerObject,link);
            return Count(theManagers[link]);
        }
}

JNIEXPORT jboolean JNICALL Java_driver_weaver_BaseWeaverConnection_fetchResults
  (JNIEnv *env, jobject talkerObject)
{
    //	get proper agent	
	jint link = getProperAgent(env,talkerObject);
        if ( link < 0 ) return FALSE;
//	fetch
	if ( theManagers[link] == NULL ) return FALSE;
        
//  pass results to java if there are no errors
	if ( !Fetch(theManagers[link]) ) {
            PassResults(env, theManagers[link]);
            return JNI_TRUE;
        } else {
// report errors
            if ( GetErrorCode(theManagers[link]) == 0 ) return JNI_FALSE;

            if ( GetErrorCode(theManagers[link]) == 102 ) {
                (*env)->ThrowNew(env,Cache->truncation,GetErrorText(theManagers[link]));
            } else {
                reportError(env,talkerObject,link);
            }
            return JNI_FALSE;
        }
}


JNIEXPORT void JNICALL Java_driver_weaver_BaseWeaverConnection_disposeConnection
  (JNIEnv *env, jobject talkerObject)
{
	if ( (*env)->ExceptionOccurred(env) ) (*env)->ExceptionClear(env);
    //	get proper agent	
	jint link = getProperAgent(env,talkerObject);
        if ( link < 0 ) return;
        
	if ( (*env)->ExceptionOccurred(env) ) (*env)->ExceptionClear(env);

//  free all resoures associated with this connection
       if ( theManagers[link] != NULL ) {
            Init(theManagers[link],clean_input,clean_output);
            DestroyWeaverStmtManager(theManagers[link]);
            theManagers[link] = NULL;
        }

	if (javaSideLog[link] != NULL && (*env)->IsSameObject(env,talkerObject, javaSideLog[link]))
	{
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
                    DestroyWeaverStmtManager(theManagers[count]);
                    theManagers[count] = NULL;
                }
            }
	}
}

JNIEXPORT void JNICALL Java_driver_weaver_BaseWeaverConnection_cancelTransaction
  (JNIEnv *env, jobject talkerObject)
{
    //	get proper agent	
	jint link = getProperAgent(env,talkerObject);
        if ( link < 0 ) return;	

	Cancel(theManagers[link]);
}

JNIEXPORT void JNICALL Java_driver_weaver_BaseWeaverConnection_prepareTransaction(JNIEnv *env, jobject talkerObject)
{
    //	get proper agent	
	jint link = getProperAgent(env,talkerObject);
        if ( link < 0 ) return;	
// prepase statement for commit
	if ( theManagers[link] == NULL ) return;
	if ( Prepare(theManagers[link]) ) {
// report errors
	    reportError(env,talkerObject,link);
	}
}


JNIEXPORT void JNICALL Java_driver_weaver_BaseWeaverConnection_commitTransaction
  (JNIEnv *env, jobject talkerObject)
{
    //	get proper agent	
	jint link = getProperAgent(env,talkerObject);
        if ( link < 0 ) return;	
        
	if ( theManagers[link] == NULL ) return;
//  initialize to statement area for a new statement and free bound globals
        Init(theManagers[link],clean_input,clean_output);
//  commit the previous statement
	if ( Commit(theManagers[link]) ) {
            reportError(env,talkerObject,link);
	}
}

JNIEXPORT void JNICALL Java_driver_weaver_BaseWeaverConnection_abortTransaction
  (JNIEnv *env, jobject talkerObject)
{
    //	get proper agent	
	jint link = getProperAgent(env,talkerObject);
        if ( link < 0 ) return;	
        
	if ( theManagers[link] == NULL ) return;
//  initialize to statement area for a new statement and free bound globals
        Init(theManagers[link],clean_input,clean_output);
//  commit the previous statement
	if ( Rollback(theManagers[link]) ) {
		reportError(env,talkerObject,link);
	}
}


JNIEXPORT void JNICALL Java_driver_weaver_BaseWeaverConnection_beginProcedure
  (JNIEnv *env, jobject talkerObject)
{
    //	get proper agent	
	jint link = getProperAgent(env,talkerObject);
        if ( link < 0 ) return;	
        
	if ( theManagers[link] == NULL ) return;

//  commit the previous statement
	if ( BeginProcedure(theManagers[link]) ) {
// report errors
		reportError(env,talkerObject,link);
	}
}


JNIEXPORT void JNICALL Java_driver_weaver_BaseWeaverConnection_endProcedure
  (JNIEnv *env, jobject talkerObject)
{
    //	get proper agent	
	jint link = getProperAgent(env,talkerObject);
        if ( link < 0 ) return;	
        
	if ( theManagers[link] == NULL ) return;
//  commit the previous statement
	if ( EndProcedure(theManagers[link]) ) {
// report errors
		reportError(env,talkerObject,link);
	}
}

JNIEXPORT jlong JNICALL Java_driver_weaver_BaseWeaverConnection_getCommandId
  (JNIEnv *env, jobject talkerObject)
{
    //	get proper agent	
	jint link = getProperAgent(env,talkerObject);
        if ( link < 0 ) return -1;	
        
	if ( theManagers[link] == NULL ) return -1;

	return GetCommandId(theManagers[link]);
}

JNIEXPORT jlong JNICALL Java_driver_weaver_BaseWeaverConnection_getTransactionId
  (JNIEnv *env, jobject talkerObject)
{
    //	get proper agent	
	jint link = getProperAgent(env,talkerObject);
        if ( link < 0 ) return -1;	
        
	if ( theManagers[link] == NULL ) return -1;

	return GetTransactionId(theManagers[link]);
}

JNIEXPORT void JNICALL Java_driver_weaver_BaseWeaverConnection_userLock
  (JNIEnv *env, jobject talkerObject, jstring group, jint val, jboolean lock)
{
	char 		lockswitch = (char)(lock == JNI_TRUE);
	int		sqlerror = 0;
	
    //	get proper agent	
	jint link = getProperAgent(env,talkerObject);
        if ( link < 0 ) return;	
        
	jsize namelen = (*env)->GetStringLength(env,group);
	char  name[64];

	if ( namelen > 63 ) {
            if (!(*env)->ExceptionOccurred(env) ) 
                (*env)->ThrowNew(env,Cache->exception,"userlock name is too long");
            return;
	}
	
	name[namelen] = 0;

	(*env)->GetStringUTFRegion(env,group,0,namelen,name);

        if ( UserLock(theManagers[link],name,(uint32_t)val,lockswitch) ) {
// report errors
		reportError(env,talkerObject,link);
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
    jint link = getProperAgent(env,talkerObject);
    if ( link < 0 ) return;	

    state = (*env)->GetStringUTFChars(env,statement,&copy);
                
    ConnectStdIO(theManagers[link],&commenv,pipein,pipeout);

    if ( StreamExec(theManagers[link],(char*)state ) ) {
        reportError(env,talkerObject,link);
    } else {
       (*env)->ReleaseStringUTFChars(env,statement,state);
    }

    DisconnectStdIO(theManagers[link]);
}

static jchar* convertStringBytes(char* target,char* source,int limit)
{

	return NULL;
}


static jint clearError(JNIEnv* env,jobject talkerObject,jint link)
{
        if ( (*env)->ExceptionOccurred(env) ) return 2;
        
	(*env)->SetIntField(env,talkerObject,Cache->result,0);
	
	return 0;
}

static jint reportError(JNIEnv* env,jobject talkerObject,jint link)
{
        jint code = (jint)GetErrorCode(theManagers[link]);
        
        if ( (*env)->ExceptionOccurred(env) ) {
            (*env)->ExceptionDescribe(env);
            (*env)->ExceptionClear(env);
        }
        
	(*env)->SetIntField(env,talkerObject,Cache->result,code);
        
        if (code != 0 ) {
            const char* errtxt = GetErrorText(theManagers[link]);
            const char* statetxt = GetErrorState(theManagers[link]);
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
        
        jobject 	tracker =  (*env)->GetObjectField(env,talker,Cache->idfield);
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

static int direct_pipeout(void* arg,char* buff,int start,int run)
{
    JNIEnv*  env = NULL;
    jobject target = arg;
    
    (*jvm)->AttachCurrentThread(jvm, (void **)&env, NULL);

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

    (*env)->DeleteGlobalRef(env,arg);

    return 0;
}

int clean_input(StmtMgr mgr,int type, void* arg) {
    JNIEnv*  env = NULL;    
    (*jvm)->AttachCurrentThread(jvm, (void **)&env, NULL);
    
    if ( type == STREAMTYPE && arg != NULL ) {
        arg = PipeDisconnect(mgr,arg);
    }

    (*env)->DeleteGlobalRef(env,arg);
    
    return 0;
}

static StmtMgr allocateWeaver(JNIEnv* env, jstring username,jstring password,jstring connection) {
	jsize passlen = (*env)->GetStringUTFLength(env,password);
	jsize namelen = (*env)->GetStringUTFLength(env,username);
	jsize connlen = (*env)->GetStringUTFLength(env,connection);

	if (( passlen > 63 || namelen > 63 || connlen > 63 ) ||
        ( passlen == 0 || namelen == 0 || connlen == 0 ) )
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

	return CreateWeaverStmtManager(name, pass, conn);
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

