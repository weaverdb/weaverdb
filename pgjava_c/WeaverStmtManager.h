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

  
#define MAX_FIELDS  20
#define MAX_STMTSIZE  (BLOBSIZE * 32)

typedef struct bound *  Bound;
typedef struct outputObj * Output;
typedef struct bindObj * Input;

#define OutputToBound(bound) ( (Bound)bound )
#define InputToBound(bound) ( (Bound)bound )

typedef struct WeaverStmtManager* StmtMgr;

typedef int (*outputfunc)(StmtMgr mgr, int type, void* value, int length, void* userarg, void* funcarg);
typedef int (*usercleanup)(StmtMgr mgr, int type, void* userarg);

StmtMgr CreateWeaverStmtManager(const char* name, const char * paslong, const char* connect);
StmtMgr  CreateSubConnection(StmtMgr mgr);
void DestroyWeaverStmtManager( StmtMgr target );
short  IsValid( StmtMgr );

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

short UserLock(StmtMgr mgr, const char* grouptolock,uint32_t val,char lock);

void	Clean( StmtMgr mgr,usercleanup input,usercleanup output);
void	Init(StmtMgr mgr,usercleanup input,usercleanup output);

Pipe     PipeConnect(StmtMgr mgr, void* args, pipefunc func);
void*    PipeDisconnect(StmtMgr mgr, Pipe comm);
void     ConnectStdIO(StmtMgr mgr,void* args,pipefunc pipein,pipefunc pipeout);
void     DisconnectStdIO(StmtMgr mgr);

short StreamExec(StmtMgr mgr,char* statement);

short ParseStatement( StmtMgr mgr,const char* thePass, long passLen);
short Fetch( StmtMgr );

Input AddBind(StmtMgr mgr, const char * vari, short type);
Input GetBind(StmtMgr mgr, const char * vari);

int SetBoundValue(StmtMgr mgr, Bound bound, void* value, int length);
short GetType(StmtMgr mgr, Bound bound);
void* SetUserspace(StmtMgr mgr, Bound bound,void* user);

Output OutputLink(StmtMgr mgr, int index, short type);
short GetOutputs(StmtMgr mgr, void* funcargs, outputfunc func);

void ClearData(StmtMgr mgr);

short SetStatementSpaceSize(StmtMgr mgr,long size);
short SetStatementBlobSize(StmtMgr mgr,long size);
long  GetStatementSpaceSize(StmtMgr mgr );
long  GetStatementBlobSize(StmtMgr mgr );

short DelegateError(StmtMgr mgr,const char* state,const char* text,int code);
short CheckForErrors(StmtMgr mgr );

long GetErrorCode(StmtMgr mgr );
const char* GetErrorText(StmtMgr mgr );
const char* GetErrorState(StmtMgr mgr );
#endif

