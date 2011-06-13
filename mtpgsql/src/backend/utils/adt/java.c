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

#include "postgres.h"

#include "utils/builtins.h"
#include "utils/java.h"
#include "catalog/pg_type.h"
#include "utils/syscache.h"
#include "catalog/pg_proc.h"
#include "access/blobstorage.h"

static          Datum
                ConvertFromJavaArg(Oid type, jvalue val);
static JNIEnv  *GetJavaEnv();

JavaVM         *jvm;
static const char* loader = "driver/weaver/WeaverObjectLoader";

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
	int             length = VARSIZE(datum) - VARHDRSZ;
	char           *data = VARDATA(datum);
	jbyteArray      jb = NULL;
	jobject         result;
        Datum pipe;

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
	int             test;
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
fmgr_java(Datum target, Oid rettype, char *clazz, char *name, char *sig,...)
{
	va_list         pvar;
        Datum           retval;

	va_start(pvar, sig);
	retval = fmgr_javaV(target, rettype, clazz, name, sig, pvar);
        
	va_end(pvar);
        return retval;
}


Datum
fmgr_javaV(Datum target, Oid rettype, char *clazz, char *name, char *sig, va_list args)
{
	JNIEnv         *jenv;
	jclass          converter;
	jmethodID       in;
	jvalue          rval;
	jobject         jtar;
	Datum           ret_datum;

	jenv = GetJavaEnv();
	(*jenv)->PushLocalFrame(jenv, 10);

        if ( target != NULL ) {
            jtar = (jobject) javaout((bytea *) target);
            converter = (*jenv)->GetObjectClass(jenv, jtar);
            if ((*jenv)->ExceptionOccurred(jenv)) {
                    (*jenv)->ExceptionClear(jenv);
                    (*jenv)->PopLocalFrame(jenv, NULL);
                    elog(ERROR, "embedded exception occurred");
            }
            in = (*jenv)->GetMethodID(jenv, converter, name, sig);
            if ((*jenv)->ExceptionOccurred(jenv)) {
                    (*jenv)->ExceptionClear(jenv);
                    (*jenv)->PopLocalFrame(jenv, NULL);
                    elog(ERROR, "embedded exception occurred");
            }
            switch (rettype) {
                case JAVAOID:
                case TEXTOID:
                case VARCHAROID:
                        rval.l = (*jenv)->CallObjectMethodV(jenv, jtar, in, args);
                        break;
                case BOOLOID:
                        rval.z = (*jenv)->CallBooleanMethodV(jenv, jtar, in, args);
                        break;
                case INT4OID:
                        rval.i = (*jenv)->CallIntMethodV(jenv, jtar, in, args);
                        break;                
                case INT8OID:
                        rval.j = (*jenv)->CallLongMethodV(jenv, jtar, in, args);
                        break;                
                case FLOAT8OID:
                        rval.d = (*jenv)->CallDoubleMethodV(jenv, jtar, in, args);
                        break;
                default:
                        rval.l = (*jenv)->CallObjectMethodV(jenv, jtar, in, args);
                        break;
            }

            if ((*jenv)->ExceptionOccurred(jenv)) {
                    (*jenv)->ExceptionClear(jenv);
                    (*jenv)->PopLocalFrame(jenv, NULL);
                    elog(ERROR, "embedded exception occurred");
            }
            ret_datum = ConvertFromJavaArg(rettype, rval);
        } else {
		converter = (*jenv)->FindClass(jenv, clazz);
		if (converter == NULL) {
			(*jenv)->ExceptionClear(jenv);
			(*jenv)->PopLocalFrame(jenv, NULL);
			elog(ERROR, "java class %s does not resolve", clazz);
		}
		in = (*jenv)->GetStaticMethodID(jenv, converter, name, sig);
		if (in == NULL) {
			(*jenv)->ExceptionClear(jenv);
			(*jenv)->PopLocalFrame(jenv, NULL);
			elog(ERROR, "method does not exist class:%s method:%s sig:%s", clazz, name, sig);
		}
		switch (rettype) {
		case JAVAOID:
                case TEXTOID:
		case VARCHAROID:
			rval.l = (*jenv)->CallStaticObjectMethodV(jenv, converter, in, args);
			break;
		case BOOLOID:
			rval.z = (*jenv)->CallStaticBooleanMethodV(jenv, converter, in, args);
			break;
		case INT4OID:
			rval.i = (*jenv)->CallStaticIntMethodV(jenv, converter, in, args);
			break;		
		case INT8OID:
			rval.i = (*jenv)->CallStaticLongMethodV(jenv, converter, in, args);
			break;	
                    case FLOAT8OID:
                        rval.d = (*jenv)->CallStaticDoubleMethodV(jenv, converter, in, args);
                        break;
		default:
			rval.l = (*jenv)->CallStaticObjectMethodV(jenv, converter, in, args);
			break;
		}
             ret_datum = ConvertFromJavaArg(rettype, rval);
       }
	(*jenv)->PopLocalFrame(jenv, NULL);
	return ret_datum;
}


Datum
fmgr_javaA(Datum target, Oid rettype, char *clazz, char *name, char *sig, void *args)
{
	JNIEnv         *jenv;
	jclass          converter;
	jmethodID       in;
	jvalue          rval;
	Datum           ret_datum;

	jenv = GetJavaEnv();

	(*jenv)->PushLocalFrame(jenv, 10);

	if (target != NULL) {
		jobject         jtar = (jobject) javaout((bytea *) target);
		converter = (*jenv)->GetObjectClass(jenv, jtar);
		in = (*jenv)->GetMethodID(jenv, converter, name, sig);
		if (in == NULL) {
			(*jenv)->PopLocalFrame(jenv, NULL);
			elog(ERROR, "method for target class does not exist");
		}
		switch (rettype) {
		case JAVAOID:
                case TEXTOID:
		case VARCHAROID:
			rval.l = (*jenv)->CallObjectMethodA(jenv, jtar, in, (jvalue *) args);
			break;
		case BOOLOID:
			rval.z = (*jenv)->CallBooleanMethodA(jenv, jtar, in, (jvalue *) args);
			break;
		case INT4OID:
			rval.i = (*jenv)->CallIntMethodA(jenv, jtar, in, (jvalue *) args);
			break;
		default:
			rval.l = (*jenv)->CallObjectMethodA(jenv, jtar, in, (jvalue *) args);
			break;
		}
	} else {
		converter = (*jenv)->FindClass(jenv, clazz);
		if (converter == NULL) {
			(*jenv)->ExceptionClear(jenv);
			(*jenv)->PopLocalFrame(jenv, NULL);
			elog(ERROR, "java class %s does not resolve", clazz);
		}
		in = (*jenv)->GetStaticMethodID(jenv, converter, name, sig);
		if (in == NULL) {
			(*jenv)->ExceptionClear(jenv);
			(*jenv)->PopLocalFrame(jenv, NULL);
			elog(ERROR, "method does not exist class:%s method:%s sig:%s", clazz, name, sig);
		}
		switch (rettype) {
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
		default:
			rval.l = (*jenv)->CallStaticObjectMethodA(jenv, converter, in, (jvalue *) args);
			break;
		}

	}

	if ((*jenv)->ExceptionOccurred(jenv)) {
		(*jenv)->ExceptionClear(jenv);
		elog(ERROR, "embedded exception occurred");
	}
	ret_datum = ConvertFromJavaArg(rettype, rval);
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
			char           *string = palloc(VARSIZE(strvar) + 1);
			memmove(string, VARDATA(strvar), VARSIZE(strvar));
			string[VARSIZE(strvar)] = 0x00;
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
		rval.l = javaout((bytea *) val);
		break;
	default:
		elog(ERROR, "java argument not valid");

	}

	return rval;
}


Datum
ConvertFromJavaArg(Oid type, jvalue val)
{
	JNIEnv         *jenv;
	Datum           ret_datum = NULL;


	jenv = GetJavaEnv();

	switch (type) {
	case INT4OID:
		ret_datum = Int32GetDatum(val.i);
		break;
        case TEXTOID:
	case VARCHAROID:
		{
			jstring         strvar = (jstring) val.l;
			int             len = (*jenv)->GetStringUTFLength(jenv, strvar);
			bytea          *string = palloc(len + VARHDRSZ);

			SETVARSIZE(string, len + VARHDRSZ);
			(*jenv)->GetStringUTFRegion(jenv, strvar, 0, len, VARDATA(string));
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

	jobject         target = javaout((bytea *) object);

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
GetJavaSignature(Datum target, char *name, int nargs, Oid * types, Oid * fid,
     char **javasrc, char **javaname, char **javasig /* return variable */ ,
		 Oid * rettype /* return variable */ )
{
	jobject         javaTarget = NULL;
	JNIEnv         *jenv;
	jclass          converter = NULL;
	jclass          class_class;

	jmethodID       getname;
	jstring         classid;

	NameData        lookup;

	HeapTuple       func;

	memset(NameStr(lookup), 0x00, NAMEDATALEN);

	jenv = GetJavaEnv();

	if (target) {
                (*jenv)->PushLocalFrame(jenv, 10);
		
                javaTarget = javaout((bytea *) target);
		converter = (*jenv)->GetObjectClass(jenv, javaTarget);

		class_class = (*jenv)->FindClass(jenv, "java/lang/Class");
		getname = (*jenv)->GetMethodID(jenv, class_class, "getName", "()Ljava/lang/String;");

		while (converter != NULL) {
			jsize           len;
			classid = (*jenv)->CallObjectMethod(jenv, converter, getname);
			len = (*jenv)->GetStringUTFLength(jenv, classid);
			(*jenv)->GetStringUTFRegion(jenv, classid, 0, len, NameStr(lookup));

			*(NameStr(lookup) + len) = '.';
			strncpy(NameStr(lookup) + len + 1, name, NAMEDATALEN - len - 2);

			func = SearchSysCacheTuple(PROCNAME, NameGetDatum(&lookup),
			   Int32GetDatum(nargs), PointerGetDatum(types), 0);

			if (HeapTupleIsValid(func)) {
				Form_pg_proc    pg_proc = (Form_pg_proc) GETSTRUCT(func);
				*fid = func->t_data->t_oid;
				*rettype = pg_proc->prorettype;
				Datum           sig = SysCacheGetAttr(PROCOID, func, Anum_pg_proc_prosrc, NULL);
				*javasig = textout((text *) sig);
				Datum           cla = SysCacheGetAttr(PROCOID, func, Anum_pg_proc_probin, NULL);
				*javasrc = textout((text *) cla);
                                
                                (*jenv)->PopLocalFrame(jenv, NULL);
				return 0;
			} else {
				converter = (*jenv)->GetSuperclass(jenv, converter);
			}
		}
                (*jenv)->PopLocalFrame(jenv, NULL);
	} else {
		strncpy(NameStr(lookup), name, NAMEDATALEN);

		func = SearchSysCacheTuple(PROCNAME, NameGetDatum(&lookup),
			   Int32GetDatum(nargs), PointerGetDatum(types), 0);

		if (HeapTupleIsValid(func)) {
			Form_pg_proc    pg_proc = (Form_pg_proc) GETSTRUCT(func);
			*fid = func->t_data->t_oid;
			*rettype = pg_proc->prorettype;
			Datum           sig = SysCacheGetAttr(PROCOID, func, Anum_pg_proc_prosrc, NULL);
			*javasig = textout((text *) sig);
			Datum           cla = SysCacheGetAttr(PROCOID, func, Anum_pg_proc_probin, NULL);
			*javasrc = textout((text *) cla);
			return 0;
		}
	}
	return 1;
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
