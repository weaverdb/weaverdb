/*-------------------------------------------------------------------------
 *
 *	WeaverValueExtractor.c

 *
 * Portions Copyright (c) 2002-2006, Myron K Scott
 *
 *
 * IDENTIFICATION
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

#include "WeaverStmtManager.h"
#include "WeaverValueExtractor.h"
#include "env/WeaverInterface.h"


static javacache CachedClasses;

static javacache*  Cache = &CachedClasses;

static int ExtractIntValue(JNIEnv* env, jobject target, void* data, int len);
static int ExtractStringValue(JNIEnv* env,   jobject target, void* data, int len);
static int ExtractCharacterValue(JNIEnv* env,   jobject target, void* data, int len);
static int ExtractBooleanValue(JNIEnv* env,   jobject target, void* data, int len);
static int ExtractDoubleValue(JNIEnv* env,   jobject target, void* data, int len);
static int ExtractLongValue(JNIEnv* env,   jobject target, void* data, int len);
static int ExtractDateValue(JNIEnv* env,   jobject target, void* data, int len);
static int ExtractByteArrayValue(JNIEnv* env,   jobject target, void* data, int len);
static int ExtractBytes(JNIEnv* env,jbyteArray target, void* data, int len);
static int MoveData(void* dest, const void* src, int len);

javacache*  CreateCache(JNIEnv* env) {
	/* exceptions  */
        CachedClasses.exception = (*env)->NewGlobalRef(env,(*env)->FindClass(env,"driver/weaver/ExecutionException"));
        CachedClasses.truncation = (*env)->NewGlobalRef(env,(*env)->FindClass(env,"driver/weaver/BinaryTruncation"));
        /*  boundary objects */
        CachedClasses.talker = (*env)->NewGlobalRef(env,(*env)->FindClass(env,"driver/weaver/BaseWeaverConnection"));
        CachedClasses.boundin = (*env)->NewGlobalRef(env,(*env)->FindClass(env,"driver/weaver/BoundInput"));
        CachedClasses.boundout = (*env)->NewGlobalRef(env,(*env)->FindClass(env,"driver/weaver/BoundOutput"));
        /*  field ids  */
        CachedClasses.nativePointer = (*env)->GetFieldID(env,CachedClasses.talker,"nativePointer","J");
        CachedClasses.result = (*env)->GetFieldID(env,CachedClasses.talker, "resultField","I");
	CachedClasses.eText =  (*env)->GetFieldID(env,CachedClasses.talker,"errorText","Ljava/lang/String;");
	CachedClasses.eState =  (*env)->GetFieldID(env,CachedClasses.talker,"state","Ljava/lang/String;");

	CachedClasses.oindex = (*env)->GetFieldID(env,CachedClasses.boundout, "index","I");
	CachedClasses.ovalue = (*env)->GetFieldID(env,CachedClasses.boundout, "value","Ljava/lang/Object;");
	CachedClasses.onullfield = (*env)->GetFieldID(env,CachedClasses.boundout,"isnull","Z");

	CachedClasses.iname = (*env)->GetFieldID(env,CachedClasses.boundin, "name","Ljava/lang/String;");
	CachedClasses.ivalue = (*env)->GetFieldID(env,CachedClasses.boundin,"value","Ljava/lang/Object;");
        
        CachedClasses.pipein = (*env)->GetMethodID(env,CachedClasses.boundin,"pipeIn","(Ljava/nio/ByteBuffer;)I");
        CachedClasses.pipeout = (*env)->GetMethodID(env,CachedClasses.boundout,"pipeOut","(Ljava/nio/ByteBuffer;)V");
        CachedClasses.infoin = (*env)->GetMethodID(env,CachedClasses.talker,"pipeIn","([B)I");
        CachedClasses.infoout = (*env)->GetMethodID(env,CachedClasses.talker,"pipeOut","([B)V");
        CachedClasses.itypeid = (*env)->GetMethodID(env,CachedClasses.boundin,"getTypeId","()I");
        CachedClasses.otypeid = (*env)->GetMethodID(env,CachedClasses.boundout,"getTypeId","()I");
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

int PassInValue(JNIEnv* env,int bindType, int linkType, int passType,jobject object,void* data, int length) {
    if ( (*env)->IsSameObject(env,NULL,data) ) {
        return 0;
    } else {
	switch( passType )
	{
            case INT4TYPE:
                return ExtractIntValue(env,object,data,length);
                break;
            case VARCHARTYPE:
                return ExtractStringValue(env,object,data,length);
                break;
            case CHARTYPE:
                return ExtractCharacterValue(env,object,data,length);
                break;
            case BOOLTYPE:
                return ExtractBooleanValue(env,object,data,length);
                break;
            case BYTEATYPE:
                return ExtractByteArrayValue(env,object,data,length);
                break;
            case TIMESTAMPTYPE:
                return ExtractDateValue(env,object,data,length);
                break;
            case DOUBLETYPE:
                return ExtractDoubleValue(env,object,data,length);
                break;
            case LONGTYPE:
                return ExtractLongValue(env,object,data,length);
                break;
            case BLOBTYPE:
            case TEXTTYPE:
            case SLOTTYPE:
            case JAVATYPE:
                return ExtractByteArrayValue(env,object,data,length);
                break;
            case STREAMTYPE:
                /* should not get here */
                break;
            default:
                break;
	    }
    }
    return 0;
}

int
ExtractIntValue(JNIEnv* env, jobject target, void* data, int len) {
    if ((*env)->IsInstanceOf(env,target,Cache->inttype)) {
        union {
            char    buffer[4];
            jint    val;
        }   convert;
        convert.val = (*env)->CallIntMethod(env,target,Cache->intvalue);
        MoveData(data,convert.buffer,4);
        return 4;
    } else if (!(*env)->ExceptionOccurred(env) ) {
        (*env)->ThrowNew(env,Cache->exception,"passed in value is not a Number");    
    }
    return 0;
}

int
ExtractStringValue(JNIEnv* env, jobject target, void* data, int max) {
    jsize           len = 0;
    jstring         value;
    const char*          buffer;
    jboolean        copy;
    int             written;
            
    if ((*env)->IsInstanceOf(env,data,Cache->stringtype)) {
        value = (jstring)data;

        len = (*env)->GetStringUTFLength(env,value);
        buffer = (*env)->GetStringUTFChars(env,(jstring)data,&copy);
        MoveData(data,buffer,len);

        (*env)->ReleaseStringUTFChars(env,data,buffer);    
        return len;
    } else if (!(*env)->ExceptionOccurred(env) ) {
        (*env)->ThrowNew(env,Cache->exception,"passed in value is not a String");    
    }
    return 0;
}

int
ExtractCharacterValue(JNIEnv* env, jobject target, void* data, int len) {
    if ( (*env)->IsInstanceOf(env,target,Cache->chartype) ) {
        union {
            char buffer[1];
            jchar value;
        } convert;
        convert.value = (*env)->CallCharMethod(env,target,Cache->charvalue);
        MoveData(data,convert.buffer,1);
        return 1;
    } else if (!(*env)->ExceptionOccurred(env) ) {
        (*env)->ThrowNew(env,Cache->exception,"passed in value is not a Character");    
    }
    return 0;
}

int
ExtractBooleanValue(JNIEnv* env, jobject target, void* data, int len) {
    if ( (*env)->IsInstanceOf(env,target,Cache->booltype) ) {
        union {
            char    buffer[1];
            jboolean    val;
        }   convert;
        convert.val = (*env)->CallBooleanMethod(env,target,Cache->boolvalue);
        MoveData(data,convert.buffer,1);
        return 1;
    } else if (!(*env)->ExceptionOccurred(env) ) {
        (*env)->ThrowNew(env,Cache->exception,"passed in value is not a Boolean");    
    }
    return 0;
}

int
ExtractDoubleValue(JNIEnv* env, jobject target, void* data, int len) {
    if ( (*env)->IsInstanceOf(env,target,Cache->doubletype) ) {
        union {
            char    buffer[8];
            jdouble  val;
        }   convert;

        convert.val = (*env)->CallDoubleMethod(env,target,Cache->doublevalue);            
        MoveData(data,convert.buffer,8);
        return 8;
    } else if (!(*env)->ExceptionOccurred(env) ) {
        (*env)->ThrowNew(env,Cache->exception,"passed in value is not a Double");    
    }
    return 0;
}

int
ExtractLongValue(JNIEnv* env, jobject target, void* data, int len) {
    if ( (*env)->IsInstanceOf(env,target,Cache->longtype) ) {
        union {
            char    buffer[8];
            jlong   val;
        }   convert;
        convert.val = (*env)->CallLongMethod(env,target,Cache->longvalue);
        MoveData(data,convert.buffer,8);
        return 8;
    } else if (!(*env)->ExceptionOccurred(env) ) {
        (*env)->ThrowNew(env,Cache->exception,"passed in value is not a Long");    
    }
    return 0;
}


int
ExtractDateValue(JNIEnv* env, jobject target, void* data, int len) {
    if ( (*env)->IsInstanceOf(env,target,Cache->datetype) ) {
        union {
            char    buffer[8];
            jdouble  val;
        }   convert;

        convert.val = (jdouble)(*env)->CallLongMethod(env,target,Cache->datevalue);
        convert.val /= 1000;
        convert.val -= (10957 * 86400);

        MoveData(data,convert.buffer,8);
        return 8;
    } else if (!(*env)->ExceptionOccurred(env) ) {
            (*env)->ThrowNew(env,Cache->exception,"passed in value is not a Date");    
    }
    return 0;
}


int
ExtractByteArrayValue(JNIEnv* env, jobject target, void* data, int len) {
    return ExtractBytes(env,(jbyteArray)target,data,len);
}

static int ExtractBytes(JNIEnv* env,jbyteArray target,void* data, int len) {
    jsize       length = (*env)->GetArrayLength(env,target);
    jboolean    copy;
    jbyte*      buffer;
    if (data != NULL) {
        if (len < length) {
            return -1;
        } else {
            buffer = (*env)->GetByteArrayElements(env,target,&copy);
            MoveData(data,buffer,length);
            (*env)->ReleaseByteArrayElements(env,target,buffer,JNI_ABORT);
        }
    }
    return length;
}

static int MoveData(void* dest, const void* src, int len) {
    if (src == NULL || dest == NULL) return 0;
    memmove(dest, src, len);
    return len;
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

int PassOutValue(JNIEnv* env,int bindType, int linkType, int passType, jobject target, void* data, int length) {
    jobject setval = NULL;

    if (target == NULL ) return 0;
    if ( (*env)->IsSameObject(env,target,NULL)) return 0;

    if ( length == 0 ) {
        (*env)->SetBooleanField(env,target,Cache->onullfield,JNI_TRUE);
        return 0;
    } else {
        (*env)->SetBooleanField(env,target,Cache->onullfield,JNI_FALSE);
    }

        switch(passType)
        {
            case INT4TYPE:
                setval = CreateIntField(data,env);
                break;
            case VARCHARTYPE:
                setval = CreateStringField(data,length,env);
                break;
            case CHARTYPE:
                setval = CreateCharField(data,env);
                break;
            case BOOLTYPE:
                setval = CreateBooleanField(data,env);
                break;
            case DOUBLETYPE:
                setval = CreateDoubleField(data,env);
                break;
            case BYTEATYPE:
            case BLOBTYPE:
            case TEXTTYPE:
            case JAVATYPE:
                setval = CreateBinaryField(data,length,env);
                break;
            case TIMESTAMPTYPE:
                setval = CreateDateField(data,env);
                break;
            case LONGTYPE:
                setval = CreateLongField(data,env);
                break;
            case STREAMTYPE:
                /* should never get here */
                break;
            default:
                return 745;
                break;
        }

        if ( setval != NULL ) {
            (*env)->SetObjectField(env,target,Cache->ovalue,setval);
            (*env)->DeleteLocalRef(env,setval);
        }
        return 0;
}
