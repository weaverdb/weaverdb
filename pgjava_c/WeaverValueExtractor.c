#include <jni.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <strings.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <pthread.h>
#include <math.h>

#include "WeaverStmtManager.h"
#include "WeaverValueExtractor.h"
#include "env/WeaverInterface.h"


static javacache CachedClasses;

static javacache*  Cache = &CachedClasses;

static void ExtractIntValue(JNIEnv* env, StmtMgr mgr,char* name, short type,  jobject target);
static void ExtractStringValue(JNIEnv* env, StmtMgr mgr,char* name, short type, jobject target);
static void ExtractCharacterValue(JNIEnv* env,StmtMgr mgr,char* name, short type, jobject target);
static void ExtractBooleanValue(JNIEnv* env,StmtMgr mgr,char* name, short type, jobject target);
static void ExtractDoubleValue(JNIEnv* env,StmtMgr mgr,char* name, short type, jobject target);
static void ExtractLongValue(JNIEnv* env,StmtMgr mgr,char* name, short type, jobject target);
static void ExtractDateValue(JNIEnv* env,StmtMgr mgr, char* name, short type, jobject target);
static void ExtractByteArrayValue(JNIEnv* env,StmtMgr mgr,char* name, short type,jobject target);
static void ExtractBytes(JNIEnv* env, StmtMgr mgr,char* name, short type,jbyteArray target);

javacache*  CreateCache(JNIEnv* env) {
	/* exceptions  */
        CachedClasses.exception = (*env)->NewGlobalRef(env,(*env)->FindClass(env,"java/sql/SQLException"));
        CachedClasses.truncation = (*env)->NewGlobalRef(env,(*env)->FindClass(env,"driver/weaver/BinaryTruncation"));
        /*  boundary objects */
        CachedClasses.talker = (*env)->NewGlobalRef(env,(*env)->FindClass(env,"driver/weaver/BaseWeaverConnection"));
        CachedClasses.linkid = (*env)->NewGlobalRef(env,(*env)->FindClass(env,"driver/weaver/LinkID"));
        CachedClasses.boundin = (*env)->NewGlobalRef(env,(*env)->FindClass(env,"driver/weaver/BoundInput"));
        CachedClasses.boundout = (*env)->NewGlobalRef(env,(*env)->FindClass(env,"driver/weaver/BoundOutput"));

        /*  field ids  */
        CachedClasses.idfield = (*env)->GetFieldID(env,CachedClasses.talker,"id","Ldriver/weaver/LinkID;");
        CachedClasses.tracker = (*env)->GetFieldID(env,CachedClasses.linkid,"linkNumber","I");
        CachedClasses.result = (*env)->GetFieldID(env,CachedClasses.talker, "resultField","I");
	CachedClasses.eText =  (*env)->GetFieldID(env,CachedClasses.talker,"errorText","Ljava/lang/String;");
	CachedClasses.eState =  (*env)->GetFieldID(env,CachedClasses.talker,"state","Ljava/lang/String;");
	CachedClasses.value = (*env)->GetFieldID(env,CachedClasses.boundout, "value","Ljava/lang/Object;");
	CachedClasses.nullfield = (*env)->GetFieldID(env,CachedClasses.boundout,"isnull","Z");
        
        CachedClasses.pipein = (*env)->GetMethodID(env,CachedClasses.boundin,"pipeIn","(Ljava/nio/ByteBuffer;)I");
        CachedClasses.pipeout = (*env)->GetMethodID(env,CachedClasses.boundout,"pipeOut","(Ljava/nio/ByteBuffer;)V");
        CachedClasses.infoin = (*env)->GetMethodID(env,CachedClasses.talker,"pipeIn","([B)I");
        CachedClasses.infoout = (*env)->GetMethodID(env,CachedClasses.talker,"pipeOut","([B)V");
        CachedClasses.typeid = (*env)->GetMethodID(env,CachedClasses.boundout,"getTypeId","()I");
        /*  output types */
        CachedClasses.doubletype = (*env)->NewGlobalRef(env,(*env)->FindClass(env,"java/lang/Double"));
        CachedClasses.doubletolong = (*env)->GetStaticMethodID(env,CachedClasses.doubletype,"doubleToLongBits","(D)J");
        CachedClasses.doublevalue = (*env)->GetMethodID(env,CachedClasses.doubletype,"doubleValue","()D");
        CachedClasses.longtodouble = (*env)->GetStaticMethodID(env,CachedClasses.doubletype,"longBitsToDouble","(J)D");
        CachedClasses.createdouble = (*env)->GetMethodID(env,CachedClasses.doubletype,"<init>","(D)V");
        
        CachedClasses.booltype = (*env)->NewGlobalRef(env,(*env)->FindClass(env,"java/lang/Boolean"));
        CachedClasses.boolvalue = (*env)->GetMethodID(env,CachedClasses.booltype,"booleanValue","()Z");
        CachedClasses.createbool = (*env)->GetMethodID(env,CachedClasses.booltype,"<init>","(Z)V");
        
        CachedClasses.inttype = (*env)->NewGlobalRef(env,(*env)->FindClass(env,"java/lang/Integer"));
        CachedClasses.intvalue = (*env)->GetMethodID(env,CachedClasses.inttype,"intValue","()I");
        CachedClasses.createint = (*env)->GetMethodID(env,CachedClasses.inttype,"<init>","(I)V");
        
        CachedClasses.chartype = (*env)->NewGlobalRef(env,(*env)->FindClass(env,"java/lang/Character"));
        CachedClasses.charvalue = (*env)->GetMethodID(env,CachedClasses.chartype,"charValue","()C");
        CachedClasses.createchar = (*env)->GetMethodID(env,CachedClasses.chartype,"<init>","(C)V");
        
        CachedClasses.longtype = (*env)->NewGlobalRef(env,(*env)->FindClass(env,"java/lang/Long"));
        CachedClasses.longvalue = (*env)->GetMethodID(env,CachedClasses.longtype,"longValue","()J");     
        CachedClasses.createlong = (*env)->GetMethodID(env,CachedClasses.longtype,"<init>","(J)V");
       
        CachedClasses.datetype = (*env)->NewGlobalRef(env,(*env)->FindClass(env,"java/util/Date"));
        CachedClasses.datevalue = (*env)->GetMethodID(env,CachedClasses.datetype,"getTime","()J");         
        CachedClasses.createdate = (*env)->GetMethodID(env,CachedClasses.datetype,"<init>","(J)V");
        
        CachedClasses.stringtype = (*env)->NewGlobalRef(env,(*env)->FindClass(env,"java/lang/String"));
        
        return &CachedClasses;
}

javacache*  DropCache(JNIEnv* env) {
	/* exceptions  */
        (*env)->DeleteGlobalRef(env,CachedClasses.exception);
        (*env)->DeleteGlobalRef(env,CachedClasses.truncation);
        /*  boundary objects */
        (*env)->DeleteGlobalRef(env,CachedClasses.talker);
        (*env)->DeleteGlobalRef(env,CachedClasses.linkid);
        (*env)->DeleteGlobalRef(env,CachedClasses.boundin);
        (*env)->DeleteGlobalRef(env,CachedClasses.boundout);
        /*  output types */
        (*env)->DeleteGlobalRef(env,CachedClasses.doubletype);       
        (*env)->DeleteGlobalRef(env,CachedClasses.booltype);
        (*env)->DeleteGlobalRef(env,CachedClasses.inttype);
        (*env)->DeleteGlobalRef(env,CachedClasses.chartype);
        (*env)->DeleteGlobalRef(env,CachedClasses.longtype);
        (*env)->DeleteGlobalRef(env,CachedClasses.datetype);
        (*env)->DeleteGlobalRef(env,CachedClasses.stringtype);
        
        return &CachedClasses;
}

int PassInValue(JNIEnv* env,StmtMgr mgr, char* name, short type, jobject data) {
    if ( (*env)->IsSameObject(env,NULL,data) ) {
        SetInputValue(mgr,name,type,NULL,0);
    } else {
	switch( type )
	{
            case INT4TYPE:
                ExtractIntValue(env,mgr,name,type,data);
                break;
            case VARCHARTYPE:
                ExtractStringValue(env,mgr,name,type,data);
                break;
            case CHARTYPE:
                ExtractCharacterValue(env,mgr,name,type,data);
                break;
            case BOOLTYPE:
                ExtractBooleanValue(env,mgr,name,type,data);
                break;
            case BYTEATYPE:
                ExtractByteArrayValue(env,mgr,name,type,data);
                break;
            case TIMESTAMPTYPE:
                ExtractDateValue(env,mgr,name,type,data);
                break;
            case DOUBLETYPE:
                ExtractDoubleValue(env,mgr,name,type,data);
                break;
            case LONGTYPE:
                ExtractLongValue(env,mgr,name,type,data);
                break;
            case BLOBTYPE:
            case TEXTTYPE:
            case SLOTTYPE:
            case JAVATYPE:
                ExtractByteArrayValue(env,mgr,name,type,data);
                break;
            case STREAMTYPE:
                break;
            default:
                break;
	}
    }
}
/*
jfieldID
GetFieldId(JNIEnv* env,jobject target,jstring slot,const char* signature) {
    char        name[64];
    jclass      bindclass;
    jsize       slotsize = 0;
    jfieldID    field;
    
    bindclass = (*env)->GetObjectClass(env,target);
    slotsize = (*env)->GetStringLength(env,slot);
    
    (*env)->GetStringUTFRegion(env,slot,0, slotsize, name); 	
    name[slotsize] = 0;
    return (*env)->GetFieldID(env,bindclass,name,signature);
}

jmethodID
GetMethodID(JNIEnv* env,jobject target,const char* name,const char* signature) {
    jclass      bindclass;
    
    bindclass = (*env)->GetObjectClass(env,target);
    return (*env)->GetMethodID(env,bindclass,name,signature);
}
*/
void
ExtractIntValue(JNIEnv* env,StmtMgr mgr,char* name, short type, jobject target) {
    if ((*env)->IsInstanceOf(env,target,Cache->inttype)) {
        union {
            char    buffer[4];
            jint    val;
        }   convert;
        convert.val = (*env)->CallIntMethod(env,target,Cache->intvalue);
        SetInputValue(mgr, name, type, convert.buffer,4);
    } else if (!(*env)->ExceptionOccurred(env) ) {
        (*env)->ThrowNew(env,Cache->exception,"passed in value is not a Number");    
    }
}

void
ExtractStringValue(JNIEnv* env, StmtMgr mgr,char* name, short type,jobject data) {
    jsize           len = 0;
    jstring         value;
    const char*          buffer;
    jboolean        copy;
    int             written;
            
    if ((*env)->IsInstanceOf(env,data,Cache->stringtype)) {
        value = (jstring)data;

        len = (*env)->GetStringUTFLength(env,value);
        buffer = (*env)->GetStringUTFChars(env,(jstring)data,&copy);
        SetInputValue(mgr,name,type,(void*)buffer,len);

        (*env)->ReleaseStringUTFChars(env,data,buffer);    
    } else if (!(*env)->ExceptionOccurred(env) ) {
        (*env)->ThrowNew(env,Cache->exception,"passed in value is not a String");    
    }
}

void
ExtractCharacterValue(JNIEnv* env,StmtMgr mgr, char* name, short type, jobject target) {
    if ( (*env)->IsInstanceOf(env,target,Cache->chartype) ) {
        union {
            char buffer[1];
            jchar value;
        } convert;
        convert.value = (*env)->CallCharMethod(env,target,Cache->charvalue);
        SetInputValue(mgr,name,type,convert.buffer,1);
    } else if (!(*env)->ExceptionOccurred(env) ) {
        (*env)->ThrowNew(env,Cache->exception,"passed in value is not a Character");    
    }
}

void
ExtractBooleanValue(JNIEnv* env,StmtMgr mgr, char* name, short type,jobject target) {
    if ( (*env)->IsInstanceOf(env,target,Cache->booltype) ) {
        union {
            char    buffer[1];
            jboolean    val;
        }   convert;
        convert.val = (*env)->CallBooleanMethod(env,target,Cache->boolvalue);
        SetInputValue(mgr,name,type,convert.buffer,1);
    } else if (!(*env)->ExceptionOccurred(env) ) {
        (*env)->ThrowNew(env,Cache->exception,"passed in value is not a Boolean");    
    }
}

void
ExtractDoubleValue(JNIEnv* env,StmtMgr mgr, char* name, short type,jobject target) {
    if ( (*env)->IsInstanceOf(env,target,Cache->doubletype) ) {
        union {
            char    buffer[8];
            jdouble  val;
        }   convert;

        convert.val = (*env)->CallDoubleMethod(env,target,Cache->doublevalue);            
        SetInputValue(mgr,name,type,convert.buffer,8);
    } else if (!(*env)->ExceptionOccurred(env) ) {
        (*env)->ThrowNew(env,Cache->exception,"passed in value is not a Double");    
    }
}

void
ExtractLongValue(JNIEnv* env,StmtMgr mgr, char* name, short type,jobject target) {
    if ( (*env)->IsInstanceOf(env,target,Cache->longtype) ) {
        union {
            char    buffer[8];
            jlong   val;
        }   convert;
        convert.val = (*env)->CallLongMethod(env,target,Cache->longvalue);
        SetInputValue(mgr,name,type,convert.buffer,8);
    } else if (!(*env)->ExceptionOccurred(env) ) {
        (*env)->ThrowNew(env,Cache->exception,"passed in value is not a Long");    
    }
}


void
ExtractDateValue(JNIEnv* env,StmtMgr mgr, char* name, short type, jobject target) {
    if ( (*env)->IsInstanceOf(env,target,Cache->datetype) ) {
        union {
            char    buffer[8];
            jdouble  val;
        }   convert;

        convert.val = (jdouble)(*env)->CallLongMethod(env,target,Cache->datevalue);
        convert.val /= 1000;
        convert.val -= (10957 * 86400);

        SetInputValue(mgr,name,type,convert.buffer,8);
    } else if (!(*env)->ExceptionOccurred(env) ) {
            (*env)->ThrowNew(env,Cache->exception,"passed in value is not a Date");    
    }
}


void
ExtractByteArrayValue(JNIEnv* env,StmtMgr mgr,char* name, short type,jobject target) {
/*
    jclass clacla = (*env)->FindClass(env,"java/lang/Class");
    jclass clazz = (*env)->GetObjectClass(env,target);
    jmethodID check = (*env)->GetMethodID(env,clacla,"isArray","()Z");
    jboolean array = (*env)->CallBooleanMethod(env,clazz,check);
    if ( array ) {
        jmethodID compcheck = (*env)->GetMethodID(env,clacla,"getComponentType","()Ljava/lang/Class;");
        jclass comp = (jclass)(*env)->CallObjectMethod(env,clazz,compcheck);
        jmethodID prim = (*env)->GetMethodID(env,clacla,"isPrimative","()Z");
        if ( (*env)->CallBooleanMethod(env,comp,prim) ) {
            char        namebuf[256];
            jmethodID namecheck = (*env)->GetMethodID(env,clacla,"getName","()Ljava/lang/String;");
            jstring name = (*env)->CallObjectMethod(env,comp,namecheck);
            jsize len = (*env)->GetStringLength(env,name);
            (*env)->GetStringUTFRegion(env,name,0,len,namebuf);
            namebuf[len] = 0x00;
            if ( strcmp(namebuf,"byte") == 0 ) {
                ExtractBytes(env,mgr,bound,(jbyteArray)target);
            }
        }
    }
*/
    ExtractBytes(env,mgr,name,type,(jbyteArray)target);
}

static void ExtractBytes(JNIEnv* env, StmtMgr mgr,char* name, short type,jbyteArray target) {
    jsize       length = (*env)->GetArrayLength(env,target);
    jboolean    copy;
    jbyte*      buffer;

    buffer = (*env)->GetByteArrayElements(env,target,&copy);
    SetInputValue(mgr,name,type,buffer,length);
    (*env)->ReleaseByteArrayElements(env,target,buffer,JNI_ABORT);
}

static jobject CreateIntField(jint* var, JNIEnv* env) {
    return (*env)->NewObject(env,Cache->inttype,Cache->createint,*var);
}
static jobject CreateCharField(jchar* var, JNIEnv* env) {
    return (*env)->NewObject(env,Cache->chartype,Cache->createchar,*var);
}

static jobject CreateBooleanField(char* var, JNIEnv* env) {
    jboolean flag = ( *var ) ? JNI_TRUE : JNI_FALSE;
    return (*env)->NewObject(env,Cache->booltype,Cache->createbool,flag);
}
static jobject CreateStringField(char* var, int length, JNIEnv* env) {
    *(var + length) = 0x00;
    return (jobject)(*env)->NewStringUTF(env,var);
}
static jobject CreateBinaryField(char* var, int length, JNIEnv* env) {
    jbyteArray jb = (*env)->NewByteArray(env,length);
    if ( jb == NULL ) {
        if ( !(*env)->ExceptionOccurred(env) ) {
            (*env)->ThrowNew(env,Cache->exception,"binary fetch");
        }
        return NULL;
    }
    (*env)->SetByteArrayRegion(env,jb,0,length,(signed char*)var);
    return (jobject)jb;
}
static jobject CreateDoubleField(jdouble* var, JNIEnv* env) {
/*
    jdouble nval = (*env)->CallStaticDoubleMethod(env,Cache->doubletype,Cache->longtodouble,*var);
*/
    return (*env)->NewObject(env,Cache->doubletype,Cache->createdouble,var);
}

static jobject CreateDateField(jdouble* var, JNIEnv* env) {
    *var += (10957 * 86400);
    *var *= 1000;
    return (*env)->NewObject(env,Cache->datetype,Cache->createdate,(jlong)*var);
}

static jobject CreateLongField(jlong* var, JNIEnv* env) {
    return (*env)->NewObject(env,Cache->longtype,Cache->createlong,*var);
}

int PassOutValue(StmtMgr mgr, int type, void* value, int length, void* userspace, void* funcargs ) {
    JNIEnv*   env = funcargs;
    jobject      target = userspace;
    jobject setval = NULL;

        if (target == NULL ) return 0;
    if ( (*env)->IsSameObject(env,target,NULL)) return 0;
        if (type == STREAMTYPE ) return 0;

        if ( value == NULL ) {
            (*env)->SetBooleanField(env,target,Cache->nullfield,JNI_TRUE);
            return 0;
        } else {
            (*env)->SetBooleanField(env,target,Cache->nullfield,JNI_FALSE);
        }

        switch(type)
        {
            case INT4TYPE:
                setval = CreateIntField(value,env);
                break;
            case VARCHARTYPE:
                setval = CreateStringField(value,length,env);
                break;
            case CHARTYPE:
                setval = CreateCharField(value,env);
                break;
            case BOOLTYPE:
                setval = CreateBooleanField(value,env);
                break;
            case DOUBLETYPE:
                setval = CreateDoubleField(value,env);
                break;
            case BYTEATYPE:
            case BLOBTYPE:
            case TEXTTYPE:
            case JAVATYPE:
                setval = CreateBinaryField(value,length,env);
                break;
            case TIMESTAMPTYPE:
                setval = CreateDateField(value,env);
                break;
            case LONGTYPE:
                setval = CreateLongField(value,env);
                break;
            case STREAMTYPE:
                break;
                /* do nothing passing done though stream already */
            default:
                DelegateError(mgr,"PASSING","results no passed, type error",745);
                break;
        }

        if ( setval != NULL ) {
            (*env)->SetObjectField(env,target,Cache->value,setval);
            (*env)->DeleteLocalRef(env,setval);
        }
        return 0;
}

int PassResults(JNIEnv* env, StmtMgr mgr)
{
        
        GetOutputs(mgr,env,PassOutValue);
        
	return CheckForErrors(mgr);
}
