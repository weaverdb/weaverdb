/*-------------------------------------------------------------------------
 *
 * java.h
 *	  Definitions for the date/time and other date/time support code.
 *	  The support code is shared with other date data types,
 *	   including abstime, reltime, date, and time.
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 *
 *-------------------------------------------------------------------------
 */
#ifndef JAVA_H
#define JAVA_H

#include <jni.h>

#include "c.h"
#include "postgres.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct funcdef* JavaFunction;

PG_EXTERN bytea*   javatextin(char* obj);
PG_EXTERN char*   javatextout(bytea* datum);

PG_EXTERN bytea*   javain(jobject obj);
PG_EXTERN jobject   javaout(bytea* datum);

PG_EXTERN void SetJVM(JavaVM* java, const char* loader);
PG_EXTERN void SetJavaObjectLoader(const char* loader);


PG_EXTERN Datum fmgr_javaA(const char *name, int nargs, Oid* types, Datum* values, Oid* returnType, bool *isNull);
PG_EXTERN Datum fmgr_cached_javaA(JavaFunction jinfo, int nargs, Datum *args, Oid* returnType, bool *isNull);

PG_EXTERN bool java_instanceof(bytea* obj,bytea* cname);
PG_EXTERN int32 java_compare(bytea* obj1,bytea* obj2);

PG_EXTERN bool java_equals(bytea* obj1,bytea* obj2);
PG_EXTERN bool java_noteq(bytea* obj1,bytea* obj2);
PG_EXTERN bool java_gt(bytea* obj1,bytea* obj2);
PG_EXTERN bool java_lt(bytea* obj1,bytea* obj2);
PG_EXTERN bool java_gteq(bytea* obj1,bytea* obj2);
PG_EXTERN bool java_lteq(bytea* obj1,bytea* obj2);

PG_EXTERN int javalen(bytea* obj);

PG_EXTERN JavaFunction GetJavaFunction(const char *name, int nargs, Oid * types);
PG_EXTERN Oid GetJavaReturnType(JavaFunction function);

bool
convert_java_to_scalar(Datum value,double* scaledval,Datum lobound,double* scaledlo,Datum hibound,double* scaledhi, Datum histogram);
#ifdef __cplusplus
}
#endif

#endif	 
