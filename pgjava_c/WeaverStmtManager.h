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
typedef int (*inputfunc)(StmtMgr , int type, void* userarg);

ConnMgr CreateWeaverConnection(const char* name, const char * paslong, const char* connect);
void DestroyWeaverConnection( ConnMgr );

StmtMgr CreateWeaverStmtManager(ConnMgr connection);
void DestroyWeaverStmtManager(ConnMgr,  StmtMgr );

short  IsValid(ConnMgr mgr);

ConnMgr  CreateSubConnection(ConnMgr );

short Begin( ConnMgr );
short Rollback( ConnMgr );
short Commit( ConnMgr );
long  GetTransactionId( ConnMgr );
long  GetCommandId( StmtMgr );

short Exec(ConnMgr, StmtMgr );
short Cancel( ConnMgr );

short Prepare( ConnMgr );
short BeginProcedure( ConnMgr );
short EndProcedure( ConnMgr );

short UserLock(ConnMgr, StmtMgr , const char* grouptolock,uint32_t val,char lock);

Pipe     PipeConnect(ConnMgr ,StmtMgr , void* args, pipefunc func);
void*    PipeDisconnect(ConnMgr ,StmtMgr , Pipe comm);
void     ConnectStdIO(ConnMgr ,void* args,pipefunc pipein,pipefunc pipeout);
void     DisconnectStdIO(ConnMgr );

short StreamExec(ConnMgr ,char* statement);

short ParseStatement(ConnMgr, StmtMgr ,const char* statement);
short Fetch( ConnMgr, StmtMgr  );
long Count( StmtMgr  );

Input SetInputValue(ConnMgr, StmtMgr , const char * vari, short type, void* value, int length);
Output SetOutputValue(ConnMgr, StmtMgr , int index, short type, void* value, int length);

void* SetUserspace(Bound bound, void* user);

Output OutputLink(ConnMgr, StmtMgr , int index, short type);
short GetOutputs(StmtMgr , void* funcargs, outputfunc func);

short DisconnectPipes(ConnMgr, StmtMgr);
void ClearData(StmtMgr );

short SetStatementSpaceSize(ConnMgr, StmtMgr ,long size);
short SetStatementBlobSize(ConnMgr, StmtMgr ,long size);
long  GetStatementSpaceSize(StmtMgr );
long  GetStatementBlobSize(StmtMgr );

short DelegateError(StmtMgr ,const char* state,const char* text,int code);
short CheckForErrors(ConnMgr, StmtMgr );

long ReportError(ConnMgr, StmtMgr, const char** text, const char** state);

long GetErrorCode(ConnMgr, StmtMgr );
const char* GetErrorText(ConnMgr, StmtMgr );
const char* GetErrorState(ConnMgr, StmtMgr );
#endif

