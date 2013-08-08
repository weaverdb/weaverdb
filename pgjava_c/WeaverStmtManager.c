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
    int refCount;
    pthread_mutex_t control;
} WeaverConnectionManager;

typedef struct WeaverStmtManager {
    long transactionId;
    long commandId;
    void* dataStack;
    long stackSize;
    long blob_size;

    Error errordelegate;
    long errorlevel;

    WeaverConnectionManager* connection;
    OpaquePreparedStatement statement;

    long holdingArea;

    inputDef* inputLog;
    outputDef* outputLog;

    short log_count;

} WeaverStmtManager;

static Input GetBind(StmtMgr mgr, const char * vari, short type);
static Input AddBind(StmtMgr mgr, const char * vari, short type);
static void Clean(StmtMgr mgr, usercleanup input, usercleanup output);
static long align(StmtMgr mgr, long pointer);
static void* Advance(StmtMgr mgr, long size);
static short ExpandBindings(StmtMgr mgr);
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
            connection->refCount += 1;
            mgr->connection = connection;

            mgr->statement = NULL;

            mgr->holdingArea = 0;

            mgr->blob_size = BLOBSIZE;
            mgr->stackSize = mgr->blob_size * 2;
            mgr->dataStack = WAllocConnectionMemory(connection->theConn, mgr->stackSize);
            mgr->transactionId = 0;

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
    pthread_mutex_lock(&mgr->control);
    if (mgr->refCount == 0 && mgr->theConn != NULL) {
        /*  it's possible the the owning thread is not the
         *  destroying thread so to a cancel/join for
         *  safety's sake.
         */
        WCancelAndJoin(mgr->theConn);
        pthread_mutex_destroy(&mgr->control);
        WDestroyConnection(mgr->theConn);
        //  connection memory freed with destroy
        mgr->theConn = NULL;
    } else {
        pthread_mutex_unlock(&mgr->control);
    }
}

ConnMgr GetWeaverConnection(StmtMgr mgr) {
    if (mgr == NULL) return NULL;
    return mgr->connection;
}

ConnMgr DestroyWeaverStmtManager(StmtMgr mgr) {
    if (mgr == NULL) return NULL;

    ConnMgr conn = mgr->connection;

    pthread_mutex_lock(&conn->control);
    conn->refCount -= 1;

    if (mgr->dataStack != NULL) WFreeMemory(conn->theConn, mgr->dataStack);
    if (mgr->inputLog != NULL) WFreeMemory(conn->theConn, mgr->inputLog);
    if (mgr->outputLog != NULL) WFreeMemory(conn->theConn, mgr->outputLog);

    WFreeMemory(conn->theConn, mgr);
    pthread_mutex_unlock(&conn->control);

    return conn;
}

StmtMgr
CreateSubConnection(StmtMgr parent) {
    if (parent == NULL) return NULL;
    OpaqueWConn connection = WCreateSubConnection(parent->connection->theConn);
    ConnMgr mgr = (WeaverConnectionManager*) WAllocConnectionMemory(connection, sizeof (WeaverConnectionManager));
    pthread_mutex_init(&mgr->control, NULL);
    mgr->refCount = 0;
    mgr->theConn = connection;
    return CreateWeaverStmtManager(mgr);
}

short IsValid(ConnMgr mgr) {
    if (mgr == NULL) return 0;
    if (mgr->theConn == NULL) return (short) 0;
    return (short) (WIsValidConnection(mgr->theConn));
}

short IsStmtValid(StmtMgr mgr) {
    if (mgr == NULL) return 0;
    if (mgr->connection == NULL) return (short) 0;
    return (short) (WIsValidConnection(mgr->connection->theConn));
}

void Clean(StmtMgr mgr, usercleanup input, usercleanup output) {
    short x;
    ConnMgr conn = mgr->connection;
    if (!IsValid(conn)) return;
    ClearData(mgr);
    for (x = 0; x < mgr->log_count; x++) {
        mgr->inputLog[x].binder[0] = '\0';
        mgr->inputLog[x].base.pointer = 0;
        mgr->inputLog[x].base.maxlength = 0;
        mgr->inputLog[x].base.indicator = -1;
        mgr->inputLog[x].base.byval = 0;
        if (mgr->inputLog[x].base.userspace != NULL && input != NULL)
            input(mgr, mgr->inputLog[x].base.type, mgr->inputLog[x].base.userspace);
        mgr->inputLog[x].base.userspace = NULL;
        mgr->inputLog[x].base.type = 0;

        mgr->outputLog[x].index = 0;
        mgr->outputLog[x].base.pointer = 0;
        mgr->outputLog[x].base.maxlength = 0;
        mgr->outputLog[x].base.indicator = -1;
        mgr->outputLog[x].base.byval = 0;
        if (mgr->outputLog[x].base.userspace != NULL && output != NULL)
            output(mgr, mgr->outputLog[x].base.type, mgr->outputLog[x].base.userspace);
        mgr->outputLog[x].base.userspace = NULL;
        mgr->outputLog[x].base.type = 0;
    }
}

short Init(StmtMgr mgr, usercleanup input, usercleanup output) {
    short clean;

    ConnMgr conn = mgr->connection;
    if (!IsValid(conn)) return 0;

    if (mgr->statement != NULL) {
        Clean(mgr, input, output);
        clean = WDestroyPreparedStatement(mgr->statement);
        mgr->statement = NULL;
        memset(&mgr->errordelegate, 0, sizeof (Error));
        mgr->errorlevel = 0;
    }

    return clean;
}

short Begin(StmtMgr mgr) {
    ConnMgr conn = mgr->connection;
    if (!IsValid(conn)) return -1;

    if (WBegin(mgr->connection->theConn, 0) == 0) {
        mgr->transactionId = WGetTransactionId(conn->theConn);
    }

    return CheckForErrors(mgr);
}

short Fetch(StmtMgr mgr) {
    ConnMgr conn = mgr->connection;
    if (!IsValid(conn)) return -1;

    long val = WFetch(mgr->statement);
    if (val == 4) return 1;
    return CheckForErrors(mgr);
}

long Count(StmtMgr mgr) {
    ConnMgr conn = mgr->connection;
    if (!IsValid(conn)) return -1;
    return WExecCount(mgr->statement);
}

short Cancel(StmtMgr mgr) {
    ConnMgr conn = mgr->connection;
    if (!IsValid(conn)) return -1;

    WCancel(conn->theConn);
    return CheckForErrors(mgr);
}

short Prepare(StmtMgr mgr) {
    ConnMgr conn = mgr->connection;
    if (!IsValid(conn)) return -1;

    WPrepare(conn->theConn);
    return CheckForErrors(mgr);
}

short BeginProcedure(StmtMgr mgr) {
    ConnMgr conn = mgr->connection;
    if (!IsValid(conn)) return -1;

    WBeginProcedure(conn->theConn);
    return CheckForErrors(mgr);
}

short EndProcedure(StmtMgr mgr) {
    ConnMgr conn = mgr->connection;
    if (!IsValid(conn)) return -1;

    WEndProcedure(conn->theConn);
    return CheckForErrors(mgr);
}

short Exec(StmtMgr mgr) {
    short err = 0;

    ConnMgr conn = mgr->connection;
    if (!IsValid(conn)) return -1;

    WExec(mgr->statement);
    err = CheckForErrors(mgr);
    if (!err) {
        mgr->commandId = WGetCommandId(conn->theConn);
    }

    return err;
}

long GetTransactionId(StmtMgr mgr) {

    ConnMgr conn = mgr->connection;

    if (!IsValid(conn)) return -1;
    return mgr->transactionId;
}

long GetCommandId(StmtMgr mgr) {
    ConnMgr conn = mgr->connection;
    if (!IsValid(conn)) return -1;
    return mgr->commandId;
}

short Commit(StmtMgr mgr) {
    ConnMgr conn = mgr->connection;
    if (!IsValid(conn)) return -1;

    mgr->transactionId = 0;
    WCommit(conn->theConn);
    return CheckForErrors(mgr);
}

short Rollback(StmtMgr mgr) {
    ConnMgr conn = mgr->connection;
    if (!IsValid(conn)) return -1;

    mgr->transactionId = 0;
    WRollback(conn->theConn);
    return CheckForErrors(mgr);
}

short UserLock(StmtMgr mgr, const char* grouptolock, uint32_t val, char lock) {
    ConnMgr conn = mgr->connection;
    if (!IsValid(conn)) return -1;

    WUserLock(conn->theConn, grouptolock, val, lock);
    return CheckForErrors(mgr);
}

long GetErrorCode(StmtMgr mgr) {
    ConnMgr conn = mgr->connection;
    if (!IsValid(conn)) return -1;

    if (mgr->errorlevel == 2) return mgr->errordelegate.rc;
    return WGetErrorCode(conn->theConn);
}

const char* GetErrorText(StmtMgr mgr) {
    ConnMgr conn = mgr->connection;
    if (!IsValid(conn)) return "connection not valid";

    if (mgr->errorlevel == 2) return mgr->errordelegate.text;
    return WGetErrorText(conn->theConn);
}

const char* GetErrorState(StmtMgr mgr) {
    ConnMgr conn = mgr->connection;
    if (!IsValid(conn)) return "INVALID";
    if (mgr->errorlevel == 2) return mgr->errordelegate.state;
    return WGetErrorState(conn->theConn);
}

short ParseStatement(StmtMgr mgr, const char* statement) {
    ConnMgr conn = mgr->connection;
    if (!IsValid(conn)) return -1;

    if (mgr->dataStack != NULL) {
        mgr->statement = WPrepareStatement(conn->theConn, statement);
        return CheckForErrors(mgr);
    } else {
        return -1;
    }
}

Input AddBind(StmtMgr mgr, const char * vari, short type) {
    int otype;
    Input bind = NULL;
    Bound base = NULL;

    ConnMgr conn = mgr->connection;
    if (!IsValid(conn)) return NULL;

    short x;

    /*  remove the marker flag of the named parameter if there is one */
    switch (*vari) {
        case '$':
        case '?':
        case ':':
            vari++;
    }

    for (x = 0; x < mgr->log_count; x++) {
        if (mgr->inputLog[x].binder[0] == '\0' || strcmp(mgr->inputLog[x].binder, vari) == 0) break;
    }
    if (x == mgr->log_count) {
        ExpandBindings(mgr);
    }

    bind = &mgr->inputLog[x];
    base = InputToBound(bind);

    base->indicator = 0;
    if (base->type == type) return bind;

    switch (type) {
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
        long stmtsz = (stmtsz * 2 < MAX_STMTSIZE) ? stmtsz * 2 : MAX_STMTSIZE;
        SetStatementSpaceSize(mgr, stmtsz);
    }

    strncpy(bind->binder, vari, 64);
    base->type = type;

    if (type == STREAMTYPE) otype = BLOBTYPE;
    else otype = type;

    /*  if the dataStack has been moved, all the pointers need to be reset  */

    WBindLink(mgr->statement, bind->binder, Advance(mgr, base->pointer), base->maxlength, &base->indicator, otype, type);

    return bind;
}

Input GetBind(StmtMgr mgr, const char * vari, short type) {
    Input bind = NULL;
    ConnMgr conn = mgr->connection;
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
    if (x == mgr->log_count || mgr->inputLog[x].binder[0] == '\0') {
        return AddBind(mgr, vari, type);
    } else {
        bind = &mgr->inputLog[x];
    }

    return bind;
}

void* SetUserspace(StmtMgr mgr, Bound bound, void* target) {
    if (bound != NULL) {
        void* old = bound->userspace;
        bound->userspace = target;
        return old;
    }
    return NULL;
}

Input SetInputValue(StmtMgr mgr, const char * vari, short type, void* data, int length) {
    ConnMgr conn = mgr->connection;
    if (!IsValid(conn)) return NULL;

    Input bound = GetBind(mgr, vari, type);

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
                    if (CheckForErrors(mgr)) return NULL;
                }
                if (base->maxlength < length + 4) {
                    *(int32_t*) space = -1;
                    space += 4;
                    *(void**) space = WAllocStatementMemory(mgr->statement, length + 4);
                    if (CheckForErrors(mgr)) return NULL;
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

Output SetOutputValue(StmtMgr mgr, int index, short type, void* data, int length) {
    Output bound = OutputLink(mgr, index, type);

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

    ConnMgr conn = mgr->connection;
    if (!IsValid(conn)) return -1;
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

Output OutputLink(StmtMgr mgr, int index, short type) {
    Output link = NULL;
    Bound base = NULL;

    ConnMgr conn = mgr->connection;

    if (!IsValid(conn)) {
        return NULL;
    } else {
        short x;

        for (x = 0; x < mgr->log_count; x++) {
            if ((index == mgr->outputLog[x].index) || (mgr->outputLog[x].index == 0)) break;
        }

        if (x == mgr->log_count) {
            ExpandBindings(mgr);
        }

        link = &mgr->outputLog[x];
        base = &link->base;
    }

    base->indicator = 0;
    if (base->type == type) return link;

    switch (type) {
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
        long stmtsz = (stmtsz * 2 < MAX_STMTSIZE) ? stmtsz * 2 : MAX_STMTSIZE;
        SetStatementSpaceSize(mgr, stmtsz);
    }

    link->index = index;
    base->type = type;

    WOutputLink(mgr->statement, link->index, Advance(mgr, base->pointer), base->maxlength, type, &base->indicator, &link->clength);

    return link;
}

void ClearData(StmtMgr mgr) {
    ConnMgr conn = mgr->connection;
    if (!IsValid(conn)) return;
    if (mgr->dataStack != NULL) memset(mgr->dataStack, '\0', mgr->stackSize);
    mgr->holdingArea = 0;
}

void* Advance(StmtMgr mgr, long size) {
    ConnMgr conn = mgr->connection;
    if (!IsValid(conn)) return NULL;
    if (size >= mgr->stackSize) return NULL;
    return ((char*) mgr->dataStack)+size;
}

short SetStatementSpaceSize(StmtMgr mgr, long size) {
    ConnMgr conn = mgr->connection;
    if (!IsValid(conn)) return -1;

    char* newbuf = WAllocConnectionMemory(conn->theConn, size);
    if (mgr->dataStack != NULL) {
        memmove(newbuf, mgr->dataStack, mgr->stackSize);
        WFreeMemory(conn->theConn, mgr->dataStack);
    }
    mgr->dataStack = newbuf;
    mgr->stackSize = size;

    ResetBindings(mgr);
    return CheckForErrors(mgr);
}

short ExpandBindings(StmtMgr mgr) {
    ConnMgr conn = mgr->connection;
    if (!IsValid(conn)) return -1;

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

    return CheckForErrors(mgr);
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

short SetStatementBlobSize(StmtMgr mgr, long size) {
    ConnMgr conn = mgr->connection;
    if (!IsValid(conn)) return -1;

    mgr->blob_size = size;
    if ((mgr->blob_size * 4) > mgr->stackSize) SetStatementSpaceSize(mgr, mgr->blob_size * 4);
    return 0;
}

long GetStatementSpaceSize(StmtMgr mgr) {
    ConnMgr conn = mgr->connection;
    if (!IsValid(conn)) return -1;

    return mgr->stackSize;
}

long GetStatementBlobSize(StmtMgr mgr) {
    ConnMgr conn = mgr->connection;
    if (!IsValid(conn)) return -1;

    return mgr->blob_size;
}

short DelegateError(StmtMgr mgr, const char* state, const char* text, int code) {
    ConnMgr conn = mgr->connection;
    if (!IsValid(conn)) return -1;

    mgr->errorlevel = 2;
    mgr->errordelegate.rc = code;
    strncpy(mgr->errordelegate.text, text, 255);
    strncpy(mgr->errordelegate.state, text, 40);
    return 2;
}

short CheckForErrors(StmtMgr mgr) {
    ConnMgr conn = mgr->connection;
    if (!IsValid(conn)) return -1;

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

short StreamExec(StmtMgr mgr, char* statement) {
    ConnMgr conn = mgr->connection;
    if (!IsValid(conn)) return -1;

    WStreamExec(conn->theConn, statement);

    return CheckForErrors(mgr);
}

Pipe PipeConnect(StmtMgr mgr, void* args, pipefunc func) {
    ConnMgr conn = mgr->connection;
    if (!IsValid(conn)) return NULL;
    return WPipeConnect(conn->theConn, args, func);

}

void* PipeDisconnect(StmtMgr mgr, Pipe comm) {
    ConnMgr conn = mgr->connection;
    if (!IsValid(conn)) return NULL;
    return WPipeDisconnect(conn->theConn, comm);
}

void ConnectStdIO(StmtMgr mgr, void *args, pipefunc in, pipefunc out) {
    ConnMgr conn = mgr->connection;
    if (!IsValid(conn)) return;
    WConnectStdIO(conn->theConn, args, in, out);
}

void DisconnectStdIO(StmtMgr mgr) {
    ConnMgr conn = mgr->connection;
    if (!IsValid(conn)) return;
    WDisconnectStdIO(conn->theConn);
}

