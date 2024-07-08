/*-------------------------------------------------------------------------
 *
 *	WeaverStmtManager.h
 *		C interface between weaver base and java interface
 *
 * Copyright (c) 2000-2024, Myron Scott  <myron@weaverdb.org>
 *
 *
 *
 *-------------------------------------------------------------------------
 */

#ifndef _WEAVERSTMTMANAGER_H_
#define _WEAVERSTMTMANAGER_H_

#include <sys/types.h>
#include "WeaverInterface.h"

  
#define MAX_FIELDS  32
#define MAX_STATEMENTS 8196

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

short StreamExec(ConnMgr ,const char* statement);

short ParseStatement(ConnMgr, StmtMgr ,const char* statement);
short Fetch( ConnMgr, StmtMgr  );
long Count( StmtMgr  );

Input LinkInput(ConnMgr conn, StmtMgr mgr, const char* var, short type, void* data, transferfunc func);
Output LinkOutput(ConnMgr conn, StmtMgr mgr, int index, short type, void* data, transferfunc func);

short DelegateError(StmtMgr ,const char* state,const char* text,int code);
short CheckForErrors(ConnMgr, StmtMgr );

long ReportError(ConnMgr, StmtMgr, const char** text, const char** state);

#endif

