/*-------------------------------------------------------------------------
 *
 *	WeaverStmtManager.h
 *		C++ interface between weaver base and java interface
 *
 * Portions Copyright (c) 2002-2006, Myron K Scott
 *
 *
 * IDENTIFICATION
 *	  $Header: /cvs/weaver/pgjava/WeaverStmtManager.h,v 1.3 2006/10/12 17:20:54 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */

#ifndef _WEAVERSTMTMANAGER_H_
#define _WEAVERSTMTMANAGER_H_

#ifdef WEAVER_JAVA
#include  <jni.h>
#endif

#include <sys/types.h>
#include "env/WeaverInterface.h"
#include "driver_weaver_BaseWeaverConnection.h"

#define BINDNULL	driver_weaver_BaseWeaverConnection_bindNull
#define BINDINTEGER  driver_weaver_BaseWeaverConnection_bindInteger
#define BINDSTRING  driver_weaver_BaseWeaverConnection_bindString
#define BINDDOUBLE  driver_weaver_BaseWeaverConnection_bindDouble
#define BINDCHARACTER  driver_weaver_BaseWeaverConnection_bindCharacter
#define BINDBOOLEAN  driver_weaver_BaseWeaverConnection_bindBoolean
#define BINDBINARY  driver_weaver_BaseWeaverConnection_bindBinary
#define BINDBLOB  driver_weaver_BaseWeaverConnection_bindBLOB
#define BINDDATE  driver_weaver_BaseWeaverConnection_bindDate
#define BINDDOUBLE  driver_weaver_BaseWeaverConnection_bindDouble
#define BINDLONG  driver_weaver_BaseWeaverConnection_bindLong
#define BINDFUNCTION  driver_weaver_BaseWeaverConnection_bindFunction
#define BINDSLOT driver_weaver_BaseWeaverConnection_bindSlot
#define BINDJAVA  driver_weaver_BaseWeaverConnection_bindJava
#define BINDTEXT  driver_weaver_BaseWeaverConnection_bindText
#define BINDSTREAM driver_weaver_BaseWeaverConnection_bindStream
#define BINDDIRECT driver_weaver_BaseWeaverConnection_bindDirect
    

struct outputObj {
	void*			theObjectRef;
	long			index;
#ifdef WEAVER_JAVA
	char			slotName[255];
	char			signature[255];
	char			className[255];
#endif
	long			pointerRef;
	int			clength;
	short			indicator;
	short			type;
};


struct bindObj {
	char			binder[255];
	long			pointerRef;
	long			type;
	short			indicator;
	short			numBind;
};


class  WeaverStmtManager {
	public:
		short				statementParsed;
		short				bindFlag;
		
		long				transactionId;	
		void*				dataStack;
		long				stackSize;
		long				blob_size;
		
		Error				errordelegate;
		long				errorlevel;
		WeaverStmtManager( const char* name, const char * paslong, const char* connect );
		WeaverStmtManager( WeaverStmtManager* parent );
		~WeaverStmtManager( void );
                
                WeaverStmtManager*  CreateSubConnection();

		short  IsValid( void );
		short Begin( void );
		short Fetch( void );
		short Rollback( void );
		short Commit( void );
		short Exec( void );
		short Cancel( void );
		short Prepare( void );

		short BeginProcedure( void );
		short EndProcedure( void );

		short UserLock(const char* grouptolock,uint32_t val,char lock);
#ifdef WEAVER_JAVA
		void	Clean(JNIEnv* e );
		void	Init(JNIEnv* e );
		short PassResults( JNIEnv* env );

                long StreamExec(char* statement);
                long PipeConnect(int pipeid,int streamid, pipefunc func);
                long PipeDisconnect( int streamid );

		short OutputLinker(jobject theRef, const char* slotname,const char* signature,
		const char* className, long theType, long index);
#else
		void	Clean( void );
		void	Init( void );
		short OutputLinker(void* theRef,long theType,long index);
#endif		
		
		short ParseStatement( const char* thePass, long passLen);
		short AddBind( const char * vari, long theType);
		short SetLink(const char * vari,long theType,void* theData,int theLength);		

		short GetOutputRef(short index,long theType,void** ref);
		void ClearData( void );
		void* Advance(long size);
		
		short SetStatementSpaceSize(long size);
		short SetStatementBlobSize(long size);
		
		long  GetStatementSpaceSize( void );
		long  GetStatementBlobSize( void );
		
		short DelegateError(const char* state,const char* text,int code);
		short CheckLowerError( void );
		
		int GetErrorCode( void );
		const char* GetErrorText( void );
		const char* GetErrorState( void );
                		
	protected:
		OpaquePGConn 			theConn;  
                
		long				holdingArea;
		long				statementLength;
		
		bindObj				bindLog[20];
		outputObj			outputLog[20];
	
		Pipe				stdpipein;
		Pipe				stdpipeout;
		Pipe				pipes[20];
	private:
		long  align( long pointer);
                bool    clean;
};
#endif

