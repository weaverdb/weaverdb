/*-------------------------------------------------------------------------
 *
 *	WeaverValueExtractor.h

 *
 * Copyright (c) 2000-2024, Myron Scott  <myron@weaverdb.org>
 *
 *
 *
 *-------------------------------------------------------------------------
 */
#include <jni.h>
#include <sys/types.h>

#ifndef _WEAVERVALUEEXTRACTOR_H
#define	_WEAVERVALUEEXTRACTOR_H

#ifdef	__cplusplus
extern "C" {
#endif
    
typedef struct javacache {
    jclass                          exception;
    jclass                          truncation;
    jclass                          talker;
    jclass                          boundin;
    jclass                          boundout;

    jclass                          chartype;
    jclass                          shorttype;
    jclass                          inttype;
    jclass                          longtype;
    jclass                          datetype;
    jclass                          floattype;
    jclass                          doubletype;
    jclass                          booltype;
    jclass                          stringtype;
   /*  BaseWeaverConnection fields */
    jfieldID                        nativePointer;
    jfieldID                        result;
    jfieldID                        eText;
    jfieldID                        eState;
    /*  BoundOutput fields */
    jfieldID                        oindex;
    jfieldID                        oname;
    jfieldID                        ovalue;
    /*  BoundInput fields */
    jfieldID                        iname;
    jfieldID                        ivalue;

    jmethodID                        ecstor;
    jmethodID                        suppressed;
    /*  BoundOutput/BoundInput methods */
    jmethodID                        pipein;
    jmethodID                        pipeout;
    jmethodID                        infoin;
    jmethodID                        infoout;
    jmethodID                        itypeid;
    jmethodID                        otypeid;

    jmethodID                        charvalue;   
    jmethodID                        createchar;   

    jmethodID                        floattoint;
    jmethodID                        inttofloat;
    jmethodID                        floatvalue;
    jmethodID                        createfloat;

    jmethodID                        doubletolong;
    jmethodID                        longtodouble;
    jmethodID                        doublevalue;
    jmethodID                        createdouble;
    jmethodID                        boolvalue;
    jmethodID                        createbool;
    jmethodID                        shortvalue;
    jmethodID                        createshort;
    jmethodID                        intvalue;
    jmethodID                        createint;
    jmethodID                        longvalue;
    jmethodID                        createlong;
    jmethodID                        datevalue;
    jmethodID                        createdate;
} javacache;

javacache* CreateCache(JNIEnv* env);
javacache* DropCache(JNIEnv* env);
int PassInValue(JNIEnv* env,int bindType, int linkType, int passType,jobject object,void* data, int length);
int PassOutValue(JNIEnv* env,int bindType, int linkType, int passType,jobject object,void* data, int length);

#ifdef	__cplusplus
}
#endif

#endif	/* _WEAVERVALUEEXTRACTOR_H */

