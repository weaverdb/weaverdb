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
#include <math.h>

#include "env/connectionutil.h"

#include "WeaverStmtManager.h"
#include "utils/java.h"
#include "WeaverValueExtractor.h"

#include "driver_weaver_WeaverInitializer.h"
#include "driver_weaver_BaseWeaverConnection.h"

#define BINDNULL  driver_weaver_BaseWeaverConnection_bindNull
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
#define BINDSTREAM  driver_weaver_BaseWeaverConnection_bindStream
#define BINDDIRECT  driver_weaver_BaseWeaverConnection_bindDirect

JavaVM*   jvm;

static          javacache*      Cache;

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

#define GETSTMT(pointer) ((StmtMgr)pointer)

static ConnMgr getConnMgr(JNIEnv* env, jobject talker);
static bool confirmAgent(JNIEnv* env,jobject talker,StmtMgr stmt);
static jint  clearError(JNIEnv* env,jobject talker);
static void  checkError(JNIEnv* env,jobject talker,StmtMgr link);
static jint  reportError(JNIEnv* env,jobject talker,jlong code, const char* text, const char* state);
static int   translateType(jint type);

static void setInputLink(JNIEnv* env, jobject talker, jlong link, jobject boundInput);
static void setOutputLink(JNIEnv* env, jobject talker, jlong link, jobject boundOutput);

static ConnMgr allocateWeaver(JNIEnv* env,jstring username,jstring password,jstring database);

JNIEXPORT void JNICALL Java_driver_weaver_WeaverInitializer_init(JNIEnv *env,jobject talkerObject, jstring jd)
{
	char		datapass[2048];
	memset(datapass,0,2048);
	
	if ( jd != NULL ) {
            int len = (*env)->GetStringUTFLength(env,jd);
            (*env)->GetStringUTFRegion(env,jd,0,len,datapass);
        }

	if ( !initweaverbackend(datapass) ) {
            (*env)->ThrowNew(env,(*env)->FindClass(env,"java/lang/UnsatisfiedLinkError"),"environment not valid, see db log");
            return;
        }
        
        Cache = CreateCache(env);
		
	(*env)->GetJavaVM(env,&jvm);

        SetJVM(jvm,NULL);
}

JNIEXPORT void JNICALL Java_driver_weaver_WeaverInitializer_close(JNIEnv *env,jobject talkerObject)
{
/*  shutdown any threads resources still hanging around */
	if ( prepareforshutdown() ) {
            shuttingdown = true;
            wrapupweaverbackend();
        }
        DropCache(env);
}

JNIEXPORT jlong JNICALL Java_driver_weaver_BaseWeaverConnection_grabConnection
  (JNIEnv * env, jobject talkerObject, jstring theName, jstring thePassword, jstring theConnect)
 {
    if ( shuttingdown ) {            
        (*env)->ThrowNew(env,Cache->exception,"shutting down");
        return 0;
    }

    ConnMgr mgr = allocateWeaver(env,theName,thePassword,theConnect);

    if ( mgr == NULL || !IsValid(mgr) ) {
        DestroyWeaverConnection(mgr);
        mgr = NULL;
        (*env)->ThrowNew(env,Cache->exception,"User not valid");
    }
//  done grabbing
//  logging the id and creating statement space if logon is valid

    return (*env)->ExceptionOccurred(env) ? 0 : (jlong)mgr;
}

JNIEXPORT jlong JNICALL Java_driver_weaver_BaseWeaverConnection_connectSubConnection
  (JNIEnv * env, jobject talkerObject) {
        ConnMgr  cparent = getConnMgr(env,talkerObject);

        if ( shuttingdown ) {
            (*env)->ThrowNew(env,Cache->exception,"shutting down");
            return 0;
        }
        ConnMgr mgr = CreateSubConnection(cparent);

        if ( mgr == NULL ) {
            (*env)->ThrowNew(env,Cache->exception,"User not valid");
        } 
        
        return (jlong)mgr;
}

JNIEXPORT void JNICALL Java_driver_weaver_BaseWeaverConnection_dispose
  (JNIEnv *env, jobject talkerObject, jlong linkid)
{
	if ( (*env)->ExceptionOccurred(env) ) (*env)->ExceptionClear(env);

        ConnMgr conn = getConnMgr(env,talkerObject);
        
        if (linkid != 0L) {
            StmtMgr mgr = (StmtMgr)linkid;
            DestroyWeaverStmtManager(conn, mgr);
        } else {
            DestroyWeaverConnection(conn); 
        }
}

JNIEXPORT jlong JNICALL Java_driver_weaver_BaseWeaverConnection_beginTransaction(JNIEnv *env, jobject talkerObject)
{
    ConnMgr ref = getConnMgr(env,talkerObject);
//	mark the beginning of the transaction
    if ( Begin(ref) ) {
        checkError(env, talkerObject,NULL);
        return 0;
    } else {
        clearError(env,talkerObject);
        return GetTransactionId(ref);
    }
}

JNIEXPORT jlong JNICALL Java_driver_weaver_BaseWeaverConnection_prepareStatement(JNIEnv *env, jobject talkerObject, jstring statement)
{
	const char* 	pass_stmt;	

        ConnMgr conn = getConnMgr(env,talkerObject);

        if ( statement == NULL ) {
            (*env)->ThrowNew(env,Cache->exception,"no statement");
            return 0;
        }
        
        StmtMgr base = CreateWeaverStmtManager(conn);
        pass_stmt = (*env)->GetStringUTFChars(env,statement,NULL);
        ParseStatement(conn, base,pass_stmt);
        (*env)->ReleaseStringUTFChars(env,statement,pass_stmt);
	
        return (jlong)base;
}

void setInputLink(JNIEnv *env, jobject talkerObject, jlong linkid, jobject boundIn)
{
 	char        var[64];
        jsize       varsize = 0;
        Input        bound;
        short       type = translateType((*env)->CallIntMethod(env, boundIn, Cache->itypeid));

        ConnMgr        conn = getConnMgr(env, talkerObject);
        StmtMgr        base = GETSTMT(linkid);
        
        jstring theVar = (*env)->GetObjectField(env, boundIn, Cache->iname);
        jobject value = (*env)->GetObjectField(env, boundIn, Cache->ivalue);
    
	varsize = (*env)->GetStringLength(env,theVar);	
	if ( varsize > 63 ) {
            if (!(*env)->ExceptionOccurred(env) ) 
                (*env)->ThrowNew(env,Cache->exception,"bind name too long");
            return;
	}
	(*env)->GetStringUTFRegion(env,theVar,0, varsize, var); 		
	var[varsize] = 0;
        
        if ( (*env)->IsSameObject(env,value,NULL) ) {
            bound = SetInputValue(conn, base,var,type,NULL,0);
        } else if ( type == STREAMTYPE ) {
            Pipe pipe = PipeConnect(conn,base,boundIn,direct_pipein);
            bound = SetInputValue(conn, base,var,type,pipe, -1); /*  -1 length means use the maxlength for the type */
            SetUserspace(InputToBound(bound),pipe);
        } else {
            PassInValue(env,conn, base,var,type,value);
        }
//  report errors
	checkError(env,talkerObject,base);
}

void setOutputLink(JNIEnv *env, jobject talkerObject, jlong linkid, jobject boundOut)
{
    Output bound = NULL;
    short type = translateType((*env)->CallIntMethod(env, boundOut, Cache->otypeid));
    //	get proper agent
    jint index = (*env)->GetIntField(env, boundOut, Cache->oindex);

    ConnMgr        conn = getConnMgr(env, talkerObject);
    StmtMgr base = GETSTMT(linkid);
    
// check for valid link
        if ( type == STREAMTYPE ) {
            Pipe pipe = PipeConnect(conn,base,boundOut,direct_pipeout);
            bound = SetOutputValue(conn,base,index,type,pipe,-1);
            SetUserspace(OutputToBound(bound),pipe);
        } else {
            bound = OutputLink(conn, base,index,type);
            SetUserspace(OutputToBound(bound),boundOut);
        }
//  report errors
	checkError(env,talkerObject,base);
}


JNIEXPORT jlong JNICALL Java_driver_weaver_BaseWeaverConnection_executeStatement
  (JNIEnv *env, jobject talkerObject, jlong linkid, jobjectArray inputs)
{
    int x;
//	get proper agent	
    ConnMgr conn = getConnMgr(env, talkerObject);
    StmtMgr ref = GETSTMT(linkid);
             
     jsize inSize = (*env)->GetArrayLength(env, inputs);
     for (x=0;x<inSize;x++) {
        jobject instep = (*env)->GetObjectArrayElement(env, inputs, x);
        setInputLink(env, talkerObject, linkid, instep);
     }     
// exec
    short result = Exec(conn, ref);
    
    DisconnectPipes(conn, ref);

    if ( result ) {
// report errors
        checkError(env,talkerObject,ref);
        return 0;
    } else {
        clearError(env,talkerObject);
        return Count(ref);
    }
}

JNIEXPORT jboolean JNICALL Java_driver_weaver_BaseWeaverConnection_fetchResults
  (JNIEnv *env, jobject talkerObject,jlong linkid, jobjectArray outputs)
{
    int x;
//	get proper agent	
    ConnMgr conn = getConnMgr(env, talkerObject);
    StmtMgr ref = GETSTMT(linkid);

     jsize inSize = (*env)->GetArrayLength(env, outputs);
     for (x=0;x<inSize;x++) {
        jobject step = (*env)->GetObjectArrayElement(env, outputs, x);
        setOutputLink(env, talkerObject, linkid, step);
     }     

    short result = Fetch(conn, ref);

//	fetch        
//  pass results to java if there are no errors
    if ( !result ) {
        PassResults(env, conn, ref);
    } else {
// report errors
        if ( GetErrorCode(conn, ref) != 0 ) {
            if ( GetErrorCode(conn, ref) == 102 ) {
                (*env)->ThrowNew(env,Cache->truncation,GetErrorText(conn, ref));
            } else {
                checkError(env,talkerObject,ref);
            }
        }
    }

    DisconnectPipes(conn, ref);
    return (!result) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_driver_weaver_BaseWeaverConnection_cancelTransaction
  (JNIEnv *env, jobject talkerObject)
{
    //	get proper agent	
    ConnMgr ref = getConnMgr(env, talkerObject);

    Cancel(ref);
}

JNIEXPORT void JNICALL Java_driver_weaver_BaseWeaverConnection_prepareTransaction(JNIEnv *env, jobject talkerObject)
{
    //	get proper agent	
        ConnMgr base = getConnMgr(env,talkerObject);
	
// prepase statement for commit
	if ( Prepare(base) ) {
// report errors
	    checkError(env,talkerObject,NULL);
	}
}


JNIEXPORT void JNICALL Java_driver_weaver_BaseWeaverConnection_commitTransaction
  (JNIEnv *env, jobject talkerObject)
{
    //	get proper agent	
        ConnMgr base = getConnMgr(env,talkerObject);
        
//  commit the previous statement
	if ( Commit(base) ) {
            checkError(env,talkerObject,NULL);
	}
}

JNIEXPORT void JNICALL Java_driver_weaver_BaseWeaverConnection_abortTransaction
  (JNIEnv *env, jobject talkerObject)
{
    //	get proper agent	
        ConnMgr base = getConnMgr(env,talkerObject);

//  commit the previous statement
	if ( Rollback(base) ) {
		checkError(env,talkerObject,NULL);
	}
}


JNIEXPORT void JNICALL Java_driver_weaver_BaseWeaverConnection_beginProcedure
  (JNIEnv *env, jobject talkerObject)
{
    //	get proper agent	
        ConnMgr base = getConnMgr(env,talkerObject);

//  commit the previous statement
	if ( BeginProcedure(base) ) {
// report errors
		checkError(env,talkerObject,NULL);
	}
}


JNIEXPORT void JNICALL Java_driver_weaver_BaseWeaverConnection_endProcedure
  (JNIEnv *env, jobject talkerObject)
{
    //	get proper agent	
        ConnMgr base = getConnMgr(env,talkerObject);
//  commit the previous statement
	if ( EndProcedure(base) ) {
// report errors
		checkError(env,talkerObject,NULL);
	}
}

JNIEXPORT jlong JNICALL Java_driver_weaver_BaseWeaverConnection_getCommandId
  (JNIEnv *env, jobject talkerObject, jlong link)
{
    //	get proper agent	
        StmtMgr base = GETSTMT(link);

	return GetCommandId(base);
}

JNIEXPORT jlong JNICALL Java_driver_weaver_BaseWeaverConnection_getTransactionId
  (JNIEnv *env, jobject talkerObject)
{
    //	get proper agent	
    ConnMgr base = getConnMgr(env,talkerObject);

    return GetTransactionId(base);
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
    ConnMgr base = getConnMgr(env,talkerObject);
	
    state = (*env)->GetStringUTFChars(env,statement,&copy);
                
    ConnectStdIO(base,&commenv,pipein,pipeout);

    if ( StreamExec(base,(char*)state ) ) {
        checkError(env,talkerObject,NULL);
    } else {
       (*env)->ReleaseStringUTFChars(env,statement,state);
    }

    DisconnectStdIO(base);
}

static ConnMgr getConnMgr(JNIEnv* env, jobject talker) {
    ConnMgr ref = NULL;
    if ( shuttingdown ) {
        ref = NULL;
    } else {
        jlong pointer = (*env)->GetLongField(env,talker,Cache->nativePointer);
        ref = (ConnMgr)pointer;

        if ( !IsValid(ref) ) {
            if (!(*env)->ExceptionOccurred(env) ) {
                (*env)->ThrowNew(env,Cache->exception,"agent not valid");   
            }
            ref = NULL;
        }
    }
    return ref;
}

static jint clearError(JNIEnv* env,jobject talkerObject)
{
        if ( (*env)->ExceptionOccurred(env) ) return 2;
        
	(*env)->SetIntField(env,talkerObject,Cache->result,0);
	
	return 0;
}

static void checkError(JNIEnv* env,jobject talkerObject,StmtMgr base)
{
        const char* errtxt;
        const char* statetxt;
        jlong code = ReportError(getConnMgr(env, talkerObject), base, &errtxt, &statetxt);
        reportError(env, talkerObject, code, errtxt, statetxt);
}

static jint reportError(JNIEnv* env,jobject talkerObject,jlong code, const char* errtxt, const char* statetxt)
{
        if ( (*env)->ExceptionOccurred(env) ) {
            (*env)->ExceptionDescribe(env);
            (*env)->ExceptionClear(env);
        }
        
	(*env)->SetIntField(env,talkerObject,Cache->result,code);
        
        if (code != 0 ) {
            char combo[255];

            if ( !errtxt ) errtxt = "no error text";
            if ( !statetxt ) statetxt = "NOSTATE";

            jstring et = (*env)->NewStringUTF(env,errtxt);
            jstring st = (*env)->NewStringUTF(env,statetxt);

            (*env)->SetObjectField(env,talkerObject,Cache->eText,et);
            (*env)->SetObjectField(env,talkerObject,Cache->eState,st);

            snprintf(combo,255,"%s: %s -- err: %d",statetxt,errtxt,(int)code);
            (*env)->ThrowNew(env,Cache->exception,combo);
        }
	
	return 0;
}

static bool confirmAgent(JNIEnv* env,jobject talker,StmtMgr stmt) {
    if ( stmt == NULL ) return false;
    return true;
}


static int direct_pipeout(void* arg,char* buff,int start,int run)
{
    JNIEnv*  env = NULL;
    jobject target = arg;
    
    (*jvm)->AttachCurrentThread(jvm, (void **)&env, NULL);

    target = (*env)->NewLocalRef(env,target);
    if ((*env)->IsSameObject(env,target,NULL)) {
        return PIPING_ERROR;
    }

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
    
    target = (*env)->NewLocalRef(env,target);
    if ((*env)->IsSameObject(env,target,NULL)) {
        return PIPING_ERROR;
    }

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

    target = (*env)->NewLocalRef(env,target);
    
    if ((*env)->IsSameObject(env,target,NULL)) {
        return PIPING_ERROR;
    }
    
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

    target = (*env)->NewLocalRef(env,target);

    if ((*env)->IsSameObject(env,target,NULL)) {
        return PIPING_ERROR;
    }
    
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

static ConnMgr allocateWeaver(JNIEnv* env, jstring username,jstring password,jstring connection) {
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

	return CreateWeaverConnection(name, pass, conn);
         
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

