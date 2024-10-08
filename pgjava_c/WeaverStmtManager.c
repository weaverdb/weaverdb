/*-------------------------------------------------------------------------
 *
 *	WeaverStmtManager.c
 *		Statement interface and packaging for java to C
 *
 * Copyright (c) 2000-2024, Myron Scott  <myron@weaverdb.org>
 *
 *
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

typedef struct indirect {
    void* userspace;
    transferfunc transfer;
} IndirectCaller;

struct bound {
    short type;
    IndirectCaller indirect;
};

typedef struct outputObj {
    struct bound base;
    long index;
} outputDef;

typedef struct bindObj {
    struct bound base;
    char binder[64];
} inputDef;

typedef struct WeaverConnectionManager {
    OpaqueWConn theConn;
    long transactionId;
    int refCount;
    StmtMgr statements[MAX_STATEMENTS];
    pthread_mutex_t control;
} WeaverConnectionManager;

typedef struct WeaverStmtManager {
    long commandId;

    Error errordelegate;
    long errorlevel;

    OpaquePreparedStatement statement;

    inputDef* inputLog;
    outputDef* outputLog;

    short input_slots;
    short output_slots;
} WeaverStmtManager;

static Output GetLink(ConnMgr, StmtMgr mgr, int index, short type);
static Output AddLink(ConnMgr, StmtMgr mgr, Output bind);
static Input GetBind(ConnMgr, StmtMgr mgr, const char * vari, short type);
static Input AddBind(ConnMgr, StmtMgr mgr, Input bind);
static short ExpandInputBindings(ConnMgr, StmtMgr mgr);
static short ResetInputBindings(StmtMgr mgr);
static short ExpandOutputBindings(ConnMgr, StmtMgr mgr);
static short ResetOutputBindings(StmtMgr mgr);

static int IndirectToDirect(void* userenv, int varType, void *varAdd, int varSize);

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
        mgr->transactionId = 0L;
        return mgr;
    }
}

StmtMgr
CreateWeaverStmtManager(ConnMgr connection) {
    StmtMgr mgr = NULL;
    int counter = 0;

    pthread_mutex_lock(&connection->control);
    if (WIsValidConnection(connection->theConn)) {
        if (connection->refCount < MAX_STATEMENTS) {
            mgr = (StmtMgr) WAllocConnectionMemory(connection->theConn, sizeof (WeaverStmtManager));
            if (mgr != NULL) {
                connection->statements[connection->refCount++] = mgr;

                mgr->statement = NULL;

                mgr->errorlevel = 0;
                mgr->commandId = 0;

                memset(&mgr->errordelegate, 0x00, sizeof (Error));

                mgr->input_slots = 0;
                mgr->output_slots = 0;

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
        WCancelAndJoin(connection);
        pthread_mutex_destroy(&mgr->control);
        
        WDestroyConnection(connection);
        mgr->theConn = NULL;
        memset(mgr, 0x00, sizeof(WeaverConnectionManager));
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

    if (mgr->inputLog != NULL) WFreeMemory(conn->theConn, mgr->inputLog);
    if (mgr->outputLog != NULL) WFreeMemory(conn->theConn, mgr->outputLog);

    if (mgr->statement != NULL) WDestroyPreparedStatement(mgr->statement);

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

    if (WPrepare(conn->theConn)) {
        return 1;
    }

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
    if (!IsValid(conn)) return -1L;
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

long ReportError(ConnMgr conn, StmtMgr mgr, const char** text, const char** state) {
    if (mgr != NULL && mgr->errorlevel == 2) {
        *text = mgr->errordelegate.text;
        *state = mgr->errordelegate.text;
        return mgr->errordelegate.rc;
    }
    if (!IsValid(conn)) {
        *text = "connection is not valid";
        *state = "INVALID";
        return -1;
    }
    long code = WGetErrorCode(conn->theConn);
    if (code != 0) {
        *text = WGetErrorText(conn->theConn);
        *state = WGetErrorState(conn->theConn);
    }
    return code;
}

short ParseStatement(ConnMgr conn, StmtMgr mgr, const char* statement) {
    mgr->statement = WPrepareStatement(conn->theConn, statement);
    return CheckForErrors(conn, mgr);
}

Input AddBind(ConnMgr conn, StmtMgr mgr, Input bind) {

    Bound base = InputToBound(bind);

    WBindTransfer(mgr->statement, bind->binder, base->type, &base->indirect, IndirectToDirect); 
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
    for (x = 0; x < mgr->input_slots; x++) {
        if (mgr->inputLog[x].binder[0] == '\0' || strcmp(vari, mgr->inputLog[x].binder) == 0) break;
    }

    if (x == mgr->input_slots) {
        mgr->input_slots = ExpandInputBindings(conn, mgr);
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

Input LinkInput(ConnMgr conn, StmtMgr mgr, const char* name, short type, void* data, transferfunc func) {
    Input in = GetBind(conn, mgr, name, type);
    Bound b = InputToBound(in);
    b->indirect.userspace = data;
    b->indirect.transfer = func;
    return in;
}

Output LinkOutput(ConnMgr conn, StmtMgr mgr, int index, short type, void* data, transferfunc func) {
    Output out = GetLink(conn, mgr, index, type);
    Bound b = OutputToBound(out);
    b->indirect.userspace = data;
    b->indirect.transfer = func;
    return out;
}

Output GetLink(ConnMgr conn, StmtMgr mgr, int index, short type) {
    short x;
    Output link = NULL;
    Bound base = NULL;

    for (x = 0; x < mgr->output_slots; x++) {
        if ((index == mgr->outputLog[x].index) || (mgr->outputLog[x].index == 0)) break;
    }

    if (x == mgr->output_slots) {
        ExpandOutputBindings(conn, mgr);
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

    WOutputTransfer(mgr->statement, link->index, base->type, &base->indirect, IndirectToDirect);
    return link;
}

short ExpandInputBindings(ConnMgr conn, StmtMgr mgr) {
    int count = mgr->input_slots * 2;

    if (count <= 0) count = 4;

    inputDef* inputs = mgr->statement != NULL ? WAllocStatementMemory(mgr->statement, sizeof (inputDef) * count)
        : WAllocConnectionMemory(conn->theConn, sizeof (inputDef) * count);
    memset(inputs, 0, sizeof (inputDef) * count);

    if (mgr->inputLog != NULL) {
        memmove(inputs, mgr->inputLog, sizeof (inputDef) * mgr->input_slots);
        WFreeMemory(conn->theConn, mgr->inputLog);
    }

    mgr->inputLog = inputs;
    mgr->input_slots = count;

    ResetInputBindings(mgr);

    return count;
}

short ExpandOutputBindings(ConnMgr conn, StmtMgr mgr) {
    int count = mgr->output_slots * 2;

    if (count <= 0) count = 4;

    outputDef* outputs = mgr->statement != NULL ? WAllocStatementMemory(mgr->statement, sizeof (outputDef) * count)
        : WAllocConnectionMemory(conn->theConn, sizeof (outputDef) * count);
    memset(outputs, 0, sizeof (outputDef) * count);

    if (mgr->outputLog != NULL) {
        memmove(outputs, mgr->outputLog, sizeof (outputDef) * mgr->output_slots);
        WFreeMemory(conn->theConn, mgr->outputLog);
    }

    mgr->outputLog = outputs;
    mgr->output_slots = count;

    ResetOutputBindings(mgr);

    return 0;
}

short ResetInputBindings(StmtMgr mgr) {
    int count = 0;
    for (count = 0; count < mgr->input_slots; count++) {
        Input bind = &mgr->inputLog[count];
        Bound base = InputToBound(bind);
        if (bind->binder[0] != '\0') {
            WBindTransfer(mgr->statement, bind->binder, base->type, &base->indirect, IndirectToDirect);
        }
    }
    return 0;
}

short ResetOutputBindings(StmtMgr mgr) {
    int count = 0;
    for (count = 0; count < mgr->output_slots; count++) {
        Output link = &mgr->outputLog[count];
        Bound base = OutputToBound(link);
        if (link->index != 0) {
            WOutputTransfer(mgr->statement, link->index, base->type, &base->indirect, IndirectToDirect);
        }
    }
    return 0;
}

int IndirectToDirect(void* user, int type, void* data, int size) {
    IndirectCaller* callDef = user;
    return callDef->transfer(callDef->userspace, type, data, size);
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

short StreamExec(ConnMgr conn, const char* statement) {
    if (!IsValid(conn)) return -1;

    WStreamExec(conn->theConn, statement);

    return WGetErrorCode(conn->theConn) == 0 ? 0 : 1; 
}

void ConnectStdIO(ConnMgr conn, void *args, transferfunc in, transferfunc out) {
    if (!IsValid(conn)) return;
    WConnectStdIO(conn->theConn, args, in, out);
}

void DisconnectStdIO(ConnMgr conn) {
    if (!IsValid(conn)) return;
    WDisconnectStdIO(conn->theConn);
}
