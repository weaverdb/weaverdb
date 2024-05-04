/*-------------------------------------------------------------------------
 *
 *	WeaverStmtManager.cc 
 *		Statement interface and packaging for java to C
 *
 * Portions Copyright (c) 2002-2006 Myron K Scott
 *
 *
 * IDENTIFICATION
 *
 *	  $Header: /cvs/weaver/pgjava/WeaverStmtManager.cc,v 1.6 2007/05/31 15:03:54 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include <stdio.h>
#include <pthread.h>

#include "WeaverStmtManager.h"
#include "env/WeaverInterface.h"  

struct bound {
    long pointer;
    int maxlength;
    void* userspace;
    short byval;
    short indicator;
    short type;
};

typedef struct outputObj {
    struct bound base;
    long index;
    int clength;
} outputDef;

typedef struct bindObj {
    struct bound base;
    char binder[64];
} inputDef;

typedef struct WeaverConnectionManager {
    OpaqueWConn theConn;
    long transactionId;
    int refCount;
    StmtMgr statements[256];
    pthread_mutex_t control;
} WeaverConnectionManager;

typedef struct WeaverStmtManager {
    long commandId;
    void* dataStack;
    long stackSize;
    long blob_size;

    Error errordelegate;
    long errorlevel;

    OpaquePreparedStatement statement;

    long holdingArea;

    inputDef* inputLog;
    outputDef* outputLog;

    short log_count;

} WeaverStmtManager;

static Output GetLink(ConnMgr, StmtMgr mgr, int index, short type);
static Output AddLink(ConnMgr, StmtMgr mgr, Output bind);
static Input GetBind(ConnMgr, StmtMgr mgr, const char * vari, short type);
static Input AddBind(ConnMgr, StmtMgr mgr, Input bind);
static long align(StmtMgr mgr, long pointer);
static void* Advance(StmtMgr mgr, long size);
static short ExpandBindings(ConnMgr, StmtMgr mgr);
static short ResetBindings(StmtMgr mgr);

ConnMgr
CreateWeaverConnection(const char* name, const char * paslong, const char* connect) {
    OpaqueWConn conn = WCreateConnection(name, paslong, connect);
    if (!WIsValidConnection(conn)) {
        WDestroyConnection(conn);
        return NULL;
    } else {
        ConnMgr mgr = (WeaverConnectionManager*) WAllocConnectionMemory(conn, sizeof (WeaverConnectionManager));
        pthread_mutex_init(&mgr->control, NULL);
        mgr->refCount = 0;
        mgr->theConn = conn;
        return mgr;
    }
}

StmtMgr
CreateWeaverStmtManager(ConnMgr connection) {
    StmtMgr mgr = NULL;
    int counter = 0;

    pthread_mutex_lock(&connection->control);
    if (WIsValidConnection(connection->theConn)) {

        mgr = (StmtMgr) WAllocConnectionMemory(connection->theConn, sizeof (WeaverStmtManager));
        if (mgr != NULL) {
            connection->statements[connection->refCount++] = mgr;

            mgr->statement = NULL;

            mgr->holdingArea = 0;

            mgr->blob_size = BLOBSIZE;
            mgr->stackSize = mgr->blob_size * 2;
            mgr->dataStack = WAllocConnectionMemory(connection->theConn, mgr->stackSize);

            mgr->errorlevel = 0;
            memset(&mgr->errordelegate, 0x00, sizeof (Error));

            mgr->log_count = MAX_FIELDS;

            if (mgr->log_count > 0) {
                mgr->inputLog = WAllocConnectionMemory(connection->theConn, sizeof (inputDef) * mgr->log_count);
                mgr->outputLog = WAllocConnectionMemory(connection->theConn, sizeof (outputDef) * mgr->log_count);
                /*  zero statement structures */
                for (counter = 0; counter < mgr->log_count; counter++) {
                    memset(&mgr->outputLog[counter], 0, sizeof (outputDef));
                    mgr->outputLog[counter].base.indicator = -1;
                    mgr->outputLog[counter].base.byval = 0;
                    mgr->outputLog[counter].base.userspace = NULL;
                    memset(&mgr->inputLog[counter], 0, sizeof (inputDef));
                    mgr->inputLog[counter].base.indicator = -1;
                    mgr->inputLog[counter].base.byval = 0;
                    mgr->inputLog[counter].base.userspace = NULL;
                }
            } else {
                mgr->inputLog = NULL;
                mgr->outputLog = NULL;
            }
        }
    }
    pthread_mutex_unlock(&connection->control);

    return mgr;
}

void DestroyWeaverConnection(ConnMgr mgr) {
    if (mgr == NULL) return;
    if ( !IsValid(mgr) ) {
        return;
    }
    pthread_mutex_lock(&mgr->control);
    if (mgr->theConn != NULL) {
        OpaqueWConn connection = mgr->theConn;
        /*  it's possible the the owning thread is not the
         *  destroying thread so to a cancel/join for
         *  safety's sake.
         */
        WCancelAndJoin(mgr->theConn);
        pthread_mutex_destroy(&mgr->control);
        
        WDestroyConnection(connection);
    } else {
        pthread_mutex_unlock(&mgr->control);
    }
}

void DestroyWeaverStmtManager(ConnMgr conn, StmtMgr mgr) {
    int x;
    if (mgr == NULL) return;

    if ( !IsValid(conn) ) {
        return;
    }

    pthread_mutex_lock(&conn->control);

    for (x=0;x<conn->refCount;x++) {
        if (mgr == conn->statements[x]) {
            conn->statements[x] = conn->statements[x+1];
            conn->statements[x+1] = mgr;
        }
    }
    if (conn->statements[conn->refCount] == mgr) {
        conn->refCount -= 1;
    }
    DisconnectPipes(conn, mgr);
    if (mgr->statement != NULL) WDestroyPreparedStatement(mgr->statement);
    if (mgr->dataStack != NULL) WFreeMemory(conn->theConn, mgr->dataStack);
    if (mgr->inputLog != NULL) WFreeMemory(conn->theConn, mgr->inputLog);
    if (mgr->outputLog != NULL) WFreeMemory(conn->theConn, mgr->outputLog);

    WFreeMemory(conn->theConn, mgr);
    pthread_mutex_unlock(&conn->control);
}

ConnMgr
CreateSubConnection(ConnMgr parent) {
    if (parent == NULL) return NULL;
    OpaqueWConn connection = WCreateSubConnection(parent->theConn);
    ConnMgr mgr = (WeaverConnectionManager*) WAllocConnectionMemory(connection, sizeof (WeaverConnectionManager));
    pthread_mutex_init(&mgr->control, NULL);
    mgr->refCount = 0;
    mgr->theConn = connection;
    mgr->transactionId = 0;
    return mgr;
}

short IsValid(ConnMgr mgr) {
    if (mgr == NULL) return 0;
    if (mgr->theConn == NULL) return (short) 0;
    return (short) (WIsValidConnection(mgr->theConn));
}

short Begin(ConnMgr conn) {
    if (!IsValid(conn)) return -1;

    if (WBegin(conn->theConn, 0) == 0) {
        conn->transactionId = WGetTransactionId(conn->theConn);
    }

    return WGetErrorCode(conn->theConn) == 0 ? 0 : 1; 
}

short Fetch(ConnMgr conn, StmtMgr mgr) {
    long val = WFetch(mgr->statement);
    if (val == 4) return 1;
    return CheckForErrors(conn, mgr);
}

long Count(StmtMgr mgr) {
    return WExecCount(mgr->statement);
}

short Cancel(ConnMgr conn) {
    if (!IsValid(conn)) return -1;

    WCancel(conn->theConn);
    return WGetErrorCode(conn->theConn) == 0 ? 0 : 1; 
}

short Prepare(ConnMgr conn) {
    if (!IsValid(conn)) return -1;

    WPrepare(conn->theConn);
    return WGetErrorCode(conn->theConn) == 0 ? 0 : 1; 
}

short BeginProcedure(ConnMgr conn) {
    if (!IsValid(conn)) return -1;

    WBeginProcedure(conn->theConn);
    return WGetErrorCode(conn->theConn) == 0 ? 0 : 1; 
}

short EndProcedure(ConnMgr conn) {
    if (!IsValid(conn)) return -1;

    WEndProcedure(conn->theConn);
    return WGetErrorCode(conn->theConn) == 0 ? 0 : 1; 
}

short Exec(ConnMgr conn, StmtMgr mgr) {
    short err = 0;

    WExec(mgr->statement);
    err = CheckForErrors(conn, mgr);
    if (!err) {
        mgr->commandId = WGetCommandId(conn->theConn);
    }

    return err;
}

long GetTransactionId(ConnMgr conn) {
    if (!IsValid(conn)) return -1;
    return conn->transactionId;
}

long GetCommandId(StmtMgr mgr) {
    return mgr->commandId;
}

short Commit(ConnMgr conn) {
    if (!IsValid(conn)) return -1;

    conn->transactionId = 0;
    WCommit(conn->theConn);
    return WGetErrorCode(conn->theConn) == 0 ? 0 : 1; 
}

short Rollback(ConnMgr conn) {
    if (!IsValid(conn)) return -1;

    conn->transactionId = 0;
    WRollback(conn->theConn);
    return WGetErrorCode(conn->theConn) == 0 ? 0 : 1; 
}

short UserLock(ConnMgr conn, StmtMgr mgr, const char* grouptolock, uint32_t val, char lock) {
    WUserLock(conn->theConn, grouptolock, val, lock);
    return CheckForErrors(conn, mgr);
}

long GetErrorCode(ConnMgr conn, StmtMgr mgr) {
    if (mgr->errorlevel == 2) return mgr->errordelegate.rc;
    return WGetErrorCode(conn->theConn);
}

const char* GetErrorText(ConnMgr conn, StmtMgr mgr) {
    if (mgr->errorlevel == 2) return mgr->errordelegate.text;
    return WGetErrorText(conn->theConn);
}

const char* GetErrorState(ConnMgr conn, StmtMgr mgr) {
    if (mgr->errorlevel == 2) return mgr->errordelegate.state;
    return WGetErrorState(conn->theConn);
}

long ReportError(ConnMgr conn, StmtMgr mgr, const char** text, const char** state) {
    if (mgr != NULL && mgr->errorlevel == 2) {
        *text = mgr->errordelegate.text;
        *state = mgr->errordelegate.text;
        return mgr->errordelegate.rc;
    }
    if (!IsValid(conn)) return -1;
    long code = WGetErrorCode(conn->theConn);
    if (code != 0) {
        *text = WGetErrorText(conn->theConn);
        *state = WGetErrorState(conn->theConn);
    }
    return code;
}

short ParseStatement(ConnMgr conn, StmtMgr mgr, const char* statement) {
    if (mgr->dataStack != NULL) {
        mgr->statement = WPrepareStatement(conn->theConn, statement);
        return CheckForErrors(conn, mgr);
    } else {
        return -1;
    }
}

Input AddBind(ConnMgr conn, StmtMgr mgr, Input bind) {
    int otype;
    Bound base = InputToBound(bind);

    base->indicator = 0;

    switch (base->type) {
        case INT4TYPE:
            base->maxlength = 4;
            base->byval = 1;
            break;
        case VARCHARTYPE:
            base->maxlength = (128 + 4);
            base->byval = 0;
            break;
        case CHARTYPE:
            base->maxlength = 1;
            base->byval = 1;
            break;
        case BOOLTYPE:
            base->maxlength = 1;
            base->byval = 1;
            break;
        case BYTEATYPE:
            base->maxlength = (128 + 4);
            base->byval = 0;
            break;
        case BLOBTYPE:
        case TEXTTYPE:
        case JAVATYPE:
            base->maxlength = (512);
            base->byval = 0;
            break;
        case TIMESTAMPTYPE:
            base->maxlength = 8;
            base->byval = 1;
            break;
        case DOUBLETYPE:
            base->maxlength = 8;
            base->byval = 1;
            break;
        case LONGTYPE:
            base->maxlength = 8;
            base->byval = 1;
            break;
        case FUNCTIONTYPE:
            break;
        case SLOTTYPE:
            base->maxlength = (512 + 4);
            base->byval = 0;
            break;
        case STREAMTYPE:
            base->maxlength = WPipeSize(conn->theConn);
            base->byval = 1;
            break;
        default:
            break;
    }
    /*  first check to see if the data will fit  */
    mgr->holdingArea = align(mgr, mgr->holdingArea);

    base->pointer = mgr->holdingArea;
    mgr->holdingArea += base->maxlength;

    if (mgr->holdingArea > MAX_STMTSIZE) {
        DelegateError(mgr, "PREPARE", "no statement binding space left", 803);
        return NULL;
    }

    while (GetStatementSpaceSize(mgr) < (mgr->holdingArea)) {
        long stmtsz = GetStatementSpaceSize(mgr);
        stmtsz = (stmtsz * 2 < MAX_STMTSIZE) ? stmtsz * 2 : MAX_STMTSIZE;
        SetStatementSpaceSize(conn, mgr, stmtsz);
    }

    if (base->type == STREAMTYPE) otype = BLOBTYPE;
    else otype = base->type;

    /*  if the dataStack has been moved, all the pointers need to be reset  */

    WBindLink(mgr->statement, bind->binder, Advance(mgr, base->pointer), base->maxlength, &base->indicator, otype, base->type);

    return bind;
}

Input GetBind(ConnMgr conn, StmtMgr mgr, const char * vari, short type) {
    Input bind = NULL;
    Bound base = NULL;
    if (!IsValid(conn)) return NULL;

    /*  remove the marker flag of the named parameter if there is one */
    switch (*vari) {
        case '$':
        case '?':
        case ':':
            vari++;
    }

    short x;
    for (x = 0; x < mgr->log_count; x++) {
        if (mgr->inputLog[x].binder[0] == '\0' || strcmp(vari, mgr->inputLog[x].binder) == 0) break;
    }

    if (x == mgr->log_count) {
        ExpandBindings(conn, mgr);
    }

    bind = &mgr->inputLog[x];
    base = InputToBound(bind);

    if (bind->binder[0] == '\0' || base->type != type) {
        strncpy(bind->binder, vari, 64);
        base->type = type;
        return AddBind(conn, mgr, bind);
    }

    return bind;
}

void* SetUserspace(Bound bound, void* target) {
    if (bound != NULL) {
        void* old = bound->userspace;
        bound->userspace = target;
        return old;
    }
    return NULL;
}

Input SetInputValue(ConnMgr conn, StmtMgr mgr, const char * vari, short type, void* data, int length) {
    Input bound = GetBind(conn, mgr, vari, type);

    if (bound != NULL) {
        Bound base = InputToBound(bound);
        if (data == NULL) {
            base->indicator = 0;
        } else {
            if (base->byval) {
                if (length < 0 || base->maxlength < length) {
                    length = base->maxlength;
                }
                memcpy(Advance(mgr, base->pointer), data, length);
                base->indicator = 1;
            } else {
                char* space = Advance(mgr, base->pointer);
                if (base->indicator == 2) {
                    WFreeMemory(conn->theConn, *(void**) (space + 4));
                    if (CheckForErrors(conn, mgr)) return NULL;
                }
                if (base->maxlength < length + 4) {
                    *(int32_t*) space = -1;
                    space += 4;
                    *(void**) space = WAllocStatementMemory(mgr->statement, length + 4);
                    if (CheckForErrors(conn, mgr)) return NULL;
                    space = *(void**) space;
                    base->indicator = 2;
                } else {
                    base->indicator = 1;
                }
                *(int32_t*) space = length + 4;
                space += 4;
                memcpy(space, data, length);
            }
        }
    }
    return bound;
}

Output SetOutputValue(ConnMgr conn, StmtMgr mgr, int index, short type, void* data, int length) {
    Output bound = OutputLink(conn, mgr, index, type);

    if (bound != NULL) {
        Bound base = OutputToBound(bound);
        if (data == NULL) {
            base->indicator = 0;
        } else {
            if (base->byval) {
                if (length < 0 || base->maxlength < length) {
                    length = base->maxlength;
                }
                memcpy(Advance(mgr, base->pointer), data, length);
            } else {
                char* space = Advance(mgr, base->pointer);
                if (base->maxlength < length + 4) {
                    length = base->maxlength - 4;
                }
                memcpy(space + 4, data, length);
                *(int32_t*) space = length + 4;
            }
            base->indicator = 1;
        }
    }
    return bound;
}

short GetOutputs(StmtMgr mgr, void* funcargs, outputfunc sendfunc) {
    short x;

    for (x = 0; x < mgr->log_count; x++) {
        Output output = &mgr->outputLog[x];
        void* value = NULL;

        if (output->index == 0) break;
        if (output->base.indicator == 1) value = Advance(mgr, output->base.pointer);
        else if (output->base.indicator == 2) value = *(void**) Advance(mgr, output->base.pointer);
        if (output->base.type == 0) value = NULL;
        sendfunc(mgr, output->base.type, value, output->clength, output->base.userspace, funcargs);
    }

    return 0;
}

short DisconnectPipes(ConnMgr conn, StmtMgr mgr) {
    short x;

    for (x = 0; x < mgr->log_count; x++) {
        Bound bound = InputToBound(&mgr->inputLog[x]);
        if (bound->type == STREAMTYPE) {
            PipeDisconnect(conn, mgr, SetUserspace(bound, NULL));
        }
        bound = OutputToBound(&mgr->outputLog[x]);
        if (bound->type == STREAMTYPE) {
            PipeDisconnect(conn, mgr, SetUserspace(bound, NULL));
        }
    }

    return 0;
}

Output OutputLink(ConnMgr conn, StmtMgr mgr, int index, short type) {
    return GetLink(conn, mgr, index, type);
}

Output GetLink(ConnMgr conn, StmtMgr mgr, int index, short type) {
    short x;
    Output link = NULL;
    Bound base = NULL;

    for (x = 0; x < mgr->log_count; x++) {
        if ((index == mgr->outputLog[x].index) || (mgr->outputLog[x].index == 0)) break;
    }

    if (x == mgr->log_count) {
        ExpandBindings(conn, mgr);
    }

    link = &mgr->outputLog[x];
    base = OutputToBound(link);

    if (link->index == 0 || base->type != type) {
        link->index = index;
        base->type = type;
        return AddLink(conn, mgr, link);
    }

    return link;
}

Output AddLink(ConnMgr conn, StmtMgr mgr, Output link) {
    Bound base = OutputToBound(link);

    switch (base->type) {
        case INT4TYPE:
            base->maxlength = 4;
            base->byval = 1;
            break;
        case VARCHARTYPE:
            base->maxlength = (128 + 4);
            base->byval = 0;
            break;
        case CHARTYPE:
            base->maxlength = 1;
            base->byval = 1;
            break;
        case BOOLTYPE:
            base->maxlength = 1;
            base->byval = 1;
            break;
        case BYTEATYPE:
            base->maxlength = (128 + 4);
            base->byval = 0;
            break;
        case JAVATYPE:
        case BLOBTYPE:
        case TEXTTYPE:
            base->maxlength = (512 + 4);
            base->byval = 0;
            break;
        case TIMESTAMPTYPE:
            base->maxlength = 8;
            base->byval = 1;
            break;
        case DOUBLETYPE:
            base->maxlength = 8;
            base->byval = 1;
            break;
        case LONGTYPE:
            base->maxlength = 8;
            base->byval = 1;
            break;
        case FUNCTIONTYPE:
            base->maxlength = 8;
            base->byval = 1;
            break;
        case STREAMTYPE:
            base->maxlength = WPipeSize(conn->theConn);
            base->byval = 1;
        default:
            break;
    }

    mgr->holdingArea = align(mgr, mgr->holdingArea);
    base->pointer = mgr->holdingArea;
    mgr->holdingArea += base->maxlength;

    if (mgr->holdingArea > MAX_STMTSIZE) {
        DelegateError(mgr, "PREPARE", "no statement binding space left", 803);
        return NULL;
    }

    while (GetStatementSpaceSize(mgr) < (mgr->holdingArea)) {
        long stmtsz = GetStatementSpaceSize(mgr);
        stmtsz = (stmtsz * 2 < MAX_STMTSIZE) ? stmtsz * 2 : MAX_STMTSIZE;
        SetStatementSpaceSize(conn, mgr, stmtsz);
    }

    WOutputLink(mgr->statement, link->index, Advance(mgr, base->pointer), base->maxlength, base->type, &base->indicator, &link->clength);

    return link;
}

void ClearData(StmtMgr mgr) {
    if (mgr->dataStack != NULL) memset(mgr->dataStack, '\0', mgr->stackSize);
    mgr->holdingArea = 0;
}

void* Advance(StmtMgr mgr, long size) {
    if (size >= mgr->stackSize) return NULL;
    return ((char*) mgr->dataStack)+size;
}

short SetStatementSpaceSize(ConnMgr conn, StmtMgr mgr, long size) {
    char* newbuf = WAllocConnectionMemory(conn->theConn, size);
    if (mgr->dataStack != NULL) {
        memmove(newbuf, mgr->dataStack, mgr->stackSize);
        WFreeMemory(conn->theConn, mgr->dataStack);
    }
    mgr->dataStack = newbuf;
    mgr->stackSize = size;

    ResetBindings(mgr);
    return CheckForErrors(conn, mgr);
}

short ExpandBindings(ConnMgr conn, StmtMgr mgr) {
    int count = mgr->log_count * 2;

    if (count <= 0) count = 2;

    inputDef* inputs = WAllocConnectionMemory(conn->theConn, sizeof (inputDef) * count);
    memset(inputs, 0x00, sizeof (inputDef) * count);
    outputDef* outputs = WAllocConnectionMemory(conn->theConn, sizeof (outputDef) * count);
    memset(outputs, 0x00, sizeof (outputDef) * count);
    if (mgr->inputLog != NULL) {
        memmove(inputs, mgr->inputLog, sizeof (inputDef) * mgr->log_count);
        WFreeMemory(conn->theConn, mgr->inputLog);
    }
    if (mgr->outputLog != NULL) {
        memmove(outputs, mgr->outputLog, sizeof (outputDef) * mgr->log_count);
        WFreeMemory(conn->theConn, mgr->outputLog);
    }
    mgr->inputLog = inputs;
    mgr->outputLog = outputs;
    mgr->log_count = count;

    ResetBindings(mgr);

    return 0;
}

short ResetBindings(StmtMgr mgr) {
    int count = 0;
    for (count = 0; count < mgr->log_count; count++) {
        Output link = &mgr->outputLog[count];
        Bound base = OutputToBound(link);
        if (link->index != 0) {
            WOutputLink(mgr->statement, link->index, Advance(mgr, base->pointer), base->maxlength, base->type, &base->indicator, &link->clength);
        }
    }
    for (count = 0; count < mgr->log_count; count++) {
        Input bind = &mgr->inputLog[count];
        Bound base = InputToBound(bind);
        if (bind->binder[0] != '\0') {
            int otype = (base->type == STREAMTYPE) ? BLOBTYPE : base->type;
            WBindLink(mgr->statement, bind->binder, Advance(mgr, base->pointer), base->maxlength, &base->indicator, otype, base->type);
        }
    }
    return 0;
}

short SetStatementBlobSize(ConnMgr conn, StmtMgr mgr, long size) {
    if (!IsValid(conn)) return -1;

    mgr->blob_size = size;
    if ((mgr->blob_size * 4) > mgr->stackSize) SetStatementSpaceSize(conn, mgr, mgr->blob_size * 4);
    return 0;
}

long GetStatementSpaceSize(StmtMgr mgr) {
    return mgr->stackSize;
}

long GetStatementBlobSize(StmtMgr mgr) {
    return mgr->blob_size;
}

short DelegateError(StmtMgr mgr, const char* state, const char* text, int code) {
    mgr->errorlevel = 2;
    mgr->errordelegate.rc = code;
    strncpy(mgr->errordelegate.text, text, 255);
    strncpy(mgr->errordelegate.state, text, 40);
    return 2;
}

short CheckForErrors(ConnMgr conn, StmtMgr mgr) {
    long cc = WGetErrorCode(conn->theConn);
    if (mgr->errorlevel == 2) {
        return 2;
    }
    if (cc != 0) {
        mgr->errorlevel = 1;
        return 1;
    }
    mgr->errorlevel = 0;

    return 0;
}

long align(StmtMgr mgr, long pointer) {
    pointer += (sizeof (long) - ((long) Advance(mgr, pointer) % sizeof (long)));
    return pointer;
}

short StreamExec(ConnMgr conn, char* statement) {
    if (!IsValid(conn)) return -1;

    WStreamExec(conn->theConn, statement);

    return WGetErrorCode(conn->theConn) == 0 ? 0 : 1; 
}

Pipe PipeConnect(ConnMgr conn, StmtMgr mgr, void* args, pipefunc func) {
    return WPipeConnect(conn->theConn, args, func);
}

void* PipeDisconnect(ConnMgr conn, StmtMgr mgr, Pipe comm) {
    return WPipeDisconnect(conn->theConn, comm);
}

void ConnectStdIO(ConnMgr conn, void *args, pipefunc in, pipefunc out) {
    if (!IsValid(conn)) return;
    WConnectStdIO(conn->theConn, args, in, out);
}

void DisconnectStdIO(ConnMgr conn) {
    if (!IsValid(conn)) return;
    WDisconnectStdIO(conn->theConn);
}

