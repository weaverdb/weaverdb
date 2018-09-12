/*-------------------------------------------------------------------------
 *
 *	WeaverStmtManager.h
 *		C++ interface between weaver base and java interface
 *
 * Portions Copyright (c) 2002-2006, Myron K Scott
 *
 *
 * IDENTIFICATION
 *	  $Header: /cvs/weaver/Wjava/WeaverStmtManager.h,v 1.3 2006/10/12 17:20:54 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */

#ifndef _WEAVERSTMTMANAGER_H_
#define _WEAVERSTMTMANAGER_H_

#include <sys/types.h>
#include "env/WeaverInterface.h"

  
#define MAX_FIELDS  0
#define MAX_STMTSIZE  (BLOBSIZE * 32)

typedef struct bound *  Bound;
typedef struct outputObj * Output;
typedef struct bindObj * Input;

#define OutputToBound(bound) ( (Bound)bound )
#define InputToBound(bound) ( (Bound)bound )

typedef struct WeaverConnectionManager* ConnMgr;
typedef struct WeaverStmtManager* StmtMgr;

typedef int (*outputfunc)(StmtMgr , int type, void* value, int length, void* userarg, void* funcarg);
typedef int (*usercleanup)(StmtMgr , int type, void* userarg);

ConnMgr CreateWeaverConnection(const char* name, const char * paslong, const char* connect);
ConnMgr GetWeaverConnection(StmtMgr );
void DestroyWeaverConnection( ConnMgr );
StmtMgr CreateWeaverStmtManager(ConnMgr connection);
short  IsValid(ConnMgr mgr);
short  IsStmtValid(StmtMgr mgr);

StmtMgr  CreateSubConnection(StmtMgr );
ConnMgr DestroyWeaverStmtManager( StmtMgr );

short Begin( StmtMgr );
short Rollback( StmtMgr );
short Commit( StmtMgr );
long  GetTransactionId( StmtMgr );
long  GetCommandId( StmtMgr );

short Exec( StmtMgr );
short Cancel( StmtMgr );

short Prepare( StmtMgr );
short BeginProcedure( StmtMgr );
short EndProcedure( StmtMgr );

short UserLock(StmtMgr , const char* grouptolock,uint32_t val,char lock);

short	Init(StmtMgr ,usercleanup input,usercleanup output);

Pipe     PipeConnect(StmtMgr , void* args, pipefunc func);
void*    PipeDisconnect(StmtMgr , Pipe comm);
void     ConnectStdIO(StmtMgr ,void* args,pipefunc pipein,pipefunc pipeout);
void     DisconnectStdIO(StmtMgr );

short StreamExec(StmtMgr ,char* statement);

short ParseStatement( StmtMgr ,const char* statement);
short Fetch( StmtMgr  );
long Count( StmtMgr  );

Input SetInputValue(StmtMgr , const char * vari, short type, void* value, int length);
Output SetOutputValue(StmtMgr , int index, short type, void* value, int length);

void* SetUserspace(StmtMgr , Bound bound, void* user);

Output OutputLink(StmtMgr , int index, short type);
short GetOutputs(StmtMgr , void* funcargs, outputfunc func);

void ClearData(StmtMgr );

short SetStatementSpaceSize(StmtMgr ,long size);
short SetStatementBlobSize(StmtMgr ,long size);
long  GetStatementSpaceSize(StmtMgr );
long  GetStatementBlobSize(StmtMgr );

short DelegateError(StmtMgr ,const char* state,const char* text,int code);
short CheckForErrors(StmtMgr );

long GetErrorCode(StmtMgr );
const char* GetErrorText(StmtMgr );
const char* GetErrorState(StmtMgr );
#endif

