/*-------------------------------------------------------------------------
 *
 * BaseWeaverConnection.cc
 *	  Base code to connect weaver to java
 *
 * Copyright (c) 2000-2024, Myron Scott  <myron@weaverdb.org>
 *
 *
 *
 *-------------------------------------------------------------------------
 */

#include <jni.h>

#include <sys/stat.h>
#include <strings.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <math.h>

#include "connectionutil.h"

#include "WeaverStmtManager.h"
#include "WeaverValueExtractor.h"

#include "org_weaverdb_WeaverInitializer.h"
#include "org_weaverdb_BaseWeaverConnection.h"

#define BINDNULL  org_weaverdb_BaseWeaverConnection_bindNull
#define BINDSHORT  org_weaverdb_BaseWeaverConnection_bindShort
#define BINDINTEGER  org_weaverdb_BaseWeaverConnection_bindInteger
#define BINDSTRING  org_weaverdb_BaseWeaverConnection_bindString
#define BINDDOUBLE  org_weaverdb_BaseWeaverConnection_bindDouble
#define BINDFLOAT  org_weaverdb_BaseWeaverConnection_bindFloat
#define BINDCHARACTER  org_weaverdb_BaseWeaverConnection_bindCharacter
#define BINDBOOLEAN  org_weaverdb_BaseWeaverConnection_bindBoolean
#define BINDBINARY  org_weaverdb_BaseWeaverConnection_bindBinary
#define BINDBLOB  org_weaverdb_BaseWeaverConnection_bindBLOB
#define BINDDATE  org_weaverdb_BaseWeaverConnection_bindDate
#define BINDLONG  org_weaverdb_BaseWeaverConnection_bindLong
#define BINDFUNCTION  org_weaverdb_BaseWeaverConnection_bindFunction
#define BINDSLOT org_weaverdb_BaseWeaverConnection_bindSlot
#define BINDJAVA  org_weaverdb_BaseWeaverConnection_bindJava
#define BINDTEXT  org_weaverdb_BaseWeaverConnection_bindText
#define BINDSTREAM  org_weaverdb_BaseWeaverConnection_bindStream
#define BINDDIRECT  org_weaverdb_BaseWeaverConnection_bindDirect

#define JAVA_ERROR -99

static          javacache*      Cache;

static bool                     debug = false;
static bool                     shuttingdown = false;

extern int                      DebugLvl;

typedef struct commargs {
    JNIEnv*  env;
    jobject  target;
    int bindType;
    int linkType;
} CommArgs;

extern void SetJVM(JavaVM* java, const char* loader);

static int transferin(void* arg,int type, void* buff,int run);
static int transferout(void* arg,int type, void* buff,int run);
static int pipeout(void* args,int type,void* buff,int run);
static int pipein(void* args,int type, void* buff,int run);
static int direct_pipeout(void* args,int type,void* buff,int run);
static int direct_pipein(void* args,int type,void* buff,int run);

#define GETSTMT(pointer) ((StmtMgr)pointer)

static ConnMgr getConnMgr(JNIEnv* env, jobject talker);
static bool confirmAgent(JNIEnv* env,jobject talker,StmtMgr stmt);
static jlong  checkError(JNIEnv* env,jobject talker,StmtMgr link);
static jlong  reportErrorToJava(JNIEnv* env,jobject talker,jlong code, const char* text, const char* state);
static int   translateType(jint type);

static void setInputLink(JNIEnv* env, jobject talkerObject, jlong linkid, CommArgs* userspace);
static void setOutputLink(JNIEnv* env, jobject talkerObject, jlong linkid, CommArgs* userspace);

static ConnMgr allocateWeaver(JNIEnv* env,jstring username,jstring password,jstring database);

JNIEXPORT void JNICALL Java_org_weaverdb_WeaverInitializer_init(JNIEnv *env,jobject talkerObject, jstring jd)
{
    JavaVM*   jvm;
    const char*    variables;

    shuttingdown = false;

    if ( (*env)->IsSameObject(env, jd, NULL) ) {
        (*env)->ThrowNew(env,(*env)->FindClass(env,"java/lang/UnsatisfiedLinkError"),"environment setup is not valid");
        return;
    }

    variables = (*env)->GetStringUTFChars(env,jd,NULL);

    bool valid = initweaverbackend(variables);

    (*env)->ReleaseStringUTFChars(env, jd, variables);
    if ( !valid ) {
        (*env)->ThrowNew(env,(*env)->FindClass(env,"java/lang/UnsatisfiedLinkError"),"environment not valid, see db log");
        return;
    }

    Cache = CreateCache(env);

    (*env)->GetJavaVM(env,&jvm);

    SetJVM(jvm,"org/weaverdb/WeaverObjectLoader");
}

JNIEXPORT void JNICALL Java_org_weaverdb_WeaverInitializer_close(JNIEnv *env,jobject talkerObject)
{
/*  shutdown any threads resources still hanging around */
	if ( prepareforshutdown() ) {
            shuttingdown = true;
            wrapupweaverbackend();
        }
        DropCache(env);
}

JNIEXPORT jlong JNICALL Java_org_weaverdb_BaseWeaverConnection_grabConnection
  (JNIEnv * env, jobject talkerObject, jstring theName, jstring thePassword, jstring theConnect)
 {
    if ( shuttingdown ) {            
        (*env)->ThrowNew(env,(*env)->FindClass(env,"org/weaverdb/ExecutionException"),"shutting down");
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

JNIEXPORT jlong JNICALL Java_org_weaverdb_BaseWeaverConnection_connectSubConnection
  (JNIEnv * env, jobject talkerObject) {
        ConnMgr  cparent = getConnMgr(env,talkerObject);

        if ( shuttingdown ) {
            (*env)->ThrowNew(env,(*env)->FindClass(env,"org/weaverdb/ExecutionException"),"shutting down");
            return 0;
        }
        ConnMgr mgr = CreateSubConnection(cparent);

        if ( mgr == NULL ) {
            (*env)->ThrowNew(env,Cache->exception,"User not valid");
        } 
        
        return (jlong)mgr;
}

JNIEXPORT void JNICALL Java_org_weaverdb_BaseWeaverConnection_dispose
  (JNIEnv *env, jobject talkerObject, jlong linkid)
{
	if ( (*env)->ExceptionOccurred(env) ) (*env)->ExceptionClear(env);

        ConnMgr conn = getConnMgr(env,talkerObject);
        
        if (linkid != 0L) {
            StmtMgr mgr = GETSTMT(linkid);
            DestroyWeaverStmtManager(conn, mgr);
        } else {
            DestroyWeaverConnection(conn); 
        }
}

JNIEXPORT void JNICALL Java_org_weaverdb_BaseWeaverConnection_disposeConnection
  (JNIEnv * env, jclass clazz, jlong connid) 
{
	if ( (*env)->ExceptionOccurred(env) ) (*env)->ExceptionClear(env);
        
        if (connid != 0L) {
            DestroyWeaverConnection((ConnMgr)connid); 
        }
}

JNIEXPORT jlong JNICALL Java_org_weaverdb_BaseWeaverConnection_beginTransaction(JNIEnv *env, jobject talkerObject)
{
    ConnMgr ref = getConnMgr(env,talkerObject);
//	mark the beginning of the transaction
    if ( Begin(ref) ) {
        checkError(env, talkerObject,NULL);
        return 0;
    } else {
        return GetTransactionId(ref);
    }
}

JNIEXPORT jlong JNICALL Java_org_weaverdb_BaseWeaverConnection_prepareStatement(JNIEnv *env, jobject talkerObject, jstring statement)
{
	const char* 	pass_stmt;	

        ConnMgr conn = getConnMgr(env,talkerObject);

        if (conn == NULL) {
            return 0;
        }

        if ( statement == NULL ) {
            (*env)->ThrowNew(env,Cache->exception,"no statement");
            return 0;
        }

        StmtMgr base = CreateWeaverStmtManager(conn);

        if (base != NULL) {
            pass_stmt = (*env)->GetStringUTFChars(env,statement,NULL);
            short result = ParseStatement(conn, base,pass_stmt);
            (*env)->ReleaseStringUTFChars(env,statement,pass_stmt);
            if (result) {
                checkError(env, talkerObject, base);
                DestroyWeaverStmtManager(conn, base);
                base = NULL;
            }
        } else {
            (*env)->ThrowNew(env,Cache->exception,"statement space exhusted");
        }
        return (jlong)base;
}

void setInputLink(JNIEnv* env, jobject talkerObject, jlong linkid, CommArgs* userspace)
{
 	const char*        varname;

        ConnMgr        conn = getConnMgr(env, talkerObject);
        StmtMgr        base = GETSTMT(linkid);
//  don't need a local ref
//  root object is held
        jstring theVar = (*env)->GetObjectField(env, userspace->target, Cache->iname);

        if ((*env)->IsSameObject(env, theVar, NULL)) {
            if (!(*env)->ExceptionOccurred(env) ) 
                (*env)->ThrowNew(env,Cache->exception,"bind name is null");
            return;
        } else if ((*env)->GetStringLength(env,theVar) > 63) {
            if (!(*env)->ExceptionOccurred(env) ) 
                (*env)->ThrowNew(env,Cache->exception,"bind name is too long");
            return;
        }

	varname = (*env)->GetStringUTFChars(env,theVar,NULL); 		
        
        LinkInput(conn,base,varname,userspace->linkType,userspace,userspace->linkType == STREAMTYPE ? direct_pipein : transferin);

        (*env)->ReleaseStringUTFChars(env,theVar,varname); 
//  report errors
	checkError(env,talkerObject,base);
}

void setOutputLink(JNIEnv* env, jobject talkerObject, jlong linkid, CommArgs* userspace)
{
        //	get proper agent
    jint index = (*env)->GetIntField(env, userspace->target, Cache->oindex);

    ConnMgr        conn = getConnMgr(env, talkerObject);
    StmtMgr base = GETSTMT(linkid);
    
    LinkOutput(conn,base,index,userspace->linkType,userspace,userspace->linkType == STREAMTYPE ? direct_pipeout : transferout);

//  report errors
    checkError(env,talkerObject,base);
}


JNIEXPORT jlong JNICALL Java_org_weaverdb_BaseWeaverConnection_executeStatement
  (JNIEnv *env, jobject talkerObject, jlong linkid, jobjectArray inputs)
{
    int x;
//	get proper agent	
    ConnMgr conn = getConnMgr(env, talkerObject);
    StmtMgr ref = GETSTMT(linkid);
    
     jsize inSize = (*env)->GetArrayLength(env, inputs);
     CommArgs callData[inSize];
     for (x=0;x<inSize;x++) {
        jobject instep = (*env)->GetObjectArrayElement(env, inputs, x);
        callData[x].env = env;
        callData[x].target = instep;
        callData[x].bindType = (*env)->CallIntMethod(env, instep, Cache->itypeid);
        callData[x].linkType = translateType(callData[x].bindType);
        setInputLink(env, talkerObject, linkid, &callData[x]);
     }     
// exec
    if ( Exec(conn, ref) ) {
// report errors
        checkError(env,talkerObject,ref);
        return 0;
    } else {
        return Count(ref);
    }
}

JNIEXPORT jboolean JNICALL Java_org_weaverdb_BaseWeaverConnection_fetchResults
  (JNIEnv *env, jobject talkerObject,jlong linkid, jobjectArray outputs)
{
    int x;
//	get proper agent	
    ConnMgr conn = getConnMgr(env, talkerObject);
    StmtMgr ref = GETSTMT(linkid);

     jsize inSize = (*env)->GetArrayLength(env, outputs);
     CommArgs callData[inSize];
     for (x=0;x<inSize;x++) {
        jobject instep = (*env)->GetObjectArrayElement(env, outputs, x);
        callData[x].env = env;
        callData[x].target = instep;
        callData[x].bindType = (*env)->CallIntMethod(env, instep, Cache->otypeid);
        callData[x].linkType = translateType(callData[x].bindType);
        setOutputLink(env, talkerObject, linkid, &callData[x]);
    }
//	fetch        
//  pass results to java if there are no errors
    if ( Fetch(conn, ref) ) {
// report errors
        checkError(env,talkerObject,ref);
        return JNI_FALSE;
    } else {
        return JNI_TRUE;
    }
}

JNIEXPORT void JNICALL Java_org_weaverdb_BaseWeaverConnection_cancelTransaction
  (JNIEnv *env, jobject talkerObject)
{
    //	get proper agent	
    ConnMgr ref = getConnMgr(env, talkerObject);

    Cancel(ref);
}

JNIEXPORT void JNICALL Java_org_weaverdb_BaseWeaverConnection_prepareTransaction(JNIEnv *env, jobject talkerObject)
{
    //	get proper agent	
        ConnMgr base = getConnMgr(env,talkerObject);
	
// prepase statement for commit
	if ( Prepare(base) ) {
// report errors
	    checkError(env,talkerObject,NULL);
	}
}


JNIEXPORT void JNICALL Java_org_weaverdb_BaseWeaverConnection_commitTransaction
  (JNIEnv *env, jobject talkerObject)
{
    //	get proper agent	
        ConnMgr base = getConnMgr(env,talkerObject);
        
//  commit the previous statement
	if ( Commit(base) ) {
            checkError(env,talkerObject,NULL);
	}
}

JNIEXPORT void JNICALL Java_org_weaverdb_BaseWeaverConnection_abortTransaction
  (JNIEnv *env, jobject talkerObject)
{
    //	get proper agent	
        ConnMgr base = getConnMgr(env,talkerObject);

//  commit the previous statement
	if ( Rollback(base) ) {
		checkError(env,talkerObject,NULL);
	}
}


JNIEXPORT void JNICALL Java_org_weaverdb_BaseWeaverConnection_beginProcedure
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


JNIEXPORT void JNICALL Java_org_weaverdb_BaseWeaverConnection_endProcedure
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

JNIEXPORT jlong JNICALL Java_org_weaverdb_BaseWeaverConnection_getCommandId
  (JNIEnv *env, jobject talkerObject, jlong link)
{
    //	get proper agent	
        StmtMgr base = GETSTMT(link);

	return GetCommandId(base);
}

JNIEXPORT jlong JNICALL Java_org_weaverdb_BaseWeaverConnection_getTransactionId
  (JNIEnv *env, jobject talkerObject)
{
    //	get proper agent	
    ConnMgr base = getConnMgr(env,talkerObject);

    return GetTransactionId(base);
}

JNIEXPORT void JNICALL Java_org_weaverdb_BaseWeaverConnection_streamExec
  (JNIEnv * env, jobject talkerObject, jstring statement)
{	
    const char*  state;
    CommArgs     commenv;
    
    commenv.env = env;
    commenv.target = talkerObject;
    //	get proper agent	
    ConnMgr base = getConnMgr(env,talkerObject);
	
    state = (*env)->GetStringUTFChars(env,statement,NULL);
                
    ConnectStdIO(base,&commenv,pipein,pipeout);

    bool valid = StreamExec(base,(char*)state); 

    (*env)->ReleaseStringUTFChars(env,statement,state);

    if (!valid) {
        checkError(env,talkerObject,NULL);
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

static jlong checkError(JNIEnv* env,jobject talkerObject,StmtMgr base)
{
        const char* errtxt;
        const char* statetxt;
        jlong code = ReportError(getConnMgr(env, talkerObject), base, &errtxt, &statetxt);
        return reportErrorToJava(env, talkerObject, code, errtxt, statetxt);
}

static jlong reportErrorToJava(JNIEnv* env,jobject talkerObject,jlong code, const char* errtxt, const char* statetxt)
{
    if (code != 0 ) {
        jthrowable existing = (*env)->ExceptionOccurred(env);
        char combo[255];

        if ( !errtxt ) errtxt = "no error text";
        if ( !statetxt ) statetxt = "NOSTATE";

        snprintf(combo,255,"%s: %s -- err: %d",statetxt,errtxt,(int)code);
        if ((*env)->IsSameObject(env, existing, NULL)) {
            (*env)->ThrowNew(env,Cache->exception,combo);
        } else {
            jthrowable throw = (*env)->NewObject(env,Cache->exception,Cache->ecstor, (*env)->NewStringUTF(env, combo));
            (*env)->CallVoidMethod(env,throw,Cache->suppressed,existing);
            (*env)->ExceptionClear(env);
            (*env)->Throw(env,throw);
        }
    }
	
    return code;
}

static bool confirmAgent(JNIEnv* env,jobject talker,StmtMgr stmt) {
    if ( stmt == NULL ) return false;
    return true;
}

static int transferin(void* arg,int type, void* buff,int run)
{
    CommArgs* comm = arg;
    JNIEnv*  env = comm->env;
    jobject target = comm->target;

    if ((*env)->IsSameObject(env,target,NULL)) {
        return PIPING_ERROR;
    }
    jobject value = (*env)->GetObjectField(env, target, Cache->ivalue);

    if ((*env)->IsSameObject(env,value,NULL)) {
        return NULL_VALUE;
    }

    int checkTrunc = PassInValue(env,comm->bindType,comm->linkType,type,value,buff, run);
    if (checkTrunc == TRUNCATION_VALUE) {
        (*env)->ThrowNew(env,Cache->truncation,"binary truncation");
    }

    return checkTrunc;
}

static int transferout(void* arg,int type, void* buff,int run)
{
    CommArgs* comm = arg;
    JNIEnv*  env = comm->env;
    jobject target = comm->target;
                
    if ((*env)->IsSameObject(env,target,NULL)) {
        return NULL_VALUE;
    }

    return PassOutValue(env,comm->bindType,comm->linkType,type,target, buff, run);
}

static int direct_pipeout(void* arg,int type, void* buff,int run)
{
    CommArgs* comm = arg;
    JNIEnv*  env = comm->env;
    jobject target = comm->target;
    
    if ((*env)->IsSameObject(env,target,NULL)) {
        return PIPING_ERROR;
    }

    if (type == METANAMETYPE) {
        return PassOutValue(env,comm->bindType,comm->linkType,type,target, buff, run);
    } else if (buff == NULL) {
        return (*env)->CallIntMethod(env,target,Cache->pipeout,NULL);
    } else {
        jobject jb = (*env)->NewDirectByteBuffer(env,buff,run);

        if ( jb != NULL ) {
            int len = (*env)->CallIntMethod(env,target,Cache->pipeout,jb);
            if ( (*env)->ExceptionCheck(env) ) {
                return PIPING_ERROR;
            } else {
                (*env)->DeleteLocalRef(env, jb);
                return len;
            }
        } else {
            if ( (*env)->ExceptionCheck(env) ) {
                return PIPING_ERROR;
            }
            return -1;
        }
    }
}

static int direct_pipein(void* arg,int type, void* buff,int run)
{
    CommArgs* comm = arg;
    JNIEnv*  env = comm->env;
    jobject target = comm->target;
        
    if ((*env)->IsSameObject(env,target,NULL)) {
        return PIPING_ERROR;
    }

    if (buff == NULL) {
    	return (*env)->CallIntMethod(env,target,Cache->pipein,NULL);
    }

    jobject jb = (*env)->NewDirectByteBuffer(env,buff,run);

    if ( jb != NULL ) {
    	jint count = (*env)->CallIntMethod(env,target,Cache->pipein,jb);

        if ( (*env)->ExceptionCheck(env) ) {
            return PIPING_ERROR;
        } else {
            (*env)->DeleteLocalRef(env, jb);
        }
        
    	return count;
    } else {
        if ( (*env)->ExceptionCheck(env) ) {
            return PIPING_ERROR;
        }
        return -1;
    }
}

static int pipeout(void* args,int type, void* buff,int run)
{
    CommArgs*  commargs = args;
    JNIEnv*    env = commargs->env;
    jobject    target = commargs->target;
    
    if ((*env)->IsSameObject(env,target,NULL)) {
        return PIPING_ERROR;
    }

    if (buff == NULL) {
    	return (*env)->CallIntMethod(env,target,Cache->infoout,NULL);
    }

    jbyteArray jb = (*env)->NewByteArray(env,run);

    if ( jb != NULL ) {
    	(*env)->SetByteArrayRegion(env,jb,0,run,(jbyte*)(buff));
    	int len = (*env)->CallIntMethod(env,target,Cache->infoout,jb);
        if ( (*env)->ExceptionCheck(env) ) {
            return PIPING_ERROR;
        } else {
            (*env)->DeleteLocalRef(env, jb);
            return len;
        }
    } else {
        if ( (*env)->ExceptionCheck(env) ) {
            return PIPING_ERROR;
        }
        return -1;
    }
}

static int pipein(void* args,int type, void* buff,int run)
{
    CommArgs*  commargs = args;
    JNIEnv*    env = commargs->env;
    jobject    target = commargs->target;

    if ((*env)->IsSameObject(env,target,NULL)) {
        return PIPING_ERROR;
    }
    
    jbyteArray jb = (*env)->NewByteArray(env,run);

    if ( jb != NULL ) {
    	jint count = (*env)->CallIntMethod(env,target,Cache->infoin,jb);
        if ( (*env)->ExceptionCheck(env) ) {
            return PIPING_ERROR;
        }
        if ( count > 0 ) {
            (*env)->GetByteArrayRegion(env,jb,0,count,(jbyte*)(buff));
        }
        (*env)->DeleteLocalRef(env, jb);
    	return count;
    } else {
        if ( (*env)->ExceptionCheck(env) ) {
            return PIPING_ERROR;
        }
        return -1;
    }
}

static ConnMgr allocateWeaver(JNIEnv* env, jstring username,jstring password,jstring connection) {
	char pass[256];
	char name[256];
	char conn[256];
        const char* errMsg = NULL;

        if (!(*env)->IsSameObject(env, username, NULL) && !(*env)->IsSameObject(env, password, NULL)) {
            jsize passlen = (*env)->GetStringUTFLength(env,password);
            jsize namelen = (*env)->GetStringUTFLength(env,username);
            if (passlen >= 0 && passlen < 255 && namelen >= 0 && namelen < 255) {
                (*env)->GetStringUTFRegion(env,password,0,passlen,pass);
                (*env)->GetStringUTFRegion(env,username,0,namelen,name);
                pass[passlen] = '\0';
                name[namelen] = '\0';
            } else {
                errMsg = "Invalid username or password - too many characters";
            }
        } else {
            name[0] = '\0';
            pass[0] = '\0';
        }

        if (!(*env)->IsSameObject(env, connection, NULL)) {
            jsize connlen = (*env)->GetStringUTFLength(env,connection);
            if (connlen >= 0 && connlen < 255) {
                (*env)->GetStringUTFRegion(env,connection,0,connlen,conn);
                conn[connlen] = '\0';
            } else {
                errMsg = "Invalid database - too many characters";
            }
        }

	if (errMsg != NULL) {
            if (!(*env)->ExceptionOccurred(env) ) {
                (*env)->ThrowNew(env,Cache->exception,errMsg);
            }
            return NULL;
	}

	return CreateWeaverConnection(name, pass, conn);
}

static int translateType(jint type) {
      switch(type)
        {
            case BINDSHORT:
                return INT2TYPE;
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
            case BINDFLOAT:
                return  FLOATTYPE;
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

