




#include <jni.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <strings.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <pthread.h>

#include "env/connectionutil.h"
#include "env/WeaverInterface.h"

#include "org_mtpgsql_server_Connection.h"
#include "org_mtpgsql_server_SimpleServer.h"

jobject 		javaSideLog[MAXBACKENDS];
OpaquePGConn*	 	theManagers[MAXBACKENDS];
JNIEnv*			currentenv[MAXBACKENDS];

static unsigned short PostPortName = 0;
static char *progname = (char *) "libweaver";
static pthread_mutex_t		allocator;
static pthread_mutexattr_t	allocatt;

extern int DebugLvl;

static int pipeout(int pipeid,char* buff,int start,int run);
static int pipein(int pipeid,char* buff,int start,int run);
static jint getProperAgent(JNIEnv* env,jobject talker,jclass talkerClass);
static jint  reportError(JNIEnv* env,jobject talker,jclass classID,jint link);

JNIEXPORT void JNICALL Java_org_mtpgsql_server_SimpleServer_init(JNIEnv *env,jobject talkerObject)
{
	char		datapass[256];
	memset(datapass,0,256);
	
	memset(javaSideLog,0,sizeof(javaSideLog));
	memset(theManagers,0,sizeof(theManagers));
	memset(currentenv,0,sizeof(currentenv));
	
	pthread_mutexattr_init(&allocatt);
	pthread_mutex_init(&allocator,&allocatt);
	
	strcpy(datapass,"template1");

	initweaverbackend(datapass);
		
}

JNIEXPORT void JNICALL Java_org_mtpgsql_server_SimpleServer_close(JNIEnv *env,jobject talkerObject)
{
    int x = 0;
/*  shutdown any threads resources still hanging around */
	prepareforshutdown();

	for (x = 0;x<MAXBACKENDS;x++) {
		if (theManagers[x] != NULL ) {
			PGDestroyConnection(theManagers[x]);
			theManagers[x] == NULL;
		}
		if ( javaSideLog[x] != NULL ) {
			(*env)->DeleteGlobalRef(env,javaSideLog[x]);
			javaSideLog[x] = NULL;    
		}
	}

	wrapupweaverbackend();
	
	pthread_mutex_destroy(&allocator);
}

/*
 * Class:     org_mtpgsql_server_Connection
 * Method:    grabConnection
 * Signature: (Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_org_mtpgsql_server_Connection_grabConnection
  (JNIEnv * env, jobject talkerObject, jstring theName, jstring thePassword, jstring theConnect)
 {
	const char* 		pass;
	const char* 		name;
	const char* 		conn;
	short			x;

	jclass 			classID;
	jfieldID 		idField;
	jobject 		tracker;
	jclass 			trackerClass;
	jfieldID		trackerField;


	pthread_mutex_lock(&allocator);
	for(x=0;x<MAXBACKENDS;x++)
	{
		if (theManagers[x] == NULL) break;
	}

	classID = (*env)->GetObjectClass(env,talkerObject);
	idField = (*env)->GetFieldID(env,classID,"id","Lorg/mtpgsql/server/LinkID;");
	tracker =(*env)->GetObjectField(env,talkerObject,idField);
	trackerClass = (*env)->GetObjectClass(env,tracker);
	trackerField = (*env)->GetFieldID(env,trackerClass,"linkNumber","I");

	(*env)->SetIntField(env,tracker,trackerField,x);
	javaSideLog[x] = (*env)->NewGlobalRef(env,talkerObject);

/*  done grabbing
  logging the id and creating statement space if logon is valid
*/
	pass = (*env)->GetStringUTFChars(env,thePassword,NULL);
	name = (*env)->GetStringUTFChars(env,theName,NULL);
        conn = (*env)->GetStringUTFChars(env,theConnect,NULL);

	theManagers[x] = PGCreateConnection(name, pass, conn);
	pthread_mutex_unlock(&allocator);
/*
	releasing java resources
*/
	(*env)->ReleaseStringUTFChars(env,thePassword,pass);
	(*env)->ReleaseStringUTFChars(env,theName,name);
	(*env)->ReleaseStringUTFChars(env,theConnect,conn);
/*
  if not valid kill everything
*/
	if ( !PGIsValidConnection(theManagers[x]) ) 
	{	
		reportError(env,talkerObject,classID,x);
		
		PGDestroyConnection(theManagers[x]);
		theManagers[x] = NULL;
		(*env)->DeleteGlobalRef(env,javaSideLog[x]);
		javaSideLog[x] = NULL;
		(*env)->ThrowNew(env,(*env)->FindClass(env,"java/sql/SQLException"),"User not valid");
		
		return;
	}
}


/*
 * Class:     org_mtpgsql_server_Connection
 * Method:    streamExec
 * Signature: (Ljava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_org_mtpgsql_server_Connection_streamExec
  (JNIEnv *env, jobject talkerObject, jstring statement)
{
	jclass classID = (*env)->GetObjectClass(env,talkerObject);
	jint link = getProperAgent(env,talkerObject,classID);
        const char* 	lstate;
        
        currentenv[link] = env;
        lstate = (*env)->GetStringUTFChars(env,statement,NULL);
                
        PGPipeConnect(theManagers[link],link,(void*)pipein,(void*)pipeout);
        PGStreamExec(theManagers[link],(char*)lstate);
        PGPipeDisconnect(theManagers[link]);
        
        (*env)->ReleaseStringUTFChars(env,statement,lstate);
/*
 report errors
*/
	reportError(env,talkerObject,classID,link);
	if ( PGGetErrorCode(theManagers[link]) != 0 ) 
	{
		(*env)->ThrowNew(env,(*env)->FindClass(env,"java/sql/SQLException"),PGGetErrorText(theManagers[link]));
	}
}



static jint reportError(JNIEnv* env,jobject talkerObject,jclass talkerClass,jint link)
{
/*
	reporting errors to java
*/
	jfieldID	fieldID = (*env)->GetFieldID(env,talkerClass, "err","I");
	jfieldID  eText = (*env)->GetFieldID(env,talkerClass,"errorText","Ljava/lang/String;");
	jfieldID  eState = (*env)->GetFieldID(env,talkerClass,"state","Ljava/lang/String;");
	jstring et = (*env)->NewStringUTF(env,PGGetErrorText(theManagers[link]));
	jstring st = (*env)->NewStringUTF(env,PGGetErrorState(theManagers[link]));

	(*env)->SetIntField(env,talkerObject,fieldID,PGGetErrorCode(theManagers[link]));

	(*env)->SetObjectField(env,talkerObject,eText,et);
	(*env)->SetObjectField(env,talkerObject,eState,st);
	
	return 0;
}

static jint getProperAgent(JNIEnv * env,jobject talkerObject,jclass talkerClass)
{
/*
 proper agent	
*/
	jfieldID 	idField = (*env)->GetFieldID(env,talkerClass,"id","Lorg/mtpgsql/server/LinkID;");
	jobject 	tracker = (*env)->GetObjectField(env,talkerObject,idField);
	jclass 		trackerClass = (*env)->GetObjectClass(env,tracker);
	jfieldID 	trackerField = (*env)->GetFieldID(env,trackerClass,"linkNumber","I");
	jint 		link = (*env)->GetIntField(env,tracker,trackerField);

	if ( (*env)->ExceptionOccurred(env) ) (*env)->ExceptionClear(env);
        currentenv[link] = env;
        
        return link;
}

static int pipeout(int pipeid,char* buff,int start,int run)
{

    JNIEnv* env = currentenv[pipeid];
    jclass cid = (*env)->GetObjectClass(env,javaSideLog[pipeid]);
    jmethodID mid = (*env)->GetMethodID(env,cid,"pipeOut","([B)V");
    jbyteArray jb = (*env)->NewByteArray(env,run);
    if ( jb != NULL ) {
    	(*env)->SetByteArrayRegion(env,jb,start,run,(jbyte*)buff);
    	(*env)->CallVoidMethod(env,javaSideLog[pipeid],mid,jb);
    	return 0;
    } else {
    	return -1;
    }
}

static int pipein(int pipeid,char* buff,int start,int run)
{

    JNIEnv* env = currentenv[pipeid];
    jclass cid = (*env)->GetObjectClass(env,javaSideLog[pipeid]);
    jmethodID mid = (*env)->GetMethodID(env,cid,"pipeIn","([B)I");
    jbyteArray jb = (*env)->NewByteArray(env,run);

    if ( jb != NULL ) {
    	jint count = (*env)->CallIntMethod(env,javaSideLog[pipeid],mid,jb);
    	(*env)->GetByteArrayRegion(env,jb,0,count,(jbyte*)(buff + start));
    	return count;
    } else {
    	return -1;
    }
}

