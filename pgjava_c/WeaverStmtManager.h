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

void     ConnectStdIO(ConnMgr ,void* args,transferfunc pipein,transferfunc pipeout);
void     DisconnectStdIO(ConnMgr );

short StreamExec(ConnMgr ,char* statement);

short ParseStatement(ConnMgr, StmtMgr ,const char* statement);
short Fetch( ConnMgr, StmtMgr  );
long Count( StmtMgr  );

Input LinkInput(ConnMgr conn, StmtMgr mgr, const char* var, short type, void* data, transferfunc func);
Output LinkOutput(ConnMgr conn, StmtMgr mgr, int index, short type, void* data, transferfunc func);

short DelegateError(StmtMgr ,const char* state,const char* text,int code);
short CheckForErrors(ConnMgr, StmtMgr );

long ReportError(ConnMgr, StmtMgr, const char** text, const char** state);

long GetErrorCode(ConnMgr, StmtMgr );
const char* GetErrorText(ConnMgr, StmtMgr );
const char* GetErrorState(ConnMgr, StmtMgr );
#endif

