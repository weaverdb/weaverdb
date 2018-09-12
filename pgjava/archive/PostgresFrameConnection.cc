/*



	JConnManager Code
	
	
	
	
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

#include "env/connectionutil.h"

#include "PostgresStmtManager.h"
#include "env/PostgresInterface.h"
#include "PostgresInitializer.h"
#include "utils/java.h"

#include "PostgresFrameConnection.h"

jobject 		javaSideLog[MAXBACKENDS];
PostgresStmtManager* 	theManagers[MAXBACKENDS];
JNIEnv*			currentenv[MAXBACKENDS];

//static unsigned short PostPortName = 0;
//static char *progname = (char *) "libpostgres";
static pthread_mutex_t		allocator;
static pthread_mutexattr_t	allocatt;

static bool	debug = false;

extern int DebugLvl;

extern "C" static int pipeout(int pipeid,int streamid,char* buff,int start,int run);
extern "C" static int pipein(int pipeid,int streamid,char* buff,int start,int run);
extern "C" static int direct_pipeout(int pipeid,int streamid,char* buff,int start,int run);
extern "C" static int direct_pipein(int pipeid,int streamid,char* buff,int start,int run);
static jint getProperAgent(JNIEnv* env,jobject talker,jclass talkerClass);
static jint  reportError(JNIEnv* env,jobject talker,jclass classID,jint link);

static char object_loader[512];

static bool        initialized = false;

JNIEXPORT void JNICALL Java_com_myosyn_drivers_postgres_PostgresInitializer_init(JNIEnv *env,jobject talkerObject, jstring jd)
{
	JavaVM*   jvm = NULL;
	char		datapass[2048];
	memset(datapass,0,2048);
	
	memset(javaSideLog,0,sizeof(javaSideLog));
	memset(theManagers,0,sizeof(theManagers));
	memset(currentenv,0,sizeof(currentenv));
	
	pthread_mutexattr_init(&allocatt);
	pthread_mutex_init(&allocator,&allocatt);
	
	if ( jd != NULL ) {
            int len = env->GetStringUTFLength(jd);
            env->GetStringUTFRegion(jd,0,len,datapass);
        }

	initpostgresbackend(datapass);
		
	env->GetJavaVM(&jvm);
	
	SetJVM(jvm,NULL);
	pthread_mutex_lock(&allocator);
        initialized  = true;
	pthread_mutex_unlock(&allocator);
}

JNIEXPORT void JNICALL Java_com_myosyn_drivers_postgres_PostgresInitializer_close(JNIEnv *env,jobject talkerObject)
{
/*  shutdown any threads resources still hanging around */
	prepareforshutdown();

	pthread_mutex_lock(&allocator);
	for (int x = 0;x<MAXBACKENDS;x++) {
            if (theManagers[x] != NULL ) {
                theManagers[x]->Init(env);
                delete theManagers[x];
                theManagers[x] = NULL;
            }
            if ( javaSideLog[x] != NULL ) {
                env->DeleteGlobalRef(javaSideLog[x]);
                javaSideLog[x] = NULL;    
            }
	}
        initialized = false;
	pthread_mutex_unlock(&allocator);

	wrapuppostgresbackend();
	
	pthread_mutex_destroy(&allocator);
}

JNIEXPORT void JNICALL Java_com_myosyn_drivers_postgres_PostgresFrameConnection_grabConnection
  (JNIEnv * env, jobject talkerObject, jstring theName, jstring thePassword, jstring theConnect)
 {
	short			x;

	pthread_mutex_lock(&allocator);
        if ( !initialized ) {
            if (!env->ExceptionOccurred() ) 
                env->ThrowNew(env->FindClass("com/myosyn/server/data/SynSQLException"),"system not initialized");
            pthread_mutex_unlock(&allocator);
            return;
        }
	for(x=0;x<MAXBACKENDS;x++)
	{
		if (javaSideLog[x] == NULL) {
                    javaSideLog[x] = env->NewGlobalRef(talkerObject);
                    break;
                }
	}
	pthread_mutex_unlock(&allocator);

	jclass classID = env->GetObjectClass(talkerObject);
	jfieldID idField = env->GetFieldID(classID,"id","Lcom/myosyn/server/data/LinkID;");
	jobject tracker = env->GetObjectField(talkerObject,idField);
	jclass trackerClass = env->GetObjectClass(tracker);
	jfieldID trackerField = env->GetFieldID(trackerClass,"linkNumber","I");
	env->SetIntField(tracker,trackerField,x);

//  done grabbing
//  logging the id and creating statement space if logon is valid
	
	jsize passlen = env->GetStringUTFLength(thePassword);
	jsize namelen = env->GetStringUTFLength(theName);
	jsize connlen = env->GetStringUTFLength(theConnect);

	if (( passlen > 63 || namelen > 63 || connlen > 63 ) ||
            ( passlen == 0 || namelen == 0 || connlen == 0 ) )
        {
		pthread_mutex_lock(&allocator);
		env->DeleteGlobalRef(javaSideLog[x]);
		javaSideLog[x] = NULL;
		pthread_mutex_unlock(&allocator);
		if (!env->ExceptionOccurred() ) 
                    env->ThrowNew(env->FindClass("com/myosyn/server/data/SynSQLException"),"User not valid");
		return;
	}

	char pass[64];
	char name[64];
	char conn[64];

	pass[passlen] = 0;
	name[namelen] = 0;
	conn[connlen] = 0;

	env->GetStringUTFRegion(thePassword,0,passlen,pass);
	env->GetStringUTFRegion(theName,0,namelen,name);
	env->GetStringUTFRegion(theConnect,0,connlen,conn);

	theManagers[x] = new PostgresStmtManager(name, pass, conn);

//  if not valid kill everything
        reportError(env,talkerObject,classID,x);

	if ( !theManagers[x]->IsValid() ) 
	{	
            pthread_mutex_lock(&allocator);

            delete theManagers[x];
            theManagers[x] = NULL;
            env->DeleteGlobalRef(javaSideLog[x]);
            javaSideLog[x] = NULL;
            pthread_mutex_unlock(&allocator);
            
            if (!env->ExceptionOccurred() ) 
                env->ThrowNew(env->FindClass("com/myosyn/server/data/SynSQLException"),"User not valid");
	}

}

JNIEXPORT void JNICALL Java_com_myosyn_drivers_postgres_PostgresFrameConnection_connectSubConnection
  (JNIEnv * env, jobject talkerObject, jobject parent) {
	short			x,open;
        PostgresStmtManager*    cparent = NULL;

	pthread_mutex_lock(&allocator);
        if ( !initialized ) {
            if (!env->ExceptionOccurred() ) 
                env->ThrowNew(env->FindClass("com/myosyn/server/data/SynSQLException"),"system not initialized");
            pthread_mutex_unlock(&allocator);
            return;
        }
	for(x=0;x<MAXBACKENDS;x++)
	{
            if (javaSideLog[x] == parent ) {
                cparent = theManagers[x];
                break;
            }
	}
	for(x=0;x<MAXBACKENDS;x++)
	{
            if (javaSideLog[x] == NULL ) {
                javaSideLog[x] = env->NewGlobalRef(talkerObject);
                break;
            }
	}
 	pthread_mutex_unlock(&allocator);

	jclass classID = env->GetObjectClass(talkerObject);
	jfieldID idField = env->GetFieldID(classID,"id","Lcom/myosyn/server/data/LinkID;");
	jobject tracker = env->GetObjectField(talkerObject,idField);
	jclass trackerClass = env->GetObjectClass(tracker);
	jfieldID trackerField = env->GetFieldID(trackerClass,"linkNumber","I");
	env->SetIntField(tracker,trackerField,x);
	
	theManagers[x] = new PostgresStmtManager(cparent);

//  if not valid kill everything
        reportError(env,talkerObject,classID,x);

	if ( !theManagers[x]->IsValid() ) 
	{	
                pthread_mutex_lock(&allocator);

		delete theManagers[x];
		theManagers[x] = NULL;
		env->DeleteGlobalRef(javaSideLog[x]);
		javaSideLog[x] = NULL;
                pthread_mutex_unlock(&allocator);
		
                if (!env->ExceptionOccurred() ) 
                    env->ThrowNew(env->FindClass("com/myosyn/server/data/SynSQLException"),"User not valid");
	}

}

JNIEXPORT void JNICALL Java_com_myosyn_drivers_postgres_PostgresFrameConnection_begin(JNIEnv *env, jobject talkerObject)
{
	jclass classID = env->GetObjectClass(talkerObject);
	jint link = getProperAgent(env,talkerObject,classID);

//  initialize to statement area for a new statement and free bound globals
	theManagers[link]->Init(env);
//	mark the beginning of the transaction
	theManagers[link]->Begin();
//	reporting errors to java
	reportError(env,talkerObject,classID,link);

	if ( theManagers[link]->GetErrorCode() != 0 ) 
	{
            if (!env->ExceptionOccurred() ) 
                env->ThrowNew(env->FindClass("com/myosyn/server/data/SynSQLException"),theManagers[link]->GetErrorText());
	}
}

JNIEXPORT void JNICALL Java_com_myosyn_drivers_postgres_PostgresFrameConnection_parseStatement(JNIEnv *env, jobject talkerObject, jstring thePassword)
{
	const char* 	thePass;
	long	passLen;

	jclass classID = env->GetObjectClass(talkerObject);
	jint link = getProperAgent(env,talkerObject,classID);
	jsize  statementlen = env->GetStringLength(thePassword);
	char  statement[8092];

	if ( statementlen > 8091 ) {
		if (!env->ExceptionOccurred() ) 
                    env->ThrowNew(env->FindClass("com/myosyn/server/data/SynSQLException"),"statment too long");
		return;
	}
	

	statement[statementlen] = 0;
	
// found reference and initing a statement space
	theManagers[link]->Init(env);
	env->GetStringUTFRegion(thePassword,0,statementlen,statement);	
	short code = theManagers[link]->ParseStatement(statement, statementlen);

	if ( code == -2 ) {
		if (!env->ExceptionOccurred() ) 
                    env->ThrowNew(env->FindClass("com/myosyn/server/data/SynSQLException"),"out of memory");
		return;
	}
	if (theManagers[link]->GetErrorCode() == 0)
	{
		theManagers[link]->statementParsed = 1;
	}
	else
	{
		theManagers[link]->Init(env);
	}
//  done parsing and checking for errors 
//	reporting errors to java
	reportError(env,talkerObject,classID,link);

	if ( theManagers[link]->GetErrorCode() != 0 ) 
	{
            if (!env->ExceptionOccurred() ) 
                env->ThrowNew(env->FindClass("com/myosyn/server/data/SynSQLException"),theManagers[link]->GetErrorText());
	}
//fprintf(stdout,"parse done\n");
}

JNIEXPORT void JNICALL Java_com_myosyn_drivers_postgres_PostgresFrameConnection_bind(JNIEnv *env, jobject talkerObject, jstring theVar, jint theType)
{
	jclass classID = env->GetObjectClass(talkerObject);
	jint link = getProperAgent(env,talkerObject,classID);
	jsize size = env->GetStringLength(theVar);
	char transfer[64];
	short result = 0;

	if ( size > 63 ) {
		if (!env->ExceptionOccurred() ) 
                    env->ThrowNew(env->FindClass("com/myosyn/server/data/SynSQLException"),"bind name too long");
		return;
	}

	transfer[size] = 0;

//	Add Bind
	env->GetStringUTFRegion(theVar,0, size, transfer); 
        theManagers[link]->Clean(env);
	result = theManagers[link]->AddBind(transfer,theType);

	if ( result != 0 ) {
		if (!env->ExceptionOccurred() ) 
                    env->ThrowNew(env->FindClass("com/myosyn/server/data/SynBinaryTruncation"),"Not enough space for the bind");
		return;
	}
	
//	Do error
	reportError(env,talkerObject,classID,link);
}


JNIEXPORT void JNICALL Java_com_myosyn_drivers_postgres_PostgresFrameConnection_setBind(JNIEnv *env, jobject talkerObject, jstring theVar, jobject bindPass, jstring theSlot, jint varType)
{
	if ( env->IsSameObject(bindPass,NULL) ) {
		if (!env->ExceptionOccurred() ) 
          env->ThrowNew(env->FindClass("com/myosyn/server/data/SynSQLException"),"null object passed for binding");
		return;
	}
        
	jclass	 		bindclass = env->GetObjectClass(bindPass);

	jclass classID = env->GetObjectClass(talkerObject);
	jint link = getProperAgent(env,talkerObject,classID);

	jsize varsize = env->GetStringLength(theVar);	
	jsize slotsize = env->GetStringLength(theSlot);
	char  var[64];
	char  slot[64];

	short result = 0;

	if ( varsize > 63 || slotsize > 63 ) {
		if (!env->ExceptionOccurred() ) 
                    env->ThrowNew(env->FindClass("com/myosyn/server/data/SynSQLException"),"bind name too long");
		return;
	}

	var[varsize] = 0;
	slot[slotsize] = 0;

	env->GetStringUTFRegion(theVar,0, varsize, var); 		
	env->GetStringUTFRegion(theSlot,0, slotsize, slot); 	

	jfieldID fieldID = NULL;

			
	switch (varType)
	{
		case BINDNULL:
			{
				theManagers[link]->SetLink(var,varType,NULL,0);
				break;
			}
		case BINDINTEGER:/*  integer   */
			{
				jint  jit;
				fieldID = env->GetFieldID(bindclass,slot,"I");
				if ( fieldID == NULL ) { break; }
				jit = env->GetIntField(bindPass,fieldID);
				theManagers[link]->SetLink(var,varType,&jit,4);
				break;
			}
		case BINDSTRING: /*  string */
			{
				jstring 	js;
				char   	jsCon[256];
				jsize      len;

				fieldID = env->GetFieldID(bindclass,slot,"Ljava/lang/String;");
				if ( fieldID == NULL ) { break; }
				js = (jstring)env->GetObjectField(bindPass,fieldID);
				len = env->GetStringLength(js);
				if ( len > 255) {
                                if (!env->ExceptionOccurred() ) 
                                    env->ThrowNew(env->FindClass("com/myosyn/server/data/SynBinaryTruncation"),"255");
                                    return;
				} else {
					env->GetStringUTFRegion(js,0,len,jsCon);
					jsCon[len] = 0;
					theManagers[link]->SetLink(var,varType,(void *)jsCon,0);
				}
				break;
			}
		case BINDCHARACTER:/* character */
			{
				jchar 	jct;
                                jbyte   jb;
				fieldID = env->GetFieldID(bindclass,slot,"C");
				if ( fieldID == NULL ) { break; }
				jct = env->GetCharField(bindPass,fieldID);
                                jb = (jchar)jct;
				theManagers[link]->SetLink(var,varType,&jb,2);
				break;
			}
		case BINDBOOLEAN: /*   boolean  */
			{
              jboolean   jb;
				fieldID = env->GetFieldID(bindclass,slot,"Z");
				if ( fieldID == NULL ) { break; }
				jb = env->GetBooleanField(bindPass,fieldID);
				theManagers[link]->SetLink(var,varType,&jb,1);
				break;
			}
		case BINDDOUBLE:/* double */
			{
				jbyteArray	jb;
			
				fieldID = env->GetFieldID(bindclass,slot,"[B");
				if ( fieldID == NULL ) {  break; }
				jb = (jbyteArray)env->GetObjectField(bindPass,fieldID);
				if ( jb == NULL ) { break; }
				jsize varLength = env->GetArrayLength(jb);
				signed char   buffer[8];
				if ( varLength > 8 ) {
                                        if (!env->ExceptionOccurred() ) {
                                            env->ThrowNew(env->FindClass("com/myosyn/server/data/SynSQLException"),"wrong double storage size");
                                        }
                                        return;
				}
				env->GetByteArrayRegion(jb,0,varLength,buffer); 
                                theManagers[link]->SetLink(var,varType,&movetime,sizeof(double));
				break;
			}
		case BINDBINARY:/* binary */
		case BINDBLOB:/* BLOB */
		case BINDJAVA: /* java serialized object  */
		case BINDTEXT:  /* postgres text type  */
			{
				jbyteArray	jb;
				jboolean   copy = JNI_FALSE;

				int blob_size = theManagers[link]->GetStatementBlobSize();
			
				fieldID = env->GetFieldID(bindclass,slot,"[B");
				if ( fieldID == NULL ) {  break; }
				jb = (jbyteArray)env->GetObjectField(bindPass,fieldID);
				if ( jb == NULL ) { break; }

				jsize varLength = env->GetArrayLength(jb);

				if ( varLength + 4 > blob_size ) {
		//  this is the max length you can report, java uses this number to break up BLOB
					char size[255];
					sprintf(size,"%u",blob_size);	
                                if (!env->ExceptionOccurred() ) 
                                        env->ThrowNew(env->FindClass("com/myosyn/server/data/SynBinaryTruncation"),(const char*)size);
					return;
				}

				jbyte* buffer = env->GetByteArrayElements(jb,&copy);
				theManagers[link]->SetLink(var,varType,buffer,varLength);
				env->ReleaseByteArrayElements(jb,buffer,JNI_ABORT);
			/*	env->DeleteLocalRef(jb);    */
				break;
			}
                case BINDDATE:  /*  date   */
			{
				jbyteArray	jb;
			
				fieldID = env->GetFieldID(bindclass,slot,"[B");
				if ( fieldID == NULL ) {  break; }
				jb = (jbyteArray)env->GetObjectField(bindPass,fieldID);
				if ( jb == NULL ) { break; }
				jsize varLength = env->GetArrayLength(jb);
				signed char   buffer[8];

				if ( varLength > 8 ) {
		//  this is the max length you can report, java uses this number to break up BLOB
					char size[255];
					sprintf(size,"%u",8);	
                                        if (!env->ExceptionOccurred() ) {
                                            env->ThrowNew(env->FindClass("com/myosyn/server/data/SynBinaryTruncation"),(const char*)size);
                                        }
                                        return;
				}
				env->GetByteArrayRegion(jb,0,varLength,buffer); 

/*  do it this way for endianess, java is always big endian  */
                                int bytestep = 0;
                                unsigned char*  tick = (unsigned char*)buffer;
                                double movetime = 0;
                                bool neg = ((*tick & 0x80) != 0);

                                *tick &= 0x7f;
                                for(bytestep=7;bytestep>0;bytestep--) {
                                        movetime += (*tick++ * pow((double)256,(double)bytestep));
                                }
                                movetime += *tick;
                                if ( neg ) movetime *= -1;

                                movetime /= 1000;
                                movetime -= (10957 * 86400);

                                theManagers[link]->SetLink(var,varType,&movetime,sizeof(double));
	/*			env->DeleteLocalRef(jb);     */
				break;
			}
                case BINDLONG:  /* long */
			{
				jbyteArray	jb;
			
				fieldID = env->GetFieldID(bindclass,slot,"[B");
				if ( fieldID == NULL ) {  break; }
				jb = (jbyteArray)env->GetObjectField(bindPass,fieldID);
				if ( jb == NULL ) { break; }
				jsize varLength = env->GetArrayLength(jb);
				signed char   buffer[8];

				if ( varLength > 8 ) {
		//  this is the max length you can report, java uses this number to break up BLOB
					char size[255];
					sprintf(size,"%u",8);	
                                          if (!env->ExceptionOccurred() ) {
                                                env->ThrowNew(env->FindClass("com/myosyn/server/data/SynBinaryTruncation"),(const char*)size);
                                          }
					return;
				}
				env->GetByteArrayRegion(jb,0,varLength,buffer); 

/*  do it this way for endianess, java is always big endian  */
                                int bytestep = 0;
                                unsigned char*  tick = (unsigned char*)buffer;
                                int64_t movetime = 0;
                                bool neg = ((*tick & 0x80) != 0);

                                *tick &= 0x7f;
                                for(bytestep=7;bytestep>0;bytestep--) {
                                        movetime += (*tick++ * pow((double)256,(double)bytestep));
                                }
                                movetime += *tick;
                                if ( neg ) movetime *= -1;


                                theManagers[link]->SetLink(var,varType,&movetime,8);
				break;
			}
		case BINDSTREAM:  /* stream */
			{
                            theManagers[link]->PipeConnect(link,atoi(slot),pipein);
                            theManagers[link]->SetLink(var,varType,slot,slotsize);
                            break;
			}
		case BINDDIRECT:  /* direct */
			{
                            theManagers[link]->PipeConnect(link,atoi(slot),direct_pipein);
                            theManagers[link]->SetLink(var,varType,slot,slotsize);
                            break;
			}
                default:
			break;
	}	
	
	if ( varType != BINDSTREAM && varType != BINDDIRECT && fieldID == NULL ) {
		if ( env->ExceptionOccurred() ) env->ExceptionClear();
		
		jmethodID mid = env->GetMethodID(env->GetObjectClass(bindclass),"getName","()Ljava/lang/String;");
		jstring name = (jstring)env->CallObjectMethod(bindclass,mid);
		jsize   namelen = env->GetStringLength(name);

		char  namechar[256];
		namechar[namelen] = 0;

		env->GetStringUTFRegion(name,0,namelen,namechar);
		 
		char mess[1024];
		strcpy(mess,"the variable '");
		strncat(mess,slot,200);
		strcat(mess,"' does not exist in the class ");
		strcat(mess,namechar);


		if (!env->ExceptionOccurred() ) 
                    env->ThrowNew(env->FindClass("com/myosyn/server/data/SynSQLException"),mess);
		return;
	}

//  report errors
	reportError(env,talkerObject,classID,link);
}

JNIEXPORT void JNICALL Java_com_myosyn_drivers_postgres_PostgresFrameConnection_outputLink
  (JNIEnv *env, jobject talkerObject, jint index, jint theType,jobject rr, jstring theSlot, jstring theSig, jstring theClass)
{	
	if ( env->IsSameObject(rr,NULL) ) {	
		if (!env->ExceptionOccurred() ) 
                    env->ThrowNew(env->FindClass("java/lang/NullPointerException"),"null object passed for linking");
		return;
	}
	
//	get proper agent	
	jclass classID = env->GetObjectClass(talkerObject);
	jint link = getProperAgent(env,talkerObject,classID);
	
        
//  get the c type strings for the link	
	jsize slotlen = env->GetStringLength(theSlot);
	jsize siglen = env->GetStringUTFLength(theSig);
	jsize classlen = env->GetStringUTFLength(theClass);

	char   	slot[64];
	char   sig[64];
	char  	classid[64];


	if ( slotlen > 63 || siglen > 63 || classlen > 63 ) {
		pthread_mutex_unlock(&allocator);
		if (!env->ExceptionOccurred() ) 
           env->ThrowNew(env->FindClass("com/myosyn/server/data/SynSQLException"),"link not valid");
		return;
	}

	slot[slotlen] = 0;
	sig[siglen] = 0;
	classid[classlen] = 0;
        
        env->GetStringUTFRegion(theSlot,0,slotlen,slot);
        env->GetStringUTFRegion(theClass,0,classlen,classid);
        env->GetStringUTFRegion(theSig,0,siglen,sig);

// check for valid link
        theManagers[link]->Clean(env);
        if ( theType == BINDSTREAM ) {
            theManagers[link]->PipeConnect(link,atoi(slot),pipeout);
            theManagers[link]->OutputLinker(NULL, slot, sig, classid, theType, index);
    } else if ( theType == BINDDIRECT ) {
            theManagers[link]->PipeConnect(link,atoi(slot),direct_pipeout);
            theManagers[link]->OutputLinker(NULL, slot, sig, classid, theType, index);
        } else { 
            jfieldID checkID = env->GetFieldID(env->GetObjectClass(rr),slot,sig);
            jthrowable tt = env->ExceptionOccurred();
            if ( tt ) env->ExceptionClear();
            if ( tt || checkID == NULL ) {
                jmethodID mid = env->GetMethodID(env->GetObjectClass(classID),"getName","()Ljava/lang/String;");
                jstring name = (jstring)env->CallObjectMethod(env->GetObjectClass(rr),mid);

                jsize namelen = env->GetStringLength(name);		 
                char nameutf[255];
                nameutf[namelen] = 0;

                env->GetStringUTFRegion(name,0,namelen,nameutf);

                char mess[1024];
                strcpy(mess,"the variable '");
                strncat(mess,slot,200);
                strcat(mess,"' does not exist in the class ");
                strncat(mess,nameutf,200);

                if (!env->ExceptionOccurred() ) 
                env->ThrowNew(env->FindClass("java/lang/NullPointerException"),mess);
                return;
            } else {
                theManagers[link]->OutputLinker(env->NewGlobalRef(rr), slot, sig, classid, theType,index);
            }
        }

//  report errors
	reportError(env,talkerObject,classID,link);

}


JNIEXPORT void JNICALL Java_com_myosyn_drivers_postgres_PostgresFrameConnection_execute
  (JNIEnv *env, jobject talkerObject)
{
	jclass classID = env->GetObjectClass(talkerObject);
	jint link = getProperAgent(env,talkerObject,classID);

// exec
	theManagers[link]->Exec();
// report errors
	reportError(env,talkerObject,classID,link);

	if ( theManagers[link]->GetErrorCode() != 0 ) 
	{
		if (!env->ExceptionOccurred() ) 
                    env->ThrowNew(env->FindClass("com/myosyn/server/data/SynSQLException"),theManagers[link]->GetErrorText());
	}
}

JNIEXPORT void JNICALL Java_com_myosyn_drivers_postgres_PostgresFrameConnection_fetch
  (JNIEnv *env, jobject talkerObject)
{
	jclass classID = env->GetObjectClass(talkerObject);
	jint link = getProperAgent(env,talkerObject,classID);
	
//	fetch
	if ( theManagers[link] == NULL ) return;
	theManagers[link]->Fetch();
//  pass results to java if there are no errors
	if ( theManagers[link]->GetErrorCode() == 0 )
	{
		theManagers[link]->PassResults( env );
	}
// report errors
	reportError(env,talkerObject,classID,link);
	if ( theManagers[link]->GetErrorCode() != 0 && theManagers[link]->GetErrorCode() != 1403 ) 
	{
		
		if (!env->ExceptionOccurred() ) {
       		if ( theManagers[link]->GetErrorCode() == 102 ) env->ThrowNew(env->FindClass("com/myosyn/server/data/SynBinaryTruncation"),theManagers[link]->GetErrorText());
			else env->ThrowNew(env->FindClass("com/myosyn/server/data/SynSQLException"),theManagers[link]->GetErrorText());
		} 
	}
}


JNIEXPORT void JNICALL Java_com_myosyn_drivers_postgres_PostgresFrameConnection_disposeConnection
  (JNIEnv *env, jobject talkerObject)
{
	if ( env->ExceptionOccurred() ) env->ExceptionClear();
	jclass classID = env->GetObjectClass(talkerObject);
	jint link = getProperAgent(env,talkerObject,classID);

// see if the connection was closed normally	
	jfieldID openField = env->GetFieldID(classID,"openConnection","I");
	jint open = env->GetIntField(talkerObject,openField);
	
	if ( env->ExceptionOccurred() ) env->ExceptionClear();

//  free all resoures associated with this connection
       if ( theManagers[link] != NULL ) {
            theManagers[link]->Init(env);
            delete theManagers[link];
            theManagers[link] = NULL;
        }

	if ( javaSideLog[link] != NULL &&
		env->IsSameObject(talkerObject, javaSideLog[link]))
	{
		env->DeleteGlobalRef(javaSideLog[link]);
                pthread_mutex_lock(&allocator);
		javaSideLog[link] = NULL;
                pthread_mutex_unlock(&allocator);
	}
	else
	{
		long count = 0;
		for(count=0;count<MAXBACKENDS;count++)
		{

                        if ( env->IsSameObject(talkerObject, javaSideLog[count]) )
			{

				theManagers[count]->Init(env);
				delete theManagers[count];
				theManagers[count] = NULL;
				env->DeleteGlobalRef(javaSideLog[count]);
                                pthread_mutex_lock(&allocator);			
				javaSideLog[count] = NULL;
                                pthread_mutex_unlock(&allocator);			

			}

		}
	}
}

JNIEXPORT void JNICALL Java_com_myosyn_drivers_postgres_PostgresFrameConnection_cancel
  (JNIEnv *env, jobject talkerObject)
{
	jclass classID = env->GetObjectClass(talkerObject);
	jint link = getProperAgent(env,talkerObject,classID);
	int clink = link;

//  initialize to statement area for a new statement and free bound globals

//	theManagers[link]->Init(env);
//  cancel the previous statement
	theManagers[link]->Cancel();
// report errors
//	reportError(env,talkerObject,classID,link);
}

JNIEXPORT void JNICALL Java_com_myosyn_drivers_postgres_PostgresFrameConnection_prepare(JNIEnv *env, jobject talkerObject)
{
	jclass classID = env->GetObjectClass(talkerObject);
	jint link = getProperAgent(env,talkerObject,classID);

// prepase statement for commit
	if ( theManagers[link] == NULL ) return;
	theManagers[link]->Prepare();
// report errors
	reportError(env,talkerObject,classID,link);
	if ( theManagers[link]->GetErrorCode() != 0 )
	{
		if (!env->ExceptionOccurred() ) 
                    env->ThrowNew(env->FindClass("com/myosyn/server/data/SynSQLException"),"Postgres error preparing--check error code");
	}
}


JNIEXPORT void JNICALL Java_com_myosyn_drivers_postgres_PostgresFrameConnection_commit
  (JNIEnv *env, jobject talkerObject)
{
	jclass classID = env->GetObjectClass(talkerObject);
	jint link = getProperAgent(env,talkerObject,classID);
	
	if ( theManagers[link] == NULL ) return;
//  initialize to statement area for a new statement and free bound globals
	theManagers[link]->Init(env);
//  commit the previous statement
	theManagers[link]->Commit();
// report errors
	reportError(env,talkerObject,classID,link);
	if ( theManagers[link]->GetErrorCode() != 0 )
	{
		if (!env->ExceptionOccurred() ) 
                    env->ThrowNew(env->FindClass("com/myosyn/server/data/SynSQLException"),"Postgres error commit--check error code");
	}
}

JNIEXPORT void JNICALL Java_com_myosyn_drivers_postgres_PostgresFrameConnection_rollBack
  (JNIEnv *env, jobject talkerObject)
{
	jclass classID = env->GetObjectClass(talkerObject);
	jint link = getProperAgent(env,talkerObject,classID);

	if ( theManagers[link] == NULL ) return;
//  initialize to statement area for a new statement and free bound globals
	theManagers[link]->Init(env);
//  commit the previous statement
	theManagers[link]->Rollback();
// report errors
	reportError(env,talkerObject,classID,link);
}


JNIEXPORT void JNICALL Java_com_myosyn_drivers_postgres_PostgresFrameConnection_beginProcedure
  (JNIEnv *env, jobject talkerObject)
{
	jclass classID = env->GetObjectClass(talkerObject);
	jint link = getProperAgent(env,talkerObject,classID);

	if ( theManagers[link] == NULL ) return;

//  commit the previous statement
	theManagers[link]->BeginProcedure();
// report errors
	reportError(env,talkerObject,classID,link);
}


JNIEXPORT void JNICALL Java_com_myosyn_drivers_postgres_PostgresFrameConnection_endProcedure
  (JNIEnv *env, jobject talkerObject)
{
	jclass classID = env->GetObjectClass(talkerObject);
	jint link = getProperAgent(env,talkerObject,classID);

	if ( theManagers[link] == NULL ) return;
//  commit the previous statement
	theManagers[link]->EndProcedure();
// report errors
	reportError(env,talkerObject,classID,link);
}

JNIEXPORT jlong JNICALL Java_com_myosyn_drivers_postgres_PostgresFrameConnection_getTransactionId
  (JNIEnv *env, jobject talkerObject)
{
	jclass classID = env->GetObjectClass(talkerObject);
	jint link = getProperAgent(env,talkerObject,classID);
	
	if ( theManagers[link] == NULL ) return -1;

	return theManagers[link]->transactionId;
}

JNIEXPORT void JNICALL Java_com_myosyn_drivers_postgres_PostgresFrameConnection_userLock
  (JNIEnv *env, jobject talkerObject, jstring group, jint val, jboolean lock)
{
	char 		lockswitch = (char)(lock == JNI_TRUE);
	int		sqlerror = 0;
	
	jclass classID = env->GetObjectClass(talkerObject);
	jint link = getProperAgent(env,talkerObject,classID);

	jsize namelen = env->GetStringLength(group);
	char  name[64];

	if ( namelen > 63 ) {
		if (!env->ExceptionOccurred() ) 
          env->ThrowNew(env->FindClass("com/myosyn/server/data/SynSQLException"),"userlock name is too long");
		return;
	}
	
	name[namelen] = 0;

	env->GetStringUTFRegion(group,0,namelen,name);

	theManagers[link]->UserLock(name,(uint32_t)val,lockswitch);

// report errors
	reportError(env,talkerObject,classID,link);
	sqlerror = theManagers[link]->GetErrorCode();
	if ( sqlerror != 0 && sqlerror != 1 ) 
	{
		if (!env->ExceptionOccurred() ) 
                    env->ThrowNew(env->FindClass("com/myosyn/server/data/SynSQLException"),"Postgres error user lock");
	}
}

JNIEXPORT void JNICALL Java_com_myosyn_drivers_postgres_PostgresFrameConnection_streamExec
  (JNIEnv * env, jobject talkerObject, jstring statement)
{	
	const char*  state;
	jboolean    copy;		
	jclass classID = env->GetObjectClass(talkerObject);
	jint link = getProperAgent(env,talkerObject,classID);

    currentenv[link] = env;

    state = env->GetStringUTFChars(statement,&copy);
                
    theManagers[link]->PipeConnect(link,0,pipein);
    theManagers[link]->PipeConnect(link,1,pipeout);

    theManagers[link]->StreamExec((char*)state);

    theManagers[link]->PipeDisconnect(0);
    theManagers[link]->PipeDisconnect(1);
       
	env->ReleaseStringUTFChars(statement,state);
// report errors
	reportError(env,talkerObject,classID,link);
	if ( theManagers[link]->GetErrorCode() != 0 ) 
	{
		if (!env->ExceptionOccurred() ) 
                    env->ThrowNew(env->FindClass("com/myosyn/server/data/SynSQLException"),theManagers[link]->GetErrorText());
	}
}

static jchar* convertStringBytes(char* target,char* source,int limit)
{

	return NULL;
}

static jint reportError(JNIEnv* env,jobject talkerObject,jclass talkerClass,jint link)
{
//	reporting errors to java
        if ( env->ExceptionOccurred() ) return 2;
	jfieldID	fieldID = env->GetFieldID(talkerClass, "resultField","I");

	env->SetIntField(talkerObject,fieldID,(jint)theManagers[link]->GetErrorCode());
	jfieldID  eText = env->GetFieldID(talkerClass,"errorText","Ljava/lang/String;");
	jfieldID  eState = env->GetFieldID(talkerClass,"state","Ljava/lang/String;");
	jstring et = env->NewStringUTF(theManagers[link]->GetErrorText());
	jstring st = env->NewStringUTF(theManagers[link]->GetErrorState());

	env->SetObjectField(talkerObject,eText,et);
	env->SetObjectField(talkerObject,eState,st);
	
	return 0;
}

static jint getProperAgent(JNIEnv * env,jobject talkerObject,jclass talkerClass)
{
// proper agent	
	jfieldID 	idField = env->GetFieldID(talkerClass,"id","Lcom/myosyn/server/data/LinkID;");
	jobject 	tracker = env->GetObjectField(talkerObject,idField);
	jclass 		trackerClass = env->GetObjectClass(tracker);
	jfieldID 	trackerField = env->GetFieldID(trackerClass,"linkNumber","I");
	jint 		link = env->GetIntField(tracker,trackerField);

	if ( env->ExceptionOccurred() ) {
                env->ExceptionDescribe();
                env->ExceptionClear();
        }
        currentenv[link] = env;
        
        return link;
}

static int direct_pipeout(int pipeid,int streamid,char* buff,int start,int run)
{
    if ( currentenv[pipeid] == NULL || javaSideLog[pipeid] == NULL )
            return PIPING_ERROR;

    JNIEnv* env = currentenv[pipeid];
    jclass cid = env->GetObjectClass(javaSideLog[pipeid]);
    jmethodID mid = env->GetMethodID(cid,"pipeOut","(ILjava/nio/ByteBuffer;)V");
    jobject jb = env->NewDirectByteBuffer(buff + start,run);
    
    if ( jb != NULL ) {
    	env->CallVoidMethod(javaSideLog[pipeid],mid,streamid,jb);
        if ( env->ExceptionOccurred() ) {
            env->ExceptionDescribe();
            env->ExceptionClear();
            return PIPING_ERROR;
        } else {
            return run;
        }
    } else {
        if ( env->ExceptionOccurred() ) {
            env->ExceptionDescribe();
            env->ExceptionClear();
            return PIPING_ERROR;
        }
        return -1;
    }
}

static int direct_pipein(int pipeid,int streamid,char* buff,int start,int run)
{
	if ( currentenv[pipeid] == NULL || javaSideLog[pipeid] == NULL )
		return PIPING_ERROR;
    JNIEnv* env = currentenv[pipeid];
    jclass cid = env->GetObjectClass(javaSideLog[pipeid]);
    jmethodID mid = env->GetMethodID(cid,"pipeIn","(ILjava/nio/ByteBuffer;)I");
    jobject jb = env->NewDirectByteBuffer(buff + start,run);

    if ( jb != NULL ) {
    	jint count = env->CallIntMethod(javaSideLog[pipeid],mid,streamid,jb);
        if ( env->ExceptionOccurred() ) {
            env->ExceptionDescribe();
            env->ExceptionClear();
            return PIPING_ERROR;
        }
        
    	return count;
    } else {
        if ( env->ExceptionOccurred() ) {
            env->ExceptionDescribe();
            env->ExceptionClear();
            return PIPING_ERROR;
        }

        return -1;
    }
}

static int pipeout(int pipeid,int streamid,char* buff,int start,int run)
{
	if ( currentenv[pipeid] == NULL || javaSideLog[pipeid] == NULL )
		return PIPING_ERROR;

    JNIEnv* env = currentenv[pipeid];
    jclass cid = env->GetObjectClass(javaSideLog[pipeid]);
    jmethodID mid = env->GetMethodID(cid,"pipeOut","(I[B)V");
    jbyteArray jb = env->NewByteArray(run);

    if ( jb != NULL ) {
    	env->SetByteArrayRegion(jb,0,run,(jbyte*)(buff + start));
    	env->CallVoidMethod(javaSideLog[pipeid],mid,streamid,jb);
        if ( env->ExceptionOccurred() ) {
            env->ExceptionDescribe();
            env->ExceptionClear();
            return PIPING_ERROR;
        } else {
            return run;
        }
    } else {
        if ( env->ExceptionOccurred() ) {
            env->ExceptionDescribe();
            env->ExceptionClear();
            return PIPING_ERROR;
        }
        fprintf(stderr,"null byte buffer -- pipeout\n");
        return PIPING_ERROR;
    }
}

static int pipein(int pipeid,int streamid,char* buff,int start,int run)
{
    if ( currentenv[pipeid] == NULL || javaSideLog[pipeid] == NULL )
            return PIPING_ERROR;
    JNIEnv* env = currentenv[pipeid];
    jclass cid = env->GetObjectClass(javaSideLog[pipeid]);
    jmethodID mid = env->GetMethodID(cid,"pipeIn","(I[B)I");
    jbyteArray jb = env->NewByteArray(run);

    if ( jb != NULL ) {
    	jint count = env->CallIntMethod(javaSideLog[pipeid],mid,streamid,jb);
        if ( env->ExceptionOccurred() ) {
            env->ExceptionDescribe();
            env->ExceptionClear();
            return PIPING_ERROR;
        }
        if ( count > 0 ) {
            env->GetByteArrayRegion(jb,0,count,(jbyte*)(buff + start));
        }
    	return count;
    } else {
        if ( env->ExceptionOccurred() ) {
            env->ExceptionDescribe();
            env->ExceptionClear();
            return PIPING_ERROR;
        }
        fprintf(stderr,"null byte buffer -- pipein\n");
        return PIPING_ERROR;
    }
}

