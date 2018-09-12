/*-------------------------------------------------------------------------
 *
 *	PostgresStmtManager.h
 *		Statement Manager talks to PostgresConnection
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *
 *-------------------------------------------------------------------------
 */
 /*   have to undefine these here b/c postgres defines them as
 part of C code MKS 10.10.2001  */

#ifndef _POSTGRESSTMTMANAGER_H_
#define _POSTGRESSTMTMANAGER_H_

#ifdef PG_JAVA
#include  <jni.h>
#endif
/*
#include <stdlib.h>
#include <unistd.h>
*/
#include <sys/types.h>
#include "env/PostgresInterface.h"
#include "PostgresFrameConnection.h"

#define BINDNULL	com_myosyn_drivers_postgres_PostgresFrameConnection_bindNull
#define BINDINTEGER  com_myosyn_drivers_postgres_PostgresFrameConnection_bindInteger
#define BINDSTRING  com_myosyn_drivers_postgres_PostgresFrameConnection_bindString
#define BINDDOUBLE  com_myosyn_drivers_postgres_PostgresFrameConnection_bindDouble
#define BINDCHARACTER  com_myosyn_drivers_postgres_PostgresFrameConnection_bindCharacter
#define BINDBOOLEAN  com_myosyn_drivers_postgres_PostgresFrameConnection_bindBoolean
#define BINDBINARY  com_myosyn_drivers_postgres_PostgresFrameConnection_bindBinary
#define BINDBLOB  com_myosyn_drivers_postgres_PostgresFrameConnection_bindBLOB
#define BINDDATE  com_myosyn_drivers_postgres_PostgresFrameConnection_bindDate
#define BINDDOUBLE  com_myosyn_drivers_postgres_PostgresFrameConnection_bindDouble
#define BINDLONG  com_myosyn_drivers_postgres_PostgresFrameConnection_bindLong
#define BINDFUNCTION  com_myosyn_drivers_postgres_PostgresFrameConnection_bindFunction
#define BINDSLOT com_myosyn_drivers_postgres_PostgresFrameConnection_bindSlot
#define BINDJAVA  com_myosyn_drivers_postgres_PostgresFrameConnection_bindJava
#define BINDTEXT  com_myosyn_drivers_postgres_PostgresFrameConnection_bindText
#define BINDSTREAM com_myosyn_drivers_postgres_PostgresFrameConnection_bindStream
#define BINDDIRECT com_myosyn_drivers_postgres_PostgresFrameConnection_bindDirect
    
/*  class is defined in the Postgres source so we need to
undefine it here  */

struct outputObj {
	void*			theObjectRef;
	long			index;
#ifdef PG_JAVA
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


class  PostgresStmtManager {
	public:
		short				statementParsed;
		short				bindFlag;
		
		long				transactionId;	
		void*				dataStack;
		long				stackSize;
		long				blob_size;
		
		Error				errordelegate;
		long				errorlevel;
		PostgresStmtManager( const char* name, const char * paslong, const char* connect );
		PostgresStmtManager( PostgresStmtManager* parent );
		~PostgresStmtManager( void );
                
                PostgresStmtManager*  CreateSubConnection();

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
#ifdef PG_JAVA
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

