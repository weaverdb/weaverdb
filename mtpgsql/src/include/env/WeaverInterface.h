

#ifndef _WEAVERINTERFACE_H_
#define _WEAVERINTERFACE_H_

#include <sys/types.h>
#include <setjmp.h>
#include "config.h"


/*  leave some space for line item ids and header info try and squeeze two
    BLOBs per page  */

#define BLOBSIZE  (BLCKSZ)

#define INT4TYPE	23
#define VARCHARTYPE	1043
#define BOOLTYPE	16
#define CHARTYPE	18
#define METANAMETYPE	19   // column name transfer
#define BYTEATYPE        17
#define TEXTTYPE         25
#define BLOBTYPE         1803
#define JAVATYPE         1830
#define NATIVEJAVATYPE         30000
#define CONNECTORTYPE         1136
#define TIMESTAMPTYPE    1184
#define DOUBLETYPE    701
#define LONGTYPE    20
#define SLOTTYPE     	1901
#define ARRAYTYPE	1902
#define PATTERNTYPE	1903
#define FUNCTIONTYPE	7733
#define STREAMTYPE	1834

#ifdef __cplusplus
extern "C" {
#endif

typedef struct error {
	int rc;
	short place;
	char  state[40];
	char text[256];
} Error;

typedef int (*transferfunc)(void* userenv, int varType, void *varAdd, int varSize);
#define PIPING_ERROR  -2
#define NULL_VALUE  -1
#define TRUNCATION_VALUE -32
#define CLOSE_OP -4
#define LENGTH_QUERY_OP -8
#define NULL_CHECK_OP -16

typedef struct Connection* OpaqueWConn;
typedef struct preparedplan* OpaquePreparedStatement;
typedef struct commbuffer*   Pipe;

LIB_EXTERN OpaqueWConn WCreateConnection(const char* name, const char * paslong, const char* connect);
LIB_EXTERN OpaqueWConn WCreateSubConnection(OpaqueWConn  conn);
LIB_EXTERN long WDestroyConnection(OpaqueWConn  conn);
LIB_EXTERN long WBegin(OpaqueWConn conn,long trans);
LIB_EXTERN OpaquePreparedStatement WPrepareStatement(OpaqueWConn conn,const char* Statement);
LIB_EXTERN long WDestroyPreparedStatement(OpaquePreparedStatement stmt);
LIB_EXTERN char* WStatement(OpaquePreparedStatement stmt);
LIB_EXTERN long WBindTransfer(OpaquePreparedStatement stmt, const char* var, int type, void* userenv, transferfunc func); 
LIB_EXTERN long WOutputTransfer(OpaquePreparedStatement stmt, short pos, int type, void* userenv, transferfunc func);
LIB_EXTERN long WExec(OpaquePreparedStatement conn );
LIB_EXTERN long WFetch( OpaquePreparedStatement conn );
LIB_EXTERN long WPrepare(OpaqueWConn conn );
LIB_EXTERN long WCommit( OpaqueWConn conn );
LIB_EXTERN long WRollback(OpaqueWConn conn );
LIB_EXTERN long WCancel( OpaqueWConn conn );
LIB_EXTERN long WCancelAndJoin(OpaqueWConn conn);
LIB_EXTERN long WBeginProcedure(OpaqueWConn conn );
LIB_EXTERN long WEndProcedure( OpaqueWConn conn );
LIB_EXTERN long WUserLock(OpaqueWConn conn, const char* group,uint32_t val,char lockit);
LIB_EXTERN long WIsValidConnection(OpaqueWConn conn);
LIB_EXTERN long WGetTransactionId(OpaqueWConn conn);
LIB_EXTERN long WGetCommandId(OpaqueWConn conn);

LIB_EXTERN void* WAllocConnectionMemory(OpaqueWConn conn, size_t size);
LIB_EXTERN void* WAllocTransactionMemory(OpaqueWConn conn, size_t size);
LIB_EXTERN void* WAllocStatementMemory(OpaquePreparedStatement conn, size_t size);
LIB_EXTERN void WFreeMemory(OpaqueWConn conn, void* pointer);
LIB_EXTERN void WCheckMemory(OpaqueWConn conn);

LIB_EXTERN long WExecCount(OpaquePreparedStatement conn);
LIB_EXTERN long WFetchIsComplete(OpaquePreparedStatement conn);
LIB_EXTERN long WGetErrorCode(OpaqueWConn conn);
LIB_EXTERN const char* WGetErrorText(OpaqueWConn conn);
LIB_EXTERN const char* WGetErrorState(OpaqueWConn conn);

LIB_EXTERN long WStreamExec(OpaqueWConn conn,const char* statement);

LIB_EXTERN void WConnectStdIO(OpaqueWConn conn,void* pipeargs,transferfunc in,transferfunc out);
LIB_EXTERN void* WDisconnectStdIO(OpaqueWConn conn);

LIB_EXTERN int WPipeSize(OpaqueWConn conn);

#ifdef __cplusplus
}
#endif

#endif
