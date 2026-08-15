/*-------------------------------------------------------------------------
 *  java.c
 *     functions to handle java objects
 *
 * Portions Copyright (c) 2000-2024, Myron Scott  <myron@weaverdb.org>
 * IDENTIFICATION
 *
 *
 *-------------------------------------------------------------------------
 */
#include <ctype.h>
#include <math.h>

#include <errno.h>
#include <jni.h>
#include <string.h>
#include <strings.h>

#include "postgres.h"

#include "utils/builtins.h"
#include "utils/java.h"
#include "catalog/pg_type.h"
#include "utils/syscache.h"
#include "catalog/pg_proc.h"
#include "access/blobstorage.h"

static          Datum
                ConvertFromJavaArg(Oid type, jvalue val, bool* isNull);
static int GetJavaEnv(JNIEnv** env);

JavaVM         *jvm;
static const char* loader = "";

static MemoryContext function_cache_cxt;
static HTAB*  function_table;
static pthread_mutex_t   ftable_guard;
static pthread_key_t   attaches;

/* Protocol tags matching JavaCallProtocol.java (v1) */
#define INV_TAG_INT4        23
#define INV_TAG_INT8        20
#define INV_TAG_FLOAT8      701
#define INV_TAG_BOOL        16
#define INV_TAG_TEXT        25
#define INV_TAG_VARCHAR     1043
#define INV_TAG_JAVA_OBJECT 1830
#define INV_TAG_NULL        -1
#define INV_TAG_ERROR       -99   /* Error payload written by Java invoker */

/* FFM upcall-based Java function invoker (set via WRegisterJavaFunctionInvoker).
 * When non-NULL, the engine prefers this path over classic JNI for LANGUAGE 'java' calls.
 */
static void* java_ffm_invoker = NULL;

static jclass loader_class;
static jmethodID loader_out;
static jmethodID loader_in;
static jmethodID loader_text_in;
static jmethodID loader_text_out;
static jmethodID loader_compare;
static jmethodID loader_equals;

typedef struct funcdef {
    NameData     key;
    jclass       clazz;
    jmethodID    method;
    int          nargs;
    Oid         argTypes[FUNC_MAX_ARGS];
    Oid          returnType;
    bool         isStatic;

    /* Original strings needed for FFM upcall invoker (LANGUAGE 'java' via pure FFM) */
    char        *className;
    char        *methodName;
    char        *methodDesc;
} FuncDef;

static jvalue CallJavaFunction(JavaFunction def, int nargs, jvalue* args);
static jvalue ConvertToJavaArg(Oid type, Datum val);
static void FormJavaFunctionSig(char* buffer, int buflen, const char *name, int nargs, Oid * types);
static JavaFunction GetJavaCallArgs(const char *name, int nargs, Oid * types);
static void DetachThread(void* thread);

/* FFM invoker support
 *
 * This function pointer comes from Java via WRegisterJavaFunctionInvoker.
 *
 * EXACT SIGNATURE (must stay in sync with Java side):
 *
 *   int invoker(
 *       const char *className,   // null-terminated UTF-8
 *       const char *methodName,
 *       const char *methodDesc,
 *       int         isStatic,    // 1 = static, 0 = instance
 *       const char *argData,     // binary block per JavaCallProtocol v1
 *       int         argDataLen,
 *       char       *resultOut,   // writable buffer
 *       int         resultOutLen
 *   );
 *
 * Java side: JavaFunctionInvoker.createUpcallStub()
 * Protocol:  JavaCallProtocol.java
 */
typedef int (*InvokerFn)(const char*, const char*, const char*, int, const char*, int, char*, int);

static Datum InvokeJavaViaFFMFromDatums(JavaFunction def, int nargs, Datum *args, bool *isNull);
static void BuildArgBlockFromDatums(JavaFunction def, int nargs, Datum *args, char **outBlock, int *outLen);
static Datum ParseInvokerResultToDatum(const char *buf, int bufLen, Oid returnType, bool *isNull);
static int  CountJvmDescriptorParams(const char *desc);
static char *EnsureCap(char *buf, int *capacity, int pos, int need);

static int  CallJavaInvokerWithGrowableResult(InvokerFn fn,
                                              const char *cname, const char *mname, const char *mdesc,
                                              int isStaticFlag,
                                              const char *argBlock, int argLen,
                                              char **resultBufOut, int *resultLenOut);
static int  InspectWrittenResultSize(const char *buf, int bufSize);
static int  ResultLooksLikeError(const char *buf, int bufSize);

static void FunctionCacheInit() {
    HASHCTL ctl;

    if (function_table != NULL)
        return;

    function_cache_cxt = AllocSetContextCreate(NULL,
            "JavaFunctionCache",
            ALLOCSET_DEFAULT_MINSIZE,
            ALLOCSET_DEFAULT_INITSIZE,
            ALLOCSET_DEFAULT_MAXSIZE);
        
    memset(&ctl, 0, sizeof(HASHCTL));
    ctl.keysize = sizeof(NameData);
    ctl.entrysize = sizeof(FuncDef);
    ctl.hash = string_hash;
    ctl.hcxt = function_cache_cxt;
    
    function_table = hash_create("java function hash", 100, &ctl, HASH_ELEM | HASH_CONTEXT | HASH_FUNCTION);
    pthread_mutex_init(&ftable_guard, NULL);
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *jvm, void *reserved) {
    SetJVM(jvm,"org/weaverdb/WeaverObjectLoader");
    return JNI_VERSION_1_8;
}

void
SetJVM(JavaVM * java, const char *ol)
{
    jvm = java;

    FunctionCacheInit();
    SetJavaObjectLoader(ol);
    pthread_key_create(&attaches, DetachThread);
}

/* Called from FFM client path to register the Java-side upcall invoker. */
void
WRegisterJavaFunctionInvoker(void* invokerFn)
{
    FunctionCacheInit();
    java_ffm_invoker = invokerFn;
}

void
DetachThread(void* thread) {
    (*jvm)->DetachCurrentThread(jvm);
}

/* ========================================================
 * FFM Invoker Bridge (PRIMARY path for LANGUAGE 'java')
 *
 * LONG-TERM STRATEGY (as of 2026):
 *   - The FFM upcall path (via java_ffm_invoker) is now the **preferred and primary**
 *     mechanism for invoking Java stored procedures.
 *   - The classic JNI path (direct jmethodID + Call*MethodA) is retained only for
 *     backward compatibility with:
 *       * Legacy C-first launchers (see main.c / JNI_CreateJavaVM)
 *       * Old WeaverInitializer + weaver_jni library users
 *   - Over time the JNI invocation code for user Java functions can be deprecated
 *     and eventually removed once the FFM path is fully mature and widely adopted.
 *
 * THREAD / ATTACHMENT NOTES:
 *   The upcall will be invoked from native database threads (worker threads,
 *   writer thread, etc.). The JVM will attach these threads automatically when
 *   the upcall is made. No explicit AttachCurrentThread is required on the C side
 *   for the FFM path (unlike the old JNI path).
 *
 * When java_ffm_invoker is set (by DirectWeaverInitializer or equivalent),
 * we prefer the FFM upcall path.
 * ======================================================== */

static char *
EnsureCap(char *buf, int *capacity, int pos, int need)
{
    while (pos + need > *capacity) {
        *capacity *= 2;
        buf = repalloc(buf, *capacity);
    }
    return buf;
}

static int
CountJvmDescriptorParams(const char *desc)
{
    const char *p;
    int n = 0;

    if (desc == NULL || *desc != '(')
        return -1;
    p = desc + 1;
    while (*p != '\0' && *p != ')') {
        switch (*p) {
            case 'B':
            case 'C':
            case 'D':
            case 'F':
            case 'I':
            case 'J':
            case 'S':
            case 'Z':
                n++;
                p++;
                break;
            case 'L':
                n++;
                while (*p != '\0' && *p != ';')
                    p++;
                if (*p == ';')
                    p++;
                break;
            case '[':
                while (*p == '[')
                    p++;
                if (*p == 'L') {
                    while (*p != '\0' && *p != ';')
                        p++;
                    if (*p == ';')
                        p++;
                } else if (*p != '\0') {
                    p++;
                }
                n++;
                break;
            default:
                return -1;
        }
    }
    return n;
}

/*
 * Build a protocol v1 arg block directly from SQL Datums.
 * VARCHAR/TEXT go as UTF-8; JAVAOID as already-serialized bytes. No JNI.
 */
static void
BuildArgBlockFromDatums(JavaFunction def, int nargs, Datum *args, char **outBlock, int *outLen)
{
    int capacity = 8192;
    char *buf = palloc(capacity);
    int pos = 4; /* numArgs */
    int count = 0;

    for (int i = 0; i < nargs; i++) {
        Oid argtype = (i < def->nargs) ? def->argTypes[i] : 0;
        buf = EnsureCap(buf, &capacity, pos, 64);

        if (argtype != INT4OID && argtype != BOOLOID && DatumGetPointer(args[i]) == NULL) {
            *(int *)(buf + pos) = INV_TAG_NULL; pos += 4;
            *(int *)(buf + pos) = 0; pos += 4;
            count++;
            continue;
        }

        switch (argtype) {
            case INT4OID:
                *(int *)(buf + pos) = INV_TAG_INT4; pos += 4;
                *(int *)(buf + pos) = 4; pos += 4;
                *(int *)(buf + pos) = DatumGetInt32(args[i]); pos += 4;
                break;
            case INT8OID:
                *(int *)(buf + pos) = INV_TAG_INT8; pos += 4;
                *(int *)(buf + pos) = 8; pos += 4;
                *(long *)(buf + pos) = *(long *)DatumGetPointer(args[i]); pos += 8;
                break;
            case FLOAT8OID:
                *(int *)(buf + pos) = INV_TAG_FLOAT8; pos += 4;
                *(int *)(buf + pos) = 8; pos += 4;
                *(double *)(buf + pos) = *(double *)DatumGetPointer(args[i]); pos += 8;
                break;
            case BOOLOID:
                *(int *)(buf + pos) = INV_TAG_BOOL; pos += 4;
                *(int *)(buf + pos) = 1; pos += 4;
                buf[pos++] = DatumGetChar(args[i]) ? 1 : 0;
                break;
            case TEXTOID:
            case VARCHAROID:
            case LZTEXTOID:
            case JAVAOID:
                {
                    Datum flat = materialize_blob_datum(args[i]);
                    bytea *payload = (bytea *) DatumGetPointer(flat);
                    int slen = VARSIZE(payload) - VARHDRSZ;
                    int tag = (argtype == JAVAOID) ? INV_TAG_JAVA_OBJECT : INV_TAG_VARCHAR;

                    buf = EnsureCap(buf, &capacity, pos, 8 + slen);
                    *(int *)(buf + pos) = tag; pos += 4;
                    *(int *)(buf + pos) = slen; pos += 4;
                    memcpy(buf + pos, VARDATA(payload), slen);
                    pos += slen;
                    if (flat != args[i])
                        pfree(DatumGetPointer(flat));
                }
                break;
            default:
                elog(ERROR, "java argument type %lu is not supported on the FFM path", (unsigned long) argtype);
        }
        count++;
    }

    *(int *)buf = count;
    *outBlock = buf;
    *outLen = pos;
}

static Datum
InvokeJavaViaFFMFromDatums(JavaFunction def, int nargs, Datum *args, bool *isNull)
{
    InvokerFn fn = (InvokerFn) java_ffm_invoker;
    char *argBlock = NULL;
    int argLen = 0;
    char *dynamicResult = NULL;
    int dynamicResultLen = 0;
    int rc;
    Datum result;

    if (fn == NULL)
        elog(ERROR, "FFM Java function invoker is not registered");

    BuildArgBlockFromDatums(def, nargs, args, &argBlock, &argLen);

    rc = CallJavaInvokerWithGrowableResult(
        fn,
        def->className ? def->className : "",
        def->methodName ? def->methodName : "",
        def->methodDesc ? def->methodDesc : "",
        def->isStatic ? 1 : 0,
        argBlock, argLen,
        &dynamicResult, &dynamicResultLen
    );

    pfree(argBlock);

    if (dynamicResult != NULL) {
        result = ParseInvokerResultToDatum(dynamicResult, dynamicResultLen, def->returnType, isNull);
        pfree(dynamicResult);
        return result;
    }

    elog(ERROR, "Java function invocation failed (rc=%d)", rc);
    return PointerGetDatum(NULL);
}

/*
 * Helper: Calls the Java invoker function pointer, managing a growable
 * result buffer so that large serialized Java objects (JAVA_OBJECT results)
 * do not get truncated.
 *
 * Strategy:
 *   - Start with a reasonable stack buffer.
 *   - If the written protocol looks incomplete or we detect that more space
 *     was needed (by inspecting length fields), allocate on the heap and retry.
 *   - The final successful buffer is returned via *resultBufOut (palloc'd).
 */
static int
CallJavaInvokerWithGrowableResult(
    InvokerFn fn,
    const char *cname,
    const char *mname,
    const char *mdesc,
    int isStaticFlag,
    const char *argBlock,
    int argLen,
    char **resultBufOut,
    int  *resultLenOut
)
{
    enum { INITIAL_SIZE = 16384 };   /* 16KB starting point */
    char  stackBuf[INITIAL_SIZE];
    char *currentBuf = stackBuf;
    int   currentSize = INITIAL_SIZE;
    int   rc;
    int   attempts = 0;
    const int MAX_ATTEMPTS = 4;

    while (attempts < MAX_ATTEMPTS) {
        memset(currentBuf, 0, currentSize);

        rc = fn(cname, mname, mdesc, isStaticFlag, argBlock, argLen, currentBuf, currentSize);

        /* Inspect what Java wrote to decide if the buffer was big enough.
         * On failure the invoker still writes TAG_ERROR into this buffer.
         */
        int written = InspectWrittenResultSize(currentBuf, currentSize);
        bool haveError = ResultLooksLikeError(currentBuf, currentSize);

        if (rc != 0 && !(haveError && written <= currentSize)) {
            if (currentBuf != stackBuf) pfree(currentBuf);
            *resultBufOut = NULL;
            *resultLenOut = 0;
            return rc;
        }

        if (written <= currentSize) {
            /* Success - buffer was sufficient */
            if (currentBuf == stackBuf) {
                /* Copy to heap so caller can always pfree */
                char *heapCopy = palloc(written);
                memcpy(heapCopy, currentBuf, written);
                *resultBufOut = heapCopy;
            } else {
                *resultBufOut = currentBuf;
            }
            *resultLenOut = written;
            return rc;
        }

        /* Buffer was too small — grow and retry */
        int newSize = written + 4096;   /* give a little extra */
        if (newSize < currentSize * 2) newSize = currentSize * 2;

        if (currentBuf != stackBuf) {
            pfree(currentBuf);
        }
        currentBuf = palloc(newSize);
        currentSize = newSize;
        attempts++;
    }

    /* Exhausted retries */
    if (currentBuf != stackBuf) pfree(currentBuf);
    *resultBufOut = NULL;
    *resultLenOut = 0;
    return -2;   /* Buffer too small even after growth */
}

/*
 * Heuristic to determine how many bytes the Java side actually wanted to write.
 * Looks at the protocol: first 4 bytes = num values, then first value's tag+len.
 * Returns the total bytes that appear to be required for a complete result.
 */
static int
InspectWrittenResultSize(const char *buf, int bufSize)
{
    if (bufSize < 4) return bufSize + 1;

    int num = *(int*)buf;
    if (num <= 0) return 4;

    long off = 4;
    int totalNeeded = 4;

    for (int i = 0; i < num && off + 8 <= bufSize; i++) {
        int tag = *(int*)(buf + off); off += 4;
        int vlen = *(int*)(buf + off); off += 4;
        totalNeeded = off + vlen;

        if (off + vlen > bufSize) {
            /* Java probably wanted to write more */
            return totalNeeded + 1024;
        }
        off += vlen;
    }

    return totalNeeded;
}

static int
ResultLooksLikeError(const char *buf, int bufSize)
{
    if (bufSize < 12)
        return 0;
    return *(int *)buf == 1 && *(int *)(buf + 4) == INV_TAG_ERROR;
}

/*
 * Parse the result block written by the Java invoker into a SQL Datum.
 * TAG_ERROR becomes elog(ERROR) with the Java exception text.
 */
static Datum
ParseInvokerResultToDatum(const char *buf, int bufLen, Oid returnType, bool *isNull)
{
    long offset;
    int tag;
    int vlen;

    if (isNull)
        *isNull = false;

    if (bufLen < 4)
        elog(ERROR, "Java function returned an empty result");

    if (*(int *)buf <= 0) {
        if (isNull)
            *isNull = true;
        return PointerGetDatum(NULL);
    }

    offset = 4;
    if (offset + 8 > bufLen)
        elog(ERROR, "Java function returned a truncated result");

    tag = *(int *)(buf + offset); offset += 4;
    vlen = *(int *)(buf + offset); offset += 4;

    if (tag != INV_TAG_NULL && tag != INV_TAG_ERROR && offset + vlen > bufLen)
        elog(ERROR, "Java function returned a truncated result");

    switch (tag) {
        case INV_TAG_INT4:
            return Int32GetDatum(*(int *)(buf + offset));
        case INV_TAG_INT8:
            {
                void *data = palloc(8);
                memcpy(data, buf + offset, 8);
                return PointerGetDatum(data);
            }
        case INV_TAG_FLOAT8:
            {
                void *data = palloc(8);
                memcpy(data, buf + offset, 8);
                return PointerGetDatum(data);
            }
        case INV_TAG_BOOL:
            return CharGetDatum(*(buf + offset) != 0);
        case INV_TAG_TEXT:
        case INV_TAG_VARCHAR:
            {
                bytea *string = palloc(vlen + VARHDRSZ + 1);
                SETVARSIZE(string, vlen + VARHDRSZ);
                memcpy(VARDATA(string), buf + offset, vlen);
                VARDATA(string)[vlen] = '\0';
                return PointerGetDatum(string);
            }
        case INV_TAG_JAVA_OBJECT:
            {
                bytea *ser = (bytea *) palloc(vlen + VARHDRSZ);
                SETVARSIZE(ser, vlen + VARHDRSZ);
                memcpy(VARDATA(ser), buf + offset, vlen);
                return PointerGetDatum(ser);
            }
        case INV_TAG_ERROR:
            {
                if (vlen > 0) {
                    char *msg = palloc(vlen + 1);
                    memcpy(msg, buf + offset, vlen);
                    msg[vlen] = '\0';
                    elog(ERROR, "Java function error: %s", msg);
                }
                elog(ERROR, "Java function error (no message)");
            }
            break;
        case INV_TAG_NULL:
            if (isNull)
                *isNull = true;
            return PointerGetDatum(NULL);
        default:
            elog(ERROR, "Java function returned unsupported result tag %d (sql type %lu)", tag, (unsigned long) returnType);
    }

    return PointerGetDatum(NULL);
}

void SetJavaObjectLoader(const char* l) {
    JNIEnv* jenv;

    GetJavaEnv(&jenv);
    
    if ( l != NULL ) loader = strdup(l);
    if (loader_class != NULL) {
        (*jenv)->DeleteGlobalRef(jenv, loader_class);
    }

    loader_class = (*jenv)->NewGlobalRef(jenv, (*jenv)->FindClass(jenv, loader));
    loader_out = (*jenv)->GetStaticMethodID(jenv, loader_class, "java_out", "([B)Ljava/lang/Object;");
    loader_in = (*jenv)->GetStaticMethodID(jenv, loader_class, "java_in", "(Ljava/lang/Object;)[B");
    loader_text_in = (*jenv)->GetStaticMethodID(jenv, loader_class, "java_text_in", "(Ljava/lang/String;)[B");
    loader_text_out = (*jenv)->GetStaticMethodID(jenv, loader_class, "java_text_out", "([B)Ljava/lang/String;");
    loader_compare = (*jenv)->GetStaticMethodID(jenv, loader_class, "java_compare", "([B[B)I");
    loader_equals = (*jenv)->GetStaticMethodID(jenv, loader_class, "java_equals", "([B[B)Z");
}

int
GetJavaEnv(JNIEnv** env)
{
        jint result;
        
        result = (*jvm)->GetEnv(jvm, (void**)env, JNI_VERSION_1_8);
        if (result == JNI_EDETACHED) {
            result = (*jvm)->AttachCurrentThread(jvm, (void**)env, NULL);
            pthread_setspecific(attaches, *env);
        }

	if (result != JNI_OK) {
		elog(FATAL, "Java environment not attached");
	}
	return result;

}

jobject
javaout(bytea * datum)
{
	JNIEnv         *jenv;
	int             length;
	char           *data;
	jbyteArray      jb = NULL;
	jobject         result;
        Datum pipe;

        if (datum == NULL) {
            return NULL;
        } else {
            length = VARSIZE(datum) - VARHDRSZ;
            data = VARDATA(datum);
        }

	GetJavaEnv(&jenv);

        if ( ISINDIRECT(datum) ) {
            int len = 0;
            length = sizeof_indirect_blob(PointerGetDatum(datum));
            data = palloc(length);
            pipe = open_read_pipeline_blob(PointerGetDatum(datum),true);
            while ( read_pipeline_segment_blob(pipe,data,&len,sizeof_max_tuple_blob()) ) {
                data += len;
            }
            close_read_pipeline_blob(pipe);
        } 

	jb = (*jenv)->NewByteArray(jenv, length);
	if (jb != NULL) {
		(*jenv)->SetByteArrayRegion(jenv, jb, 0, length, (jbyte *) data);
	} else {
		elog(ERROR, "java memory error");
	}

        if ( ISINDIRECT(datum) ) {
            pfree(data);
        }

	result = (*jenv)->CallStaticObjectMethod(jenv, loader_class, loader_out, jb);

	if (result == NULL || (*jenv)->ExceptionCheck(jenv)) {
		elog(ERROR, "javaout: embedded exception occurred");
	}
	return result;
}


bytea          *
javain(jobject target)
{
	JNIEnv         *jenv;
	int             length;
	bytea          *data;
	jbyteArray      jb = NULL;

	GetJavaEnv(&jenv);

	jb = (*jenv)->CallStaticObjectMethod(jenv, loader_class, loader_in, target);
	if (jb == NULL || (*jenv)->ExceptionCheck(jenv)) {
		elog(ERROR, "javain: embedded exception occurred");
	}
	length = (*jenv)->GetArrayLength(jenv, jb);
	data = (bytea *) palloc(length + VARHDRSZ);

	(*jenv)->GetByteArrayRegion(jenv, jb, 0, length, (signed char *) VARDATA(data));
	SETVARSIZE(data, length + VARHDRSZ);

	return data;
}

bytea          *
javatextin(char *target)
{
	void           *env;
	JNIEnv         *jenv;
	int             length;
	bytea          *data;
	jbyteArray      jb = NULL;
	jbyte          *prim = NULL;

	GetJavaEnv(&jenv);

	(*jenv)->PushLocalFrame(jenv, 10);

	jb = (*jenv)->CallStaticObjectMethod(jenv, loader_class, loader_text_in, (*jenv)->NewStringUTF(jenv, target));
	if (jb == NULL || (*jenv)->ExceptionCheck(jenv)) {
		(*jenv)->PopLocalFrame(jenv, NULL);
		elog(ERROR, "javatextin: embedded exception occurred");
	}
	length = (*jenv)->GetArrayLength(jenv, jb);
	data = (bytea *) palloc(length + VARHDRSZ);

	(*jenv)->GetByteArrayRegion(jenv, jb, 0, length, (signed char *) VARDATA(data));
	SETVARSIZE(data, length + VARHDRSZ);
	(*jenv)->PopLocalFrame(jenv, NULL);

	return data;
}



char           *
javatextout(bytea * target)
{
	JNIEnv         *jenv;
	int             length = VARSIZE(target) - VARHDRSZ;
	char           *data = VARDATA(target);
	jbyteArray      jb = NULL;
	jbyte          *prim = NULL;
	jstring         result;

	GetJavaEnv(&jenv);

	jb = (*jenv)->NewByteArray(jenv, length);
	if (jb != NULL) {
		(*jenv)->SetByteArrayRegion(jenv, jb, 0, length, (jbyte *) data);
	} else {
		(*jenv)->PopLocalFrame(jenv, NULL);
		elog(ERROR, "java memory error");
	}

	result = (*jenv)->CallStaticObjectMethod(jenv, loader_class, loader_text_out, jb);

	if (result == NULL || (*jenv)->ExceptionCheck(jenv)) {
		(*jenv)->PopLocalFrame(jenv, NULL);
		elog(ERROR, "javatextout: embedded exception occurred");
	}
	length = (*jenv)->GetStringUTFLength(jenv, result);
	data = palloc(length + 1);

	(*jenv)->GetStringUTFRegion(jenv, result, 0, length, data);
	data[length] = 0x00;
	(*jenv)->PopLocalFrame(jenv, NULL);
	return data;
}

Datum
fmgr_javaA(const char* function, int nargs, Oid* types, Datum *args, Oid* returnType, bool* isNull)
{
	JNIEnv         *jenv;
	jvalue          rval;
        jvalue        jargs[FUNC_MAX_ARGS];
	Datum           ret_datum;
        int x=0;
        JavaFunction def = GetJavaCallArgs(function, nargs, types);

        if (returnType != NULL) {
            *returnType = def->returnType;
        }

        if (java_ffm_invoker != NULL) {
            return InvokeJavaViaFFMFromDatums(def, nargs, args, isNull);
        }

	GetJavaEnv(&jenv);

	(*jenv)->PushLocalFrame(jenv, 10);

        for (x=0;x<nargs;x++) {
            jargs[x] = ConvertToJavaArg(def->argTypes[x], args[x]);
        }

        rval = CallJavaFunction(def, nargs, jargs);

	if ((*jenv)->ExceptionCheck(jenv)) {
		elog(ERROR, "fmgr_javaA: embedded exception occurred");
	}
	ret_datum = ConvertFromJavaArg(def->returnType, rval, isNull);
	(*jenv)->PopLocalFrame(jenv, NULL);

	return ret_datum;
}

Datum
fmgr_cached_javaA(JavaFunction jinfo, int nargs, Datum *args, Oid* returnType, bool* isNull)
{
	JNIEnv         *jenv;
	jvalue          rval;
	Datum           ret_datum;
        jvalue        jargs[FUNC_MAX_ARGS];
        int x;

        if (returnType != NULL) {
            *returnType = jinfo->returnType;
        }

        if (java_ffm_invoker != NULL) {
            return InvokeJavaViaFFMFromDatums(jinfo, nargs, args, isNull);
        }

        GetJavaEnv(&jenv);

	(*jenv)->PushLocalFrame(jenv, 10);
        
        for (x=0;x<nargs;x++) {
            jargs[x] = ConvertToJavaArg(jinfo->argTypes[x], args[x]);
        }

        rval = CallJavaFunction(jinfo,  nargs, jargs);

	if ((*jenv)->ExceptionCheck(jenv)) {
		elog(ERROR, "fmgr_cached_javaA: embedded exception occurred");
	}

	ret_datum = ConvertFromJavaArg(jinfo->returnType, rval,isNull);

	(*jenv)->PopLocalFrame(jenv, NULL);

	return ret_datum;

}

jvalue
ConvertToJavaArg(Oid type, Datum val)
{
	JNIEnv         *jenv;
	jvalue          rval;


	GetJavaEnv(&jenv);

	switch (type) {
	case INT4OID:
		rval.i = DatumGetInt32(val);
		break;
        case TEXTOID:
	case VARCHAROID:
		{
			bytea          *strvar = (bytea *) val;
                        int            len = VARSIZE(strvar) - VARHDRSZ;
			char           *string = palloc(len + 1);
			memmove(string, VARDATA(strvar), len);
			string[len] = 0x00;
			rval.l = (*jenv)->NewStringUTF(jenv, string);
			pfree(string);
			break;
		}
	case FLOAT8OID:
                rval.d = *(double*)PointerGetDatum(val);
                break;
        case INT8OID:
                rval.j = PointerGetDatum(val);
                break;
        case BOOLOID:
		rval.z = DatumGetChar(val);
		break;
	case JAVAOID:
		rval.l = javaout((bytea*)DatumGetPointer(val));
		break;
	default:
		rval.i = 0;
		elog(ERROR, "java argument not valid");

	}

	return rval;
}

Datum
ConvertFromJavaArg(Oid type, jvalue val, bool *isNull)
{
	JNIEnv         *jenv;
	Datum           ret_datum = PointerGetDatum(NULL);

	GetJavaEnv(&jenv);
        
	switch (type) {
            case INT4OID:
                    ret_datum = Int32GetDatum(val.i);
                    break;
            case TEXTOID:
            case VARCHAROID:
                    {
                            jstring         strvar = (jstring) val.l;
                            int             len = (*jenv)->GetStringUTFLength(jenv, strvar);
                            bytea          *string = palloc(len + VARHDRSZ + 1);

                            SETVARSIZE(string, len + VARHDRSZ);
                            (*jenv)->GetStringUTFRegion(jenv, strvar, 0, len, VARDATA(string));
                            VARDATA(string)[len] = '\0';
                            ret_datum = PointerGetDatum(string);
                            break;
                    }
            case BOOLOID:
                    ret_datum = CharGetDatum(val.z);
                    break;
            case FLOAT8OID:
                {
                    void* data = palloc(8);
                    memcpy(data,&val.d,8);
                    ret_datum = PointerGetDatum(data);
                    break;
                }
            case INT8OID:
                {
                    void* data = palloc(8);
                    memcpy(data,&val.j,8);
                    ret_datum = PointerGetDatum(data);
                    break;
                }
            case JAVAOID:
                {
                    if ((*jenv)->IsSameObject(jenv,val.l,NULL)) {
                        *isNull = true;
                        ret_datum = PointerGetDatum(NULL);
                    } else {
                        ret_datum = PointerGetDatum(javain(val.l));
                    }
                    break;
                }
            default:
                    elog(ERROR, "java argument not valid");

	}


	return ret_datum;
}

bool
java_instanceof(bytea * object, bytea * class)
{
	JNIEnv         *jenv;
	jclass          converter;
	bool            ret_val;
	char*           replace;
	char*           clazz;
        jobject         target;

	GetJavaEnv(&jenv);
	(*jenv)->PushLocalFrame(jenv, 10);

	target = javaout(object);

        clazz = textout(class);
        
        replace = clazz;
	while (replace) {
		replace = strchr(replace, '.');
		if (replace != NULL)
			*replace = '/';
	}

	converter = (*jenv)->FindClass(jenv, clazz);

        pfree(clazz);

	if ((*jenv)->ExceptionCheck(jenv)) {
		(*jenv)->PopLocalFrame(jenv, NULL);
		elog(ERROR, "java_instanceof: embedded exception while trying to check java objects");
	}
	ret_val = (*jenv)->IsInstanceOf(jenv, target, converter);

	(*jenv)->PopLocalFrame(jenv, NULL);

	return ret_val;
}

int32
java_compare(bytea * obj1, bytea * obj2)
{
	JNIEnv         *jenv;
	jmethodID       in;
	jint            result = 0;
	jbyteArray      master1 = NULL;
	jbyteArray      master2 = NULL;

	GetJavaEnv(&jenv);
	(*jenv)->PushLocalFrame(jenv, 10);

	if ((*jenv)->ExceptionOccurred(jenv)) {
		(*jenv)->ExceptionClear(jenv);
		(*jenv)->PopLocalFrame(jenv, NULL);
		elog(ERROR, "could not find method java_compare method in java object loader");
	}
	master1 = (*jenv)->NewByteArray(jenv, VARSIZE(obj1) - VARHDRSZ);
	if (master1 != NULL) {
		(*jenv)->SetByteArrayRegion(jenv, master1, 0, VARSIZE(obj1) - VARHDRSZ, (jbyte *) VARDATA(obj1));
	} else {
		(*jenv)->PopLocalFrame(jenv, NULL);
		elog(ERROR, "java memory error in compare 1");
	}

	master2 = (*jenv)->NewByteArray(jenv, VARSIZE(obj2) - VARHDRSZ);
	if (master2 != NULL) {
		(*jenv)->SetByteArrayRegion(jenv, master2, 0, VARSIZE(obj2) - VARHDRSZ, (jbyte *) VARDATA(obj2));
	} else {
		(*jenv)->PopLocalFrame(jenv, NULL);
		elog(ERROR, "java memory error in compare 2");
	}

	result = (*jenv)->CallStaticIntMethod(jenv, loader_class, loader_compare, master1, master2);

	if ((*jenv)->ExceptionCheck(jenv)) {
		(*jenv)->PopLocalFrame(jenv, NULL);
		elog(ERROR, "java_compare: embedded exception while trying to compare java objects");
	}
	(*jenv)->PopLocalFrame(jenv, NULL);
	return result;
}

bool
java_equals(bytea * obj1, bytea * obj2)
{
	JNIEnv         *jenv;
	jboolean        result = 0;
	jbyteArray      master1 = NULL;
	jbyteArray      master2 = NULL;

	GetJavaEnv(&jenv);
	(*jenv)->PushLocalFrame(jenv, 10);

        if (obj1 == obj2) return true;
        if (obj1 == NULL && obj2 != NULL) return false;
        if (obj1 != NULL && obj2 == NULL) return false;

	master1 = (*jenv)->NewByteArray(jenv, VARSIZE(obj1) - VARHDRSZ);
	if (master1 != NULL) {
		(*jenv)->SetByteArrayRegion(jenv, master1, 0, VARSIZE(obj1) - VARHDRSZ, (jbyte *) VARDATA(obj1));
	} else {
		(*jenv)->PopLocalFrame(jenv, NULL);
		elog(ERROR, "java memory error");
	}

	master2 = (*jenv)->NewByteArray(jenv, VARSIZE(obj2) - VARHDRSZ);
	if (master2 != NULL) {
		(*jenv)->SetByteArrayRegion(jenv, master2, 0, VARSIZE(obj2) - VARHDRSZ, (jbyte *) VARDATA(obj2));
	} else {
		(*jenv)->PopLocalFrame(jenv, NULL);
		elog(ERROR, "java memory error");
	}

	result = (*jenv)->CallStaticBooleanMethod(jenv, loader_class, loader_equals, master1, master2);

	if ((*jenv)->ExceptionCheck(jenv)) {
		(*jenv)->PopLocalFrame(jenv, NULL);
		elog(ERROR, "java_equals: embedded exception while trying to compare java objects");
	}
	(*jenv)->PopLocalFrame(jenv, NULL);
	return result;
}

bool
java_noteq(bytea * obj1, bytea * obj2)
{
	return !java_equals(obj1, obj2);
}

PG_EXTERN       bool
java_gt(bytea * obj1, bytea * obj2)
{
	if (java_compare(obj1, obj2) > 0)
		return true;
	else
		return false;
}

PG_EXTERN       bool
java_lt(bytea * obj1, bytea * obj2)
{
	if (java_compare(obj1, obj2) < 0)
		return true;
	else
		return false;
}


PG_EXTERN       bool
java_lteq(bytea * obj1, bytea * obj2)
{
	if (java_compare(obj1, obj2) <= 0)
		return true;
	else
		return false;
}


PG_EXTERN       bool
java_gteq(bytea * obj1, bytea * obj2)
{
	if (java_compare(obj1, obj2) >= 0)
		return true;
	else
		return false;
}

PG_EXTERN int
javalen(bytea * obj)
{
	return VARSIZE(obj) - VARHDRSZ;
}

Oid GetJavaReturnType(JavaFunction function) {
    return function->returnType;
}

void FormJavaFunctionSig(char* buffer, int buflen, const char *name, int nargs, Oid * types) {
        char                 args[128];
        const char*           argformat = "%ld,";

        int x;
        char*   insert = args;

        memset(args, '\0', 128);

        for (x=0;x<nargs;x++) {
            sprintf(insert, argformat, types[x]);
            insert = strchr(insert, '\0');
        }
        if (*(--insert) == ',') *insert = '\0';

        snprintf(buffer, buflen, "%s(%s)", name, args);
}

JavaFunction
GetJavaFunction(const char *name, int nargs, Oid * types)
{
    return GetJavaCallArgs(name, nargs, types);
}

JavaFunction
GetJavaCallArgs(const char *name, int nargs, Oid * argtypes)
{
	JNIEnv         *jenv = NULL;
        bool hfound = false;
        JavaFunction            definition = NULL;            
        NameData             lookup;

        FunctionCacheInit();

        memset(NameStr(lookup), '\0', NAMEDATALEN);

        FormJavaFunctionSig(NameStr(lookup), NAMEDATALEN, name, nargs, argtypes);

        pthread_mutex_lock(&ftable_guard);
        definition = hash_search(function_table, NameStr(lookup), HASH_FIND, &hfound);
        pthread_mutex_unlock(&ftable_guard);
        if (definition == NULL) {
            HeapTuple func = SearchSysCacheTuple(PROCNAME, PointerGetDatum(name),
                   Int32GetDatum(nargs), PointerGetDatum(argtypes), 0);

            if (HeapTupleIsValid(func)) {
                char* mark;
                pthread_mutex_lock(&ftable_guard);
                definition = hash_search(function_table, NameStr(lookup), HASH_ENTER, &hfound);

                if (!hfound) {
                    Datum           cla = SysCacheGetAttr(PROCNAME, func, Anum_pg_proc_prosrc, NULL);
                    const char*  javasrc = textout((text *) cla);
                    Datum           sig = SysCacheGetAttr(PROCNAME, func, Anum_pg_proc_probin, NULL);
                    const char*  javasig = textout((text *) sig);
                    const char*  javaname;

                    mark = strchr(javasrc,'.');
                    *mark = '\0';
                    javaname = mark + 1;

                    definition->returnType = DatumGetObjectId(SysCacheGetAttr(PROCNAME, func, Anum_pg_proc_prorettype, NULL));
                    definition->nargs = nargs;
                    memmove(definition->argTypes,argtypes, sizeof(Oid) * nargs);

                    /* Store original strings for FFM upcall invoker path */
                    MemoryContext oldcxt = MemoryContextSwitchTo(function_cache_cxt);
                    definition->className  = pstrdup(javasrc);
                    definition->methodName = pstrdup(javaname);
                    definition->methodDesc = pstrdup(javasig);
                    MemoryContextSwitchTo(oldcxt);

                    {
                        int dparams = CountJvmDescriptorParams(javasig);
                        definition->isStatic = (dparams < 0) ? true : (nargs == dparams);
                    }

                    if (java_ffm_invoker == NULL) {
                        GetJavaEnv(&jenv);
                        definition->clazz = (*jenv)->NewGlobalRef(jenv, (*jenv)->FindClass(jenv, javasrc));
                        definition->method = (*jenv)->GetStaticMethodID(jenv, definition->clazz, javaname, javasig);
                        definition->isStatic = true;
                        if (definition->method == NULL) {
                            if ((*jenv)->ExceptionCheck(jenv)) {
                                (*jenv)->ExceptionClear(jenv);
                            }
                            definition->method = (*jenv)->GetMethodID(jenv, definition->clazz, javaname, javasig);
                            definition->isStatic = false;
                        }
                    } else {
                        definition->clazz = NULL;
                        definition->method = NULL;
                    }

                    pfree((void*)javasrc);
                    pfree((void*)javasig);
                }
                pthread_mutex_unlock(&ftable_guard);
            }
        }

        if (definition == NULL) {
            elog(ERROR, "Java function %s definition not found", NameStr(lookup));
        }
        
        return definition;
}

jvalue
CallJavaFunction(JavaFunction def, int nargs, jvalue* args) {
        jvalue         rval;
        jobject        target = NULL;

	JNIEnv         *jenv;
        GetJavaEnv(&jenv);
        if (!def->isStatic) {
            target = args[0].l;
            args = args + 1;
        }

        switch (def->returnType) {
            case JAVAOID:
            case TEXTOID:
            case VARCHAROID:
                    rval.l = def->isStatic ? 
                            (*jenv)->CallStaticObjectMethodA(jenv, def->clazz, def->method, args)
                            : (*jenv)->CallObjectMethodA(jenv, target, def->method, args);
                    break;
            case BOOLOID:
                    rval.z = def->isStatic ? 
                        (*jenv)->CallStaticBooleanMethodA(jenv, def->clazz, def->method, args)
                        : (*jenv)->CallBooleanMethodA(jenv, target, def->method, args);
                    break;
            case INT4OID:
                    rval.i = def->isStatic ?
                        (*jenv)->CallStaticIntMethodA(jenv, def->clazz, def->method, args)
                        : (*jenv)->CallIntMethodA(jenv, target, def->method, args);
                    break;
            case INT8OID:
                    rval.j = def->isStatic ?
                        (*jenv)->CallStaticLongMethodA(jenv, def->clazz, def->method, args)
                        : (*jenv)->CallLongMethodA(jenv, target, def->method, args);
                    break;                
            case FLOAT8OID:
                    rval.d = def->isStatic ?
                        (*jenv)->CallStaticDoubleMethodA(jenv, def->clazz, def->method, args)
                        : (*jenv)->CallDoubleMethodA(jenv, target, def->method,args);
                    break;
            default:
                    rval.l = def->isStatic ?
                        (*jenv)->CallStaticObjectMethodA(jenv, def->clazz, def->method,  args)
                        : (*jenv)->CallObjectMethodA(jenv, target, def->method, args);
                    break;
        }
        return rval;
}

bool
convert_java_to_scalar(Datum value,double* scaledval,Datum lobound,double* scaledlo,Datum hibound,double* scaledhi, Datum histogram) {
    if ( scaledlo ) {
        *scaledlo = 0.0;
    }
    if ( scaledhi ) {
        *scaledhi = 1.0;
    }
    if ( scaledval ) {
        *scaledval = 0.5;
    }   
/*  these values are bogus for now so return false  */
    return false;
}
