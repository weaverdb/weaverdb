/*-------------------------------------------------------------------------
 *
 *
 * IDENTIFICATION
 *	  $Header: /cvs/weaver/mtpgsql/src/backend/utils/adt/java.c,v 1.2 2006/10/09 15:17:41 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */
#include <ctype.h>
#include <math.h>
#include <sys/types.h>
#include <errno.h>
#include <jni.h>
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
static JNIEnv  *GetJavaEnv(void);

JavaVM         *jvm;
static const char* loader = "driver/weaver/WeaverObjectLoader";

static MemoryContext function_cache_cxt;
static HTAB*  function_table;
static pthread_mutex_t   ftable_guard;

static jclass loader_class;
static jmethodID loader_out;
static jmethodID loader_in;
static jmethodID loader_text_in;
static jmethodID loader_text_out;
static jmethodID loader_compare;
static jmethodID loader_equals;

static jclass class_class;
static jmethodID getname;

typedef struct funcdef {
    NameData     key;
    jclass       clazz;
    jmethodID    method;
    int          nargs;
    Oid         argTypes[FUNC_MAX_ARGS];
    Oid          returnType;
    bool         isStatic;
} FuncDef;

static jvalue CallJavaFunction(JavaFunction def, jobject target, int nargs, jvalue* args);
static jvalue ConvertToJavaArg(Oid type, Datum val);
static void FormJavaFunctionSig(char* buffer, int buflen, const char* clazz, const char *name, int nargs, Oid * types);

static void FunctionCacheInit() {
    HASHCTL ctl;
    JNIEnv* jenv = GetJavaEnv();

    
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

    class_class = (*jenv)->NewGlobalRef(jenv, (*jenv)->FindClass(jenv, "java/lang/Class"));
    getname = (*jenv)->GetMethodID(jenv, class_class, "descriptorString", "()Ljava/lang/String;");
}

void
SetJVM(JavaVM * java, const char *ol)
{
	jvm = java;

        FunctionCacheInit();
	SetJavaObjectLoader(ol);
}

void SetJavaObjectLoader(const char* l) {
    JNIEnv* jenv = GetJavaEnv();
    
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

JNIEnv*
GetJavaEnv()
{
	JNIEnv         *jenv;


        (*jvm)->AttachCurrentThread(jvm, (void*)&jenv, NULL);

	if (jenv == NULL) {
		elog(FATAL, "Java environment not attached");
	}
	return jenv;

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

	jenv = GetJavaEnv();

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

	if (result == NULL || (*jenv)->ExceptionOccurred(jenv)) {
		(*jenv)->ExceptionClear(jenv);
		elog(ERROR, "embedded exception occurred");
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

	jenv = GetJavaEnv();

	jb = (*jenv)->CallStaticObjectMethod(jenv, loader_class, loader_in, target);
	if (jb == NULL || (*jenv)->ExceptionOccurred(jenv)) {
		(*jenv)->ExceptionClear(jenv);
		elog(ERROR, "embedded exception occurred");
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
	int             result;
	int             length;
	bytea          *data;
	jbyteArray      jb = NULL;
	jbyte          *prim = NULL;

	jenv = GetJavaEnv();

	(*jenv)->PushLocalFrame(jenv, 10);

	jb = (*jenv)->CallStaticObjectMethod(jenv, loader_class, loader_text_in, (*jenv)->NewStringUTF(jenv, target));
	if (jb == NULL || (*jenv)->ExceptionOccurred(jenv)) {
		(*jenv)->ExceptionClear(jenv);
		(*jenv)->PopLocalFrame(jenv, NULL);
		elog(ERROR, "embedded exception occurred");
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

	jenv = GetJavaEnv();

	jb = (*jenv)->NewByteArray(jenv, length);
	if (jb != NULL) {
		(*jenv)->SetByteArrayRegion(jenv, jb, 0, length, (jbyte *) data);
	} else {
		(*jenv)->PopLocalFrame(jenv, NULL);
		elog(ERROR, "java memory error");
	}

	result = (*jenv)->CallStaticObjectMethod(jenv, loader_class, loader_text_out, jb);

	if (result == NULL || (*jenv)->ExceptionOccurred(jenv)) {
		(*jenv)->ExceptionClear(jenv);
		(*jenv)->PopLocalFrame(jenv, NULL);
		elog(ERROR, "embedded exception occurred");
	}
	length = (*jenv)->GetStringUTFLength(jenv, result);
	data = palloc(length + 1);

	(*jenv)->GetStringUTFRegion(jenv, result, 0, length, data);
	data[length] = 0x00;
	(*jenv)->PopLocalFrame(jenv, NULL);
	return data;
}

Datum
fmgr_javaA(Datum target, const char* function, int nargs, Oid* types, Datum *args, Oid* returnType, bool* isNull)
{
	JNIEnv         *jenv;
	jvalue          rval;
        jvalue        jargs[FUNC_MAX_ARGS];
	Datum           ret_datum;
        Oid foid;
        Oid rtype;
        const char            *clazz;
        const char            *method;
        const char            *sig;
        int x=0;
                
	jenv = GetJavaEnv();

	(*jenv)->PushLocalFrame(jenv, 10);


        jobject jtar = (jobject) javaout((bytea *) DatumGetPointer(target));

        JavaFunction def = GetJavaCallArgs(jtar, function, nargs, types);

        for (x=0;x<nargs;x++) {
            jargs[x] = ConvertToJavaArg(def->argTypes[x], args[x]);
        }

        rval = CallJavaFunction(def, jtar, nargs, jargs);

	if ((*jenv)->ExceptionOccurred(jenv)) {
		(*jenv)->ExceptionClear(jenv);
		elog(ERROR, "embedded exception occurred");
	}
	ret_datum = ConvertFromJavaArg(def->returnType, rval, isNull);
	(*jenv)->PopLocalFrame(jenv, NULL);

        if (returnType != NULL) {
            *returnType = def->returnType;
        }

	return ret_datum;
}

Datum
fmgr_cached_javaA(JavaFunction jinfo, int nargs, Datum *args, Oid* returnType, bool* isNull)
{
	JNIEnv         *jenv = GetJavaEnv();
	jclass          converter;
	jmethodID       in;
	jvalue          rval;
	Datum           ret_datum;
        jvalue        jargs[FUNC_MAX_ARGS];
        Oid foid;
        int x;

	(*jenv)->PushLocalFrame(jenv, 10);
        
        for (x=0;x<nargs;x++) {
            jargs[x] = ConvertToJavaArg(jinfo->argTypes[x], args[x]);
        }

        rval = CallJavaFunction(jinfo,  NULL, nargs, jargs);

	if ((*jenv)->ExceptionOccurred(jenv)) {
		(*jenv)->ExceptionClear(jenv);
		elog(ERROR, "embedded exception occurred");
	}

	ret_datum = ConvertFromJavaArg(jinfo->returnType, rval,isNull);

	(*jenv)->PopLocalFrame(jenv, NULL);

        if (returnType != NULL) {
            *returnType = jinfo->returnType;
        }

	return ret_datum;

}

jvalue
ConvertToJavaArg(Oid type, Datum val)
{
	JNIEnv         *jenv;
	jvalue          rval;


	jenv = GetJavaEnv();

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
        case JAVARESULTOID:
                rval.l = NULL;
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


	jenv = GetJavaEnv();
        
        if ( (*jenv)->IsSameObject(jenv,val.l,NULL) ) 
        {
            *isNull = true;
            return PointerGetDatum(NULL);
        }

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
		ret_datum = PointerGetDatum(javain(val.l));
		break;
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
	char           *replace;

	jenv = GetJavaEnv();
	(*jenv)->PushLocalFrame(jenv, 10);

	jobject         target = javaout(object);

	replace = (char *) VARDATA(class);
	while (replace != NULL) {
		replace = strchr(replace, '.');
		if (replace != NULL)
			*replace = '/';
	}

	converter = (*jenv)->FindClass(jenv, VARDATA(class));

	if ((*jenv)->ExceptionOccurred(jenv)) {
		(*jenv)->ExceptionClear(jenv);
		(*jenv)->PopLocalFrame(jenv, NULL);
		elog(ERROR, "embedded exception while trying to cehck java objects");
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

	jenv = GetJavaEnv();
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

	if ((*jenv)->ExceptionOccurred(jenv)) {
		(*jenv)->ExceptionClear(jenv);
		(*jenv)->PopLocalFrame(jenv, NULL);
		elog(ERROR, "embedded exception while trying to compare java objects");
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

	jenv = GetJavaEnv();
	(*jenv)->PushLocalFrame(jenv, 10);

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

	if ((*jenv)->ExceptionOccurred(jenv)) {
		(*jenv)->ExceptionClear(jenv);
		(*jenv)->PopLocalFrame(jenv, NULL);
		elog(ERROR, "embedded exception while trying to compare java objects");
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

PG_EXTERN Datum 
java_convert(jobject target, Oid type) 
{
    jvalue val;
    bool isNull;
    val.l = target;
    return ConvertFromJavaArg(type, val, &isNull);
}

void FormJavaFunctionSig(char* buffer, int buflen, const char* clazz, const char *name, int nargs, Oid * types) {
        char                 args[128];
        const char*           argformat = "%ld,";

        int x;
        char*   insert = args;

        memset(args, '\0', 128);

        for (x=0;x<nargs;x++) {
            sprintf(insert, argformat, types[x]);
            insert = index(insert, '\0');
        }
        if (*(--insert) == ',') *insert = '\0';

        if (clazz == NULL) {
            snprintf(buffer, buflen, "%s(%s)", name, args);
        } else {
            snprintf(buffer, buflen, "%s.%s(%s)", clazz, name, args);
        }
}

JavaFunction
GetJavaCallArgs(jobject target, const char *name, int nargs, Oid * types)
{
	JNIEnv         *jenv;
	jclass          converter = NULL;
	jstring         classid;
        bool hfound = false;
        char                 buffer[128];
        JavaFunction            definition = NULL;            
        NameData             lookup;

	jenv = GetJavaEnv();

	if (!(*jenv)->IsSameObject(jenv, target, NULL)) {                
                (*jenv)->PushLocalFrame(jenv, 10);
		
		converter = (*jenv)->GetObjectClass(jenv, target);

		while (converter != NULL && definition == NULL) {
                    jsize           len;

                    memset(NameStr(lookup), '\0', NAMEDATALEN);
                    classid = (*jenv)->CallObjectMethod(jenv, converter, getname);
                    len = (*jenv)->GetStringUTFLength(jenv, classid);
                    (*jenv)->GetStringUTFRegion(jenv, classid, 1, len - 2, buffer);
                    buffer[len - 1] = '\0';

                    FormJavaFunctionSig(NameStr(lookup), NAMEDATALEN, buffer, name, nargs, types);

                    pthread_mutex_lock(&ftable_guard);
                    definition = hash_search(function_table, NameStr(lookup), HASH_FIND, &hfound);
                    pthread_mutex_unlock(&ftable_guard);
                    if (definition == NULL) {
                        NameData funcName;

                        snprintf(NameStr(funcName), NAMEDATALEN, "%s.%s", buffer, name);
                        HeapTuple func = SearchSysCacheTuple(PROCNAME, NameGetDatum(&funcName),
                                Int32GetDatum(nargs), PointerGetDatum(types), 0);

                        if (HeapTupleIsValid(func)) {
                            char* mark;
                            pthread_mutex_lock(&ftable_guard);
                            definition = hash_search(function_table, NameStr(lookup), HASH_ENTER, &hfound);

                            if (!hfound) {
                                Datum           cla = SysCacheGetAttr(PROCNAME, func, Anum_pg_proc_prosrc, NULL);
                                const char*           javasrc = textout((text *) cla);
                                Datum           sig = SysCacheGetAttr(PROCNAME, func, Anum_pg_proc_probin, NULL);
                                const char*           javasig = textout((text *) sig);
                                char*      mark = index(javasrc,'.');
                                const char* javaname = javasrc;

                                if (mark != NULL) {
                                    *mark = '\0';
                                    javaname = mark + 1;
                                }

                                if (javaname == javasrc || strcmp(buffer, javasrc) == 0) {
                                    definition->clazz = (*jenv)->NewGlobalRef(jenv, converter);
                                    definition->method = (*jenv)->GetMethodID(jenv, definition->clazz, javaname, javasig);
                                    definition->isStatic = false;
                                    if (definition->method == NULL) {
                                        definition->method = (*jenv)->GetStaticMethodID(jenv, definition->clazz, javasrc, javasig);
                                        definition->isStatic = true;
                                    }
                                    definition->returnType = DatumGetObjectId(SysCacheGetAttr(PROCNAME, func, Anum_pg_proc_prorettype, NULL));
                                    definition->nargs = nargs;
                                    memmove(definition->argTypes,types, sizeof(Oid) * nargs);
                                } else {
                                    definition = hash_search(function_table, NameStr(lookup), HASH_REMOVE, &hfound);
                                    definition = NULL;
                                }

                                pfree((void*)javasrc);
                                pfree((void*)javasig);
                            }
                            pthread_mutex_unlock(&ftable_guard);
                        }
                    }
                    converter = (*jenv)->GetSuperclass(jenv, converter);
		}
                (*jenv)->PopLocalFrame(jenv, NULL);
	} else {  /* static function  */
            memset(NameStr(lookup), '\0', NAMEDATALEN);

            FormJavaFunctionSig(NameStr(lookup), NAMEDATALEN, NULL, name, nargs, types);

            pthread_mutex_lock(&ftable_guard);
            definition = hash_search(function_table, NameStr(lookup), HASH_FIND, &hfound);
            pthread_mutex_unlock(&ftable_guard);
            if (definition == NULL) {
                HeapTuple func = SearchSysCacheTuple(PROCNAME, PointerGetDatum(name),
                       Int32GetDatum(nargs), PointerGetDatum(types), 0);

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

                        mark = index(javasrc,'.');
                        *mark = '\0';
                        javaname = mark + 1;

                        definition->clazz = (*jenv)->NewGlobalRef(jenv, (*jenv)->FindClass(jenv, javasrc));
                        definition->method = (*jenv)->GetStaticMethodID(jenv, definition->clazz, javaname, javasig);
                        definition->isStatic = true;
                        if (definition->method == NULL) {
                            definition->method = (*jenv)->GetMethodID(jenv, definition->clazz, javaname, javasig);
                            definition->isStatic = false;
                        }
                        definition->returnType = DatumGetObjectId(SysCacheGetAttr(PROCNAME, func, Anum_pg_proc_prorettype, NULL));
                        definition->nargs = nargs;
                        memmove(definition->argTypes,types, sizeof(Oid) * nargs);

                        pfree((void*)javasrc);
                        pfree((void*)javasig);
                    }
                    pthread_mutex_unlock(&ftable_guard);
                }
            }
        }

        if (definition == NULL) {
            elog(ERROR, "Java function %s definition not found", NameStr(lookup));
        }
        
        return definition;
}

jvalue
CallJavaFunction(JavaFunction def, jobject target, int nargs, jvalue* args) {
	JNIEnv         *jenv = GetJavaEnv();
        jvalue         rval;

        if (target == NULL && !def->isStatic) {
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
