/* 
 * File:   WeaverValueExtractor.h
 * Author: mscott
 *
 * Created on August 3, 2008, 5:40 PM
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
    jclass                          linkid;
    jclass                          boundin;
    jclass                          boundout;
    
    jclass                          chartype;
    jclass                          inttype;
    jclass                          longtype;
    jclass                          datetype;
    jclass                          doubletype;
    jclass                          booltype;
    jclass                          stringtype;
   /*  BaseWeaverConnection fields */
    jfieldID                        idfield;   
    jfieldID                        tracker;
    jfieldID                        result;
    jfieldID                        eText;
    jfieldID                        eState;
    /*  BoundOutput fields */
    jfieldID                        value;
    jfieldID                        nullfield;
    /*  BoundOutput/BoundInput methods */
    jmethodID                        pipein;
    jmethodID                        pipeout;
    jmethodID                        infoin;
    jmethodID                        infoout;

    jmethodID                        charvalue;   
    jmethodID                        createchar;   
    jmethodID                        doubletolong;
    jmethodID                        longtodouble;
    jmethodID                        doublevalue;
    jmethodID                        createdouble;
    jmethodID                        boolvalue;
    jmethodID                        createbool;
    jmethodID                        intvalue;
    jmethodID                        createint;
    jmethodID                        longvalue;
    jmethodID                        createlong;
    jmethodID                        datevalue;
    jmethodID                        createdate;
} javacache;

javacache* CreateCache(JNIEnv* env);
javacache* DropCache(JNIEnv* env);
int PassInValue(JNIEnv* env,StmtMgr mgr,Input bound, jobject object);
int PassResults(JNIEnv* env, StmtMgr mgr);

#ifdef	__cplusplus
}
#endif

#endif	/* _WEAVERVALUEEXTRACTOR_H */

