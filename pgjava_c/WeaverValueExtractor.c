/*-------------------------------------------------------------------------
 *
 *	WeaverValueExtractor.c

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
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <pthread.h>
#include <math.h>

#include "WeaverStmtManager.h"
#include "WeaverValueExtractor.h"
#include "WeaverInterface.h"


static javacache CachedClasses;

static javacache*  Cache = &CachedClasses;

static int ExtractShortValue(JNIEnv* env, jobject target, void* data, int len);
static int ExtractIntValue(JNIEnv* env, jobject target, void* data, int len);
static int ExtractStringValue(JNIEnv* env,   jobject target, void* data, int len);
static int ExtractCharacterValue(JNIEnv* env,   jobject target, void* data, int len);
static int ExtractBooleanValue(JNIEnv* env,   jobject target, void* data, int len);
static int ExtractFloatValue(JNIEnv* env,   jobject target, void* data, int len);
static int ExtractDoubleValue(JNIEnv* env,   jobject target, void* data, int len);
static int ExtractLongValue(JNIEnv* env,   jobject target, void* data, int len);
static int ExtractDateValue(JNIEnv* env,   jobject target, void* data, int len);
static int ExtractByteArrayValue(JNIEnv* env,   jobject target, void* data, int len);
static int ExtractBytes(JNIEnv* env,jbyteArray target, void* data, int len);
static int MoveData(void* dest, const void* src, int len);

javacache*  CreateCache(JNIEnv* env) {
	/* exceptions  */
        CachedClasses.exception = (*env)->NewGlobalRef(env,(*env)->FindClass(env,"org/weaverdb/ExecutionException"));
        CachedClasses.ecstor = (*env)->GetMethodID(env,CachedClasses.exception,"<init>","(Ljava/lang/String;)V");
        CachedClasses.suppressed = (*env)->GetMethodID(env,CachedClasses.exception,"addSuppressed","(Ljava/lang/Throwable;)V");
        CachedClasses.truncation = (*env)->NewGlobalRef(env,(*env)->FindClass(env,"org/weaverdb/BinaryTruncation"));
        /*  boundary objects */
        CachedClasses.talker = (*env)->NewGlobalRef(env,(*env)->FindClass(env,"org/weaverdb/BaseWeaverConnection"));
        CachedClasses.boundin = (*env)->NewGlobalRef(env,(*env)->FindClass(env,"org/weaverdb/BoundInput"));
        CachedClasses.boundout = (*env)->NewGlobalRef(env,(*env)->FindClass(env,"org/weaverdb/BoundOutput"));
        /*  field ids  */
        CachedClasses.nativePointer = (*env)->GetFieldID(env,CachedClasses.talker,"nativePointer","J");

	CachedClasses.oindex = (*env)->GetFieldID(env,CachedClasses.boundout, "index","I");
	CachedClasses.oname = (*env)->GetFieldID(env,CachedClasses.boundout, "columnName","Ljava/lang/String;");
	CachedClasses.ovalue = (*env)->GetFieldID(env,CachedClasses.boundout, "value","Ljava/lang/Object;");

	CachedClasses.iname = (*env)->GetFieldID(env,CachedClasses.boundin, "name","Ljava/lang/String;");
	CachedClasses.ivalue = (*env)->GetFieldID(env,CachedClasses.boundin,"value","Ljava/lang/Object;");
        
        CachedClasses.pipein = (*env)->GetMethodID(env,CachedClasses.boundin,"pipeIn","(Ljava/nio/ByteBuffer;)I");
        CachedClasses.pipeout = (*env)->GetMethodID(env,CachedClasses.boundout,"pipeOut","(Ljava/nio/ByteBuffer;)I");
        CachedClasses.infoin = (*env)->GetMethodID(env,CachedClasses.talker,"pipeIn","([B)I");
        CachedClasses.infoout = (*env)->GetMethodID(env,CachedClasses.talker,"pipeOut","([B)I");
        CachedClasses.itypeid = (*env)->GetMethodID(env,CachedClasses.boundin,"getTypeId","()I");
        CachedClasses.otypeid = (*env)->GetMethodID(env,CachedClasses.boundout,"getTypeId","()I");
        /*  output types */
        CachedClasses.floattype = (*env)->NewGlobalRef(env,(*env)->FindClass(env,"java/lang/Float"));
        CachedClasses.floattoint = (*env)->GetStaticMethodID(env,CachedClasses.floattype,"floatToIntBits","(F)I");
        CachedClasses.floatvalue = (*env)->GetMethodID(env,CachedClasses.floattype,"floatValue","()F");
        CachedClasses.inttofloat = (*env)->GetStaticMethodID(env,CachedClasses.floattype,"intBitsToFloat","(I)F");
        CachedClasses.createfloat = (*env)->GetMethodID(env,CachedClasses.floattype,"<init>","(F)V");

        CachedClasses.doubletype = (*env)->NewGlobalRef(env,(*env)->FindClass(env,"java/lang/Double"));
        CachedClasses.doubletolong = (*env)->GetStaticMethodID(env,CachedClasses.doubletype,"doubleToLongBits","(D)J");
        CachedClasses.doublevalue = (*env)->GetMethodID(env,CachedClasses.doubletype,"doubleValue","()D");
        CachedClasses.longtodouble = (*env)->GetStaticMethodID(env,CachedClasses.doubletype,"longBitsToDouble","(J)D");
        CachedClasses.createdouble = (*env)->GetMethodID(env,CachedClasses.doubletype,"<init>","(D)V");
        
        CachedClasses.booltype = (*env)->NewGlobalRef(env,(*env)->FindClass(env,"java/lang/Boolean"));
        CachedClasses.boolvalue = (*env)->GetMethodID(env,CachedClasses.booltype,"booleanValue","()Z");
        CachedClasses.createbool = (*env)->GetMethodID(env,CachedClasses.booltype,"<init>","(Z)V");

        CachedClasses.shorttype = (*env)->NewGlobalRef(env,(*env)->FindClass(env,"java/lang/Short"));
        CachedClasses.shortvalue = (*env)->GetMethodID(env,CachedClasses.shorttype,"shortValue","()S");
        CachedClasses.createshort = (*env)->GetMethodID(env,CachedClasses.shorttype,"<init>","(S)V");
        
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
        (*env)->DeleteGlobalRef(env,CachedClasses.floattype);       
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
    if ( (*env)->IsSameObject(env,NULL,object) ) {
        return NULL_VALUE;
    } else {
	switch( passType )
	{
            case INT2TYPE:
                return ExtractShortValue(env,object,data,length);
                break;
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
            case FLOATTYPE:
                return ExtractFloatValue(env,object,data,length);
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
ExtractShortValue(JNIEnv* env, jobject target, void* data, int len) {
    if ((*env)->IsInstanceOf(env,target,Cache->shorttype)) {
        union {
            char    buffer[2];
            jshort    val;
        }   convert;
        convert.val = (*env)->CallShortMethod(env,target,Cache->shortvalue);
        MoveData(data,convert.buffer,2);
        return 2;
    } else if (!(*env)->ExceptionOccurred(env) ) {
        (*env)->ThrowNew(env,Cache->exception,"passed in value is not a Number");    
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
    int             written;
            
    if ((*env)->IsInstanceOf(env,target,Cache->stringtype)) {
        value = (jstring)target;

        len = (*env)->GetStringUTFLength(env,value);
        if (data != NULL) {
            if (len > max) {
                return -1;
            } else {
                buffer = (*env)->GetStringUTFChars(env,value,NULL);
                MoveData(data,buffer,len);
                (*env)->ReleaseStringUTFChars(env,value,buffer); 
            }
        }
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
ExtractFloatValue(JNIEnv* env, jobject target, void* data, int len) {
    if ( (*env)->IsInstanceOf(env,target,Cache->floattype) ) {
        union {
            char    buffer[4];
            jfloat  val;
        }   convert;

        convert.val = (*env)->CallFloatMethod(env,target,Cache->floatvalue);            
        MoveData(data,convert.buffer,4);
        return 4;
    } else if (!(*env)->ExceptionOccurred(env) ) {
        (*env)->ThrowNew(env,Cache->exception,"passed in value is not a Double");    
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
            return TRUNCATION_VALUE;
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

static jobject CreateShortField(jshort* var, JNIEnv* env) {
    return (*env)->NewObject(env,Cache->shorttype,Cache->createshort,*var);
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

static jobject CreateStringField(const char* var, int length, JNIEnv* env) {
    char buffer[length+1];
    memmove(buffer,var,length);
    buffer[length] = 0x00;
    return (jobject)(*env)->NewStringUTF(env,buffer);
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
    return (*env)->NewObject(env,Cache->doubletype,Cache->createdouble,*var);
}

static jobject CreateFloatField(jfloat* var, JNIEnv* env) {
    return (*env)->NewObject(env,Cache->floattype,Cache->createfloat,*var);
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


    if ( data == NULL || length == NULL_VALUE ) {
        (*env)->SetObjectField(env,target,Cache->ovalue,NULL);
        return 0;
    } else if (passType == METANAMETYPE) {
        setval = CreateStringField(data,length,env);
        (*env)->SetObjectField(env,target,Cache->oname,setval);
        (*env)->DeleteLocalRef(env,setval);
         return length;
    } else {
        switch(passType) {
            case INT2TYPE:
                setval = CreateShortField(data,env);
                break;
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
            case FLOATTYPE:
                setval = CreateFloatField(data,env);
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
            default: {
                char err[256];
                snprintf(err,256,"unable to understand type bound:%d link:%d pass:%d",bindType,linkType,passType);
                (*env)->ThrowNew(env,Cache->exception,err);    
                return 745;
            }
        }

        if ( setval != NULL ) {
            (*env)->SetObjectField(env,target,Cache->ovalue,setval);
            (*env)->DeleteLocalRef(env,setval);
        }
    }
    return length;
}
