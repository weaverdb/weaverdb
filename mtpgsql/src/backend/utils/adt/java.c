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

static int  GetJavaSignature(jobject target,const char* name,int nargs,Oid* types, Oid* fid,
				const char** src, const char** method,
				const char** sig, Oid* rettype);

void
SetJVM(JavaVM * java, const char *ol)
{
	jvm = java;
	if (ol != NULL)
		loader = ol;
}

void SetJavaObjectLoader(const char* l) {
    if ( l != NULL ) loader = strdup(l);
}

JNIEnv         *
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
	jclass          converter;
	jmethodID       out;
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

	converter = (*jenv)->FindClass(jenv, loader);
	if (converter == NULL) {
		elog(ERROR, "failed to find converter class");
	}
	out = (*jenv)->GetStaticMethodID(jenv, converter, "java_out", "([B)Ljava/lang/Object;");
	if ((*jenv)->ExceptionOccurred(jenv)) {
		(*jenv)->ExceptionClear(jenv);
		elog(ERROR, "embedded exception occurred");
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

	result = (*jenv)->CallStaticObjectMethod(jenv, converter, out, jb);

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
	jclass          converter;
	jmethodID       in;
	int             length;
	bytea          *data;
	jbyteArray      jb = NULL;

	jenv = GetJavaEnv();

	converter = (*jenv)->FindClass(jenv, loader);
	if (converter == NULL) {
		elog(ERROR, "failed to find converter class");
	}
	in = (*jenv)->GetStaticMethodID(jenv, converter, "java_in", "(Ljava/lang/Object;)[B");
	if ((*jenv)->ExceptionOccurred(jenv)) {
		(*jenv)->ExceptionClear(jenv);
		elog(ERROR, "embedded exception occurred");
	}
	jb = (*jenv)->CallStaticObjectMethod(jenv, converter, in, target);
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
	jclass          converter;
	jmethodID       in;
	int             length;
	bytea          *data;
	jbyteArray      jb = NULL;
	jbyte          *prim = NULL;

	jenv = GetJavaEnv();

	(*jenv)->PushLocalFrame(jenv, 10);

	converter = (*jenv)->FindClass(jenv, loader);
	if (converter == NULL) {
		(*jenv)->PopLocalFrame(jenv, NULL);
		elog(ERROR, "failed to find converter class");
}
	in = (*jenv)->GetStaticMethodID(jenv, converter, "java_text_in", "(Ljava/lang/String;)[B");
	if ((*jenv)->ExceptionOccurred(jenv)) {
		(*jenv)->ExceptionClear(jenv);
		(*jenv)->PopLocalFrame(jenv, NULL);
		elog(ERROR, "embedded exception occurred");
	}
	jb = (*jenv)->CallStaticObjectMethod(jenv, converter, in, (*jenv)->NewStringUTF(jenv, target));
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
	jclass          converter;
	jmethodID       out;
	int             length = VARSIZE(target) - VARHDRSZ;
	char           *data = VARDATA(target);
	jbyteArray      jb = NULL;
	jbyte          *prim = NULL;
	jstring         result;

	jenv = GetJavaEnv();

	converter = (*jenv)->FindClass(jenv, loader);
	if (converter == NULL) {
		(*jenv)->PopLocalFrame(jenv, NULL);
		elog(ERROR, "failed to find converter class");
}
	out = (*jenv)->GetStaticMethodID(jenv, converter, "java_text_out", "([B)Ljava/lang/String;");
	if ((*jenv)->ExceptionOccurred(jenv)) {
		(*jenv)->ExceptionClear(jenv);
		(*jenv)->PopLocalFrame(jenv, NULL);
		elog(ERROR, "embedded exception occurred");
	}
	jb = (*jenv)->NewByteArray(jenv, length);
	if (jb != NULL) {
		(*jenv)->SetByteArrayRegion(jenv, jb, 0, length, (jbyte *) data);
	} else {
		(*jenv)->PopLocalFrame(jenv, NULL);
		elog(ERROR, "java memory error");
	}

	result = (*jenv)->CallStaticObjectMethod(jenv, converter, out, jb);

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
fmgr_javaA(Datum target, const char* function, int nargs, Oid* types, jvalue *args,bool* isNull)
{
	JNIEnv         *jenv;
	jclass          converter;
	jmethodID       in;
	jvalue          rval;
	Datum           ret_datum;
        Oid foid;
        Oid rtype;
        const char            *clazz;
        const char            *method;
        const char            *sig;
                
	jenv = GetJavaEnv();

	(*jenv)->PushLocalFrame(jenv, 10);

        jobject         jtar = (jobject) javaout((bytea *) DatumGetPointer(target));

        if (GetJavaSignature(jtar,function,nargs,types,&foid,&clazz,&method,&sig,&rtype)) {
            if ((*jenv)->IsSameObject(jenv, jtar, NULL)) {
                converter = (*jenv)->FindClass(jenv,clazz);
                in = (*jenv)->GetStaticMethodID(jenv, converter, method, sig);
            } else {
                converter = (*jenv)->GetObjectClass(jenv, jtar);
                in = (*jenv)->GetMethodID(jenv, converter, method, sig);
            }
            pfree((void*)clazz);
            /* never free method, part of clazz */
            pfree((void*)sig);
            if (in == NULL) {
                (*jenv)->PopLocalFrame(jenv, NULL);
                elog(ERROR, "%s.%s with signature %s does not exist", clazz, method, sig);
            }
        } else {
            (*jenv)->PopLocalFrame(jenv, NULL);
            elog(ERROR, "java function %s not found", function);
        }

        switch (rtype) {
            case JAVAOID:
            case TEXTOID:
            case VARCHAROID:
                    rval.l = (*jenv)->IsSameObject(jenv, jtar, NULL) ? 
                            (*jenv)->CallStaticObjectMethodA(jenv, converter, in, (jvalue *) args)
                            : (*jenv)->CallObjectMethodA(jenv, jtar, in, (jvalue *) args);
                    break;
            case BOOLOID:
                    rval.z = (*jenv)->IsSameObject(jenv, jtar, NULL) ? 
                        (*jenv)->CallStaticBooleanMethodA(jenv, converter, in, (jvalue *) args)
                        : (*jenv)->CallBooleanMethodA(jenv, jtar, in, (jvalue *) args);
                    break;
            case INT4OID:
                    rval.i = (*jenv)->IsSameObject(jenv, jtar, NULL) ? 
                        (*jenv)->CallStaticIntMethodA(jenv, converter, in, (jvalue *) args)
                        : (*jenv)->CallIntMethodA(jenv, jtar, in, (jvalue *) args);
                    break;
            case INT8OID:
                    rval.j = (*jenv)->IsSameObject(jenv, jtar, NULL) ? 
                        (*jenv)->CallStaticLongMethodA(jenv, converter, in, (jvalue *) args)
                        : (*jenv)->CallLongMethodA(jenv, jtar, in, args);
                    break;                
            case FLOAT8OID:
                    rval.d = (*jenv)->IsSameObject(jenv, jtar, NULL) ? 
                        (*jenv)->CallStaticDoubleMethodA(jenv, converter, in, (jvalue *) args)
                        : (*jenv)->CallDoubleMethodA(jenv, jtar, in, args);
                    break;
            default:
                    rval.l = (*jenv)->IsSameObject(jenv, jtar, NULL) ? 
                        (*jenv)->CallStaticObjectMethodA(jenv, converter, in, (jvalue *) args)
                        : (*jenv)->CallObjectMethodA(jenv, jtar, in, (jvalue *) args);
                    break;
        }

	if ((*jenv)->ExceptionOccurred(jenv)) {
		(*jenv)->ExceptionClear(jenv);
		elog(ERROR, "embedded exception occurred");
	}
	ret_datum = ConvertFromJavaArg(rtype, rval,isNull);
	(*jenv)->PopLocalFrame(jenv, NULL);

	return ret_datum;
}

Datum
fmgr_cached_javaA(JavaInfo* jinfo, int nargs, Oid* types, jvalue *args,bool* isNull)
{
	JNIEnv         *jenv = GetJavaEnv();
	jclass          converter;
	jmethodID       in;
	jvalue          rval;
	Datum           ret_datum;
        Oid foid;
        Oid rtype;

	(*jenv)->PushLocalFrame(jenv, 10);

        rtype = jinfo->rettype;
        converter = (*jenv)->FindClass(jenv, jinfo->javaclazz);
        if (converter == NULL) {
                (*jenv)->ExceptionClear(jenv);
                (*jenv)->PopLocalFrame(jenv, NULL);
                elog(ERROR, "java class %s does not resolve", jinfo->javaclazz);
        }
        in = (*jenv)->GetStaticMethodID(jenv, converter, jinfo->javamethod, jinfo->javasig);
        if (in == NULL) {
                (*jenv)->ExceptionClear(jenv);
                (*jenv)->PopLocalFrame(jenv, NULL);
                elog(ERROR, "method does not exist class:%s method:%s sig:%s", jinfo->javaclazz, jinfo->javamethod, jinfo->javasig);
        }

        switch (rtype) {
		case JAVAOID:
                case TEXTOID:
		case VARCHAROID:
			rval.l = (*jenv)->CallStaticObjectMethodA(jenv, converter, in, (jvalue *) args);
			break;
		case BOOLOID:
			rval.z = (*jenv)->CallStaticBooleanMethodA(jenv, converter, in, (jvalue *) args);
			break;
		case INT4OID:
			rval.i = (*jenv)->CallStaticIntMethodA(jenv, converter, in, (jvalue *) args);
			break;
                case INT8OID:
                        rval.j = (*jenv)->CallStaticLongMethodA(jenv, converter, in, args);
                        break;                
                case FLOAT8OID:
                        rval.d = (*jenv)->CallStaticDoubleMethodA(jenv, converter, in, args);
                        break;
                    default:
			rval.l = (*jenv)->CallStaticObjectMethodA(jenv, converter, in, (jvalue *) args);
			break;
	}

	if ((*jenv)->ExceptionOccurred(jenv)) {
		(*jenv)->ExceptionClear(jenv);
		elog(ERROR, "embedded exception occurred");
	}
	ret_datum = ConvertFromJavaArg(rtype, rval,isNull);
	(*jenv)->PopLocalFrame(jenv, NULL);

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
	jclass          converter;
	jmethodID       in;
	jint            result = 0;
	jbyteArray      master1 = NULL;
	jbyteArray      master2 = NULL;

	jenv = GetJavaEnv();
	(*jenv)->PushLocalFrame(jenv, 10);

	converter = (*jenv)->FindClass(jenv, loader);
	if (converter == NULL) {
		(*jenv)->PopLocalFrame(jenv, NULL);
		elog(ERROR, "failed to find converter class");
	}
	in = (*jenv)->GetStaticMethodID(jenv, converter, "java_compare", "([B[B)I");
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

	result = (*jenv)->CallStaticIntMethod(jenv, converter, in, master1, master2);

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
	jclass          converter;
	jmethodID       in;
	jboolean        result = 0;
	jbyteArray      master1 = NULL;
	jbyteArray      master2 = NULL;

	jenv = GetJavaEnv();
	(*jenv)->PushLocalFrame(jenv, 10);

	converter = (*jenv)->FindClass(jenv, loader);
	if (converter == NULL) {
		(*jenv)->PopLocalFrame(jenv, NULL);
		elog(ERROR, "failed to find converter class");
	}
	in = (*jenv)->GetStaticMethodID(jenv, converter, "java_equals", "([B[B)Z");
	if ((*jenv)->ExceptionOccurred(jenv)) {
		(*jenv)->ExceptionClear(jenv);
		(*jenv)->PopLocalFrame(jenv, NULL);
		elog(ERROR, "could not find method java_equals method in java object loader");
	}
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

	result = (*jenv)->CallStaticBooleanMethod(jenv, converter, in, master1, master2);

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

PG_EXTERN int
GetJavaSignature(jobject target, const char *name, int nargs, Oid * types, Oid * fid,
     const char **javasrc, const char **javaname, const char **javasig, Oid * rettype )
{
	JNIEnv         *jenv;
	jclass          converter = NULL;
	jclass          class_class;

	jmethodID       getname;
	jstring         classid;
        int found = 0;

	jenv = GetJavaEnv();

	if (target) {
                NameData        lookup;
                
                memset(NameStr(lookup), 0x00, NAMEDATALEN);
              (*jenv)->PushLocalFrame(jenv, 10);
		
		converter = (*jenv)->GetObjectClass(jenv, target);

		class_class = (*jenv)->FindClass(jenv, "java/lang/Class");
		getname = (*jenv)->GetMethodID(jenv, class_class, "getName", "()Ljava/lang/String;");

		while (converter != NULL) {
                    char*               mark;
                    jsize           len;
                    classid = (*jenv)->CallObjectMethod(jenv, converter, getname);
                    len = (*jenv)->GetStringUTFLength(jenv, classid);
                    (*jenv)->GetStringUTFRegion(jenv, classid, 0, len, NameStr(lookup));
                    mark = NameStr(lookup);
                    while ( mark != NULL ) {
                        mark = index(mark,'.');
                        if ( mark != NULL ) *mark = '/';
                    }

                    *(NameStr(lookup) + len) = '.';
                    strncpy(NameStr(lookup) + len + 1, name, NAMEDATALEN - len - 2);

                    HeapTuple func = SearchSysCacheTuple(PROCNAME, NameGetDatum(&lookup),
                       Int32GetDatum(nargs), PointerGetDatum(types), 0);

                    if (HeapTupleIsValid(func)) {
                        char* mark;
                        Form_pg_proc    pg_proc = (Form_pg_proc) GETSTRUCT(func);
                        *fid = func->t_data->t_oid;
                        *rettype = pg_proc->prorettype;

                        Datum           cla = SysCacheGetAttr(PROCNAME, func, Anum_pg_proc_prosrc, NULL);
                        *javasrc = textout((text *) cla);
                        *javaname = *javasrc;
                        Datum           sig = SysCacheGetAttr(PROCNAME, func, Anum_pg_proc_probin, NULL);
                        *javasig = textout((text *) sig);

                        found = 1;
                        break;
                    } else {
                        converter = (*jenv)->GetSuperclass(jenv, converter);
                    }
		}
                (*jenv)->PopLocalFrame(jenv, NULL);
	} else {
            NameData        lookup;                
            memset(NameStr(lookup), 0x00, NAMEDATALEN);
            strncpy(NameStr(lookup), name, strlen(name));
            HeapTuple func = SearchSysCacheTuple(PROCNAME, NameGetDatum(&lookup),
                       Int32GetDatum(nargs), PointerGetDatum(types), 0);

             if (HeapTupleIsValid(func)) {
                    char* mark;
                    Form_pg_proc    pg_proc = (Form_pg_proc) GETSTRUCT(func);
                    *fid = func->t_data->t_oid;
                    *rettype = pg_proc->prorettype;

                    Datum           cla = SysCacheGetAttr(PROCNAME, func, Anum_pg_proc_prosrc, NULL);
                    *javasrc = textout((text *) cla);
                    Datum           sig = SysCacheGetAttr(PROCNAME, func, Anum_pg_proc_probin, NULL);
                    *javasig = textout((text *) sig);

                    mark = index(*javasrc,'.');
                    *mark = '\0';
                    *javaname = mark + 1;
                    found = 1;
                }
        }
        
        return found;
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
