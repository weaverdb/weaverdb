/*-------------------------------------------------------------------------
 *
 *      WeaverConnection.c
 *               Lowest level of Postgres interface
 *
 *
 *
 * IDENTIFICATION
 *              Myron Scott, mkscott@sacadia.com, 2.05F.2001
 *
 *-------------------------------------------------------------------------
 */

#include <stdlib.h>
#include <unistd.h>

#include <sys/socket.h>
#include <sys/types.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <assert.h>
#include <setjmp.h>
#include <pthread.h>

#define _INTERNAL_WEAVERCONNECTION_BUILD_

#include "env/connectionutil.h"
#include "env/WeaverConnection.h"

#include "access/heapam.h"
#include "access/blobstorage.h"
#include "utils/relcache.h"
#include "libpq/libpq.h"
#include "catalog/pg_shadow.h"
#include "lib/stringinfo.h"
#include "tcop/tcopprot.h"
#include "utils/syscache.h"
#include "catalog/pg_type.h"
#include "executor/executor.h"
#include "tcop/pquery.h"
#include "tcop/utility.h"
#include "nodes/execnodes.h"
#include "env/dbwriter.h"
#include "env/dolhelper.h"

#include "storage/sinvaladt.h"
#include "storage/multithread.h"
#include "storage/sinval.h"
#include "storage/smgr.h"
#include "storage/lock.h"
#include "miscadmin.h"
#include "access/printtup.h"
#include "access/htup.h"
#include "parser/gramparse.h"
#include "executor/executor.h"
#include "catalog/heap.h"
#include "utils/temprel.h"
#include "parser/parse_coerce.h"
#include "parser/parserinfo.h"

typedef enum {
    TRANSACTION_MEMORY,
    STATEMENT_MEMORY,
    CONNECTION_MEMORY
} mem_type;

/* buffer flusher for external world */

static long WDisposeConnection(OpaqueWConn conn);

static int FillExecArgs(PreparedPlan* plan);
static PreparedPlan *ParsePlan(PreparedPlan * plan);
static void SetError(WConn connection, int sqlError, char* state, char* err);
static void* WAllocMemory(WConn connection, mem_type type, size_t size);

static SectionId   connection_section_id = SECTIONID("CONN");

/*
static WConn SetupConnection(OpaqueWConn conn);
static long ReleaseConnection(WConn comm);
static bool ReadyConnection(WConn connection);
 */

#define SETUP(target) (WConn)target

#define READY(target, err)  \
    SetEnv(target->env);\
    \
    err = setjmp(target->env->errorContext);\
    if (err != 0) {\
        if (GetTransactionInfo()->SharedBufferChanged) {\
            strncpy(connection->env->state, "ABORTONLY", 39);\
            target->stage = TRAN_ABORTONLY;\
        }\
        SetAbortOnly();\
        WHandleError(target,err);\
    } else {\
        target->CDA.rc = 0\

#define RELEASE(target) \
    } \
    SetEnv(NULL);  \


extern OpaqueWConn
WCreateConnection(const char *tName, const char *pass, const char *conn) {
    int sqlError = 0;
    char dbpath[512];
    Oid dbid = InvalidOid;
    WConn connection = NULL;
    Env*     env;

    if (!isinitialized()) return NULL;

    env = CreateEnv(NULL);
    if ( env == NULL ) {
        return NULL;
    } else {
        SetEnv(env);
        MemoryContextInit();
    }
    
    connection = AllocateEnvSpace(connection_section_id,sizeof (struct Connection));
    memset(connection, 0x00, sizeof (struct Connection));

    connection->validFlag = -1;
    strncpy(connection->password, pass, 64);
    strncpy(connection->name, tName, 64);
    strncpy(connection->connect, conn, 64);

    connection->env = env;

    connection->env->Mode = InitProcessing;

    SetDatabaseName(conn);
    GetRawDatabaseInfo(conn, &dbid, dbpath);

    if (dbid == InvalidOid) {
        sqlError = 99;
        strncpy(connection->env->errortext, "unsuccessful connection -- too many connections", 255);
        strncpy(connection->env->state, "DISCONNECTED", 39);
        /*  destroy env takes care of the memory cxt  */
        SetEnv(NULL);
        DestroyEnv(env);

        return NULL;
    } else {
        connection->env->DatabaseId = dbid;
    }

    /* from Init Relations cache from RelationInitialize();   */
    InitThread(NORMAL_THREAD);
    RelationInitialize();
    InitCatalogCache();

    CallableInitInvalidationState();

    connection->env->Mode = NormalProcessing;

    /* this code checks to see if the user is valid  */
    if (dbid != InvalidOid) {
        short winner = false;
        HeapTuple ht = NULL;
        char isNull = true;

        if (strlen(tName) > 0) {
            ht = SearchSysCacheTuple(SHADOWNAME, PointerGetDatum(tName), 0, 0, 0);
            if (HeapTupleIsValid(ht)) {
                Datum dpass = SysCacheGetAttr(SHADOWNAME, ht, Anum_pg_shadow_passwd, &isNull);
                if (!isNull) {
                    char cpass[256];
                    memset(cpass, 0, 256);
                    strncpy(cpass, (char *) DatumGetPointer(dpass + 4), (*(int *) dpass) - 4);
                    winner = (strcmp(pass, cpass) == 0);
                    if (!winner) {
                        strncpy(connection->env->errortext, "user password does not match", 255);
                        sqlError = 1702;
                    }
                } else {
                    winner = true;
                }
            } else {
                sqlError = 1703;
                strncpy(connection->env->errortext, "user does not exist", 255);
                winner = false;
            }
        } else {
            if (GetBoolProperty("allow_anonymous")) {
                winner = true;
            } else {
                strncpy(connection->env->errortext, "anonymous connections not allowed", 255);
                sqlError = 1704;
            }
        }
    }

    connection->stage = TRAN_INVALID;

    if (sqlError == 0) {
        connection->validFlag = 1;
        strncpy(connection->env->errortext, "successful connection", 255);
        strncpy(connection->env->state, "CONNECTED", 39);

        SetPgUserName(connection->name);
        SetUserId();
        pthread_mutex_init(&connection->child_lock, NULL);
        connection->parent = NULL;
    } else {
        connection->validFlag = 0;
        strncpy(connection->env->state, "DISCONNECTED", 39);
        strncpy(connection->env->errortext, "connection failed", 255);
        WHandleError((OpaqueWConn) connection, sqlError);
    }

    SetEnv(NULL);

    return (OpaqueWConn) connection;
}

OpaqueWConn
WCreateSubConnection(OpaqueWConn parent) {
    return NULL;
}

extern long
WDestroyConnection(OpaqueWConn conn) {
    if ( conn == NULL ) return 0;
    
    WConn parent = conn->parent;

    if (parent) {
        pthread_mutex_lock(&parent->child_lock);
        parent->child_count--;
        pthread_mutex_unlock(&parent->child_lock);
    } else {
        pthread_mutex_lock(&conn->child_lock);
        while (conn->child_count > 0) {
            pthread_t waitfor = 0;
            void* result;
            pthread_mutex_unlock(&conn->child_lock);
            waitfor = FindChildThread(conn->env);
            if (waitfor != 0) pthread_join(waitfor, &result);
            pthread_mutex_lock(&conn->child_lock);
        }
        pthread_mutex_unlock(&conn->child_lock);
    }
    while ( conn->plan ) {
        WDestroyPreparedStatement(conn->plan);
    }
    if (conn->validFlag >= 0) {
        WDisposeConnection(conn);
    }
    if (conn->env != NULL) {
        DestroyEnv(conn->env);
    }
}

extern long
WBegin(OpaqueWConn conn, long trans) {
    long err = 0;
    WConn connection = SETUP(conn);

    clearerror(connection->env);

    if (connection->transaction_owner != 0) {
        err = 454;
        SetError(connection, err, "CONTEXT", "transaction owner already set");
        return err;
    }

    READY(connection, err);

    if (connection->stage != TRAN_INVALID) {
        elog(ERROR, "already in transaction %d", connection->stage);
    }

    connection->stage = TRAN_INVALID;
    /*  only do this if we are a top level connection  */
    if (connection->parent == NULL) {
        WResetQuery(connection);
        StartTransaction();
        SetQuerySnapshot();
    } else {
        if (connection->parent->stage == TRAN_INVALID) {
            elog(ERROR, "parent transaction is not in a transaction");
        } else {
            pthread_mutex_lock(&connection->parent->child_lock);
            connection->parent->child_trans++;
            pthread_mutex_unlock(&connection->parent->child_lock);
            CloneParentTransaction();
        }
    }

    connection->transaction_owner = pthread_self();
    connection->stage = STMT_NEW;

    RELEASE(connection);
    return err;
}

extern char*
WStatement(OpaquePreparedStatement plan) {
    return plan->statement;
}

extern OpaquePreparedStatement
WPrepareStatement(OpaqueWConn conn, const char *smt) {
    WConn connection = SETUP(conn);
    long err = 0;
    PreparedPlan* plan;
    MemoryContext old;

    if (!pthread_equal(connection->transaction_owner, pthread_self())) {
        char msg[256];
        err = 454;
        snprintf(msg, 255, "transaction is owned by thread %d, cannot make call from this context", connection->transaction_owner);
        SetError(connection, err, "CONTEXT", msg);
        return NULL;
    }
    /* If begin was not called, call it  */
    if (connection->stage == TRAN_INVALID) {
        err = 455;
        SetError(connection, err, "CONTEXT", "context not valid, check call sequence");
        return NULL;
    }

    READY(connection, err);

    if (CheckForCancel()) {
        elog(ERROR, "Query Cancelled");
    }

    old = MemoryContextSwitchTo(GetEnvMemoryContext());
    plan = (PreparedPlan *) palloc(sizeof (PreparedPlan));
    plan->statement = pstrdup(smt);
plan->created_cxt = AllocSetContextCreate(MemoryContextGetCurrentContext(),
            "PreparedPlanContext",
            ALLOCSET_DEFAULT_MINSIZE,
            ALLOCSET_DEFAULT_INITSIZE,
            ALLOCSET_DEFAULT_MAXSIZE);
    MemoryContextSwitchTo(plan->created_cxt);
    plan->owner = connection;
    memset(plan->input, 0, sizeof (Binder) * MAX_ARGS);
    memset(plan->output, 0, sizeof (Output) * MAX_ARGS);
    plan->input_count = 0;

    plan->tupdesc = NULL;
    plan->state = NULL;
    plan->qdesc = NULL;
    
    plan->querytreelist = NULL;
    plan->plantreelist = NULL;
    
    plan->exec_cxt = NULL;
    plan->stage = STMT_NEW;
    
    plan->next = connection->plan;
    connection->plan = plan;

    RELEASE(connection);

    return plan;
}

extern long 
WDestroyPreparedStatement(OpaquePreparedStatement stmt) {
    WConn connection = SETUP(stmt->owner);
    long err;
    READY(connection,err);
    if ( stmt == connection->plan ) {
        connection->plan = stmt->next;
    } else {
        PreparedPlan* start = connection->plan;
        while ( start->next != stmt ) {
            start = start->next;
        }
        start->next = stmt->next;
    }
    
    if (stmt->qdesc != NULL) {
        ExecutorEnd(stmt->qdesc, stmt->state);
    }
    MemoryContextDelete(stmt->created_cxt);
    if ( stmt->exec_cxt ) MemoryContextDelete(stmt->exec_cxt);
    pfree(stmt->statement);
    pfree(stmt);
    RELEASE(connection);
    return err;
}

extern long
WOutputLink(OpaquePreparedStatement plan, short pos, void *varAdd, int varSize, int varType, short *ind, int *clength) {
    WConn connection = SETUP(plan->owner);
    long err;

    if (!pthread_equal(connection->transaction_owner, pthread_self())) {
        char msg[256];
        err = 454;
        snprintf(msg, 255, "transaction is owned by thread %d, cannot make call from this context", connection->transaction_owner);
        SetError(connection, err, "CONTEXT", msg);
        return err;
    }

    READY(connection, err);

    if (CheckForCancel()) {
        elog(ERROR, "Query Cancelled");
    }

    if (pos > MAX_ARGS || pos <= 0) {
        coded_elog(ERROR, 101, "bad value - index must be greater than 0 and less than %d", MAX_ARGS);
    } else {
        plan->output[pos - 1].index = pos;
        plan->output[pos - 1].target = varAdd;
        plan->output[pos - 1].size = varSize;
        plan->output[pos - 1].type = varType;
        plan->output[pos - 1].notnull = ind;
        plan->output[pos - 1].freeable = NULL;
        plan->output[pos - 1].length = clength;
    }

    RELEASE(connection);

    return err;
}

extern long
WExec(OpaquePreparedStatement plan) {
    WConn connection = SETUP(plan->owner);
    long err = 0;

    List *trackquery = NULL;
    List *trackplan = NULL;
    Plan *plantree = NULL;
    Query *querytree = NULL;

    if (!pthread_equal(connection->transaction_owner, pthread_self())) {
        char msg[256];
        err = 454;
        snprintf(msg, 255, "transaction is owned by thread %d, cannot make call from this context", connection->transaction_owner);
        SetError(connection, err, "CONTEXT", msg);
        return err;
    }

    if (CheckForCancel()) {
        elog(ERROR, "Query Cancelled");
    }

    READY(connection, err);

    plan = ParsePlan(plan);

    WResetExecutor(plan);
    plan->processed = 0;
    
    trackquery = plan->querytreelist;
    trackplan = plan->plantreelist;

    while (trackquery) {
        /*
         * Increment Command Counter so we see everything
         * that happened in this transaction to here
         */
        CommandCounterIncrement();

        querytree = (Query *) lfirst(trackquery);
        trackquery = lnext(trackquery);

        plantree = (Plan *) lfirst(trackplan);
        trackplan = lnext(trackplan);

        SetQuerySnapshot();

        if (querytree->commandType == CMD_UTILITY) {
            ProcessUtility(querytree->utilityStmt, None);
            /*
             * increment after any utility if there are
             * more subqueries to execute
             */
        } else {
            plan->state = CreateExecutorState();

            if (plan->input_count > 0) {
                FillExecArgs(plan);
            } else {
                plan->state->es_param_list_info = NULL;
            }

            plan->qdesc = CreateQueryDesc(querytree, plantree, None);

            plan->tupdesc = ExecutorStart(plan->qdesc, plan->state);
            plan->state->es_processed = 0;
            plan->state->es_lastoid = InvalidOid;

            if (plan->qdesc->operation != CMD_SELECT) {
                TupleTableSlot *slot = NULL;

                ItemPointerData tuple_ctid;
                int count = 0;

                do {
                    slot = ExecProcNode(plan->qdesc->plantree);
                    if (TupIsNull(slot))
                        break;

                    tuple_ctid = slot->val->t_self;

                    switch (plan->qdesc->operation) {
                        case CMD_INSERT:
                            slot->val->t_data->t_oid = GetGenId();
                            ExecAppend(slot, NULL, plan->state);
                            count++;
                            break;
                        case CMD_DELETE:
                            ExecDelete(slot, &tuple_ctid, plan->state);
                            count++;
                            break;
                        case CMD_UPDATE:
                            ExecReplace(slot, &tuple_ctid, plan->state);
                            count++;
                            break;
                        default:
                            elog(DEBUG, "ExecutePlan: unknown operation in queryDesc");
                            break;
                    }
                    if (count % 99 == 0 && CheckForCancel()) {
                        elog(ERROR, "Query Cancelled");
                    }
                } while (true);
                plan->processed += count;
                WResetExecutor(plan);
            }
        }
   }
    plan->stage = STMT_EXEC;
    RELEASE(connection);
    return err;
}

extern long
WFetch(OpaquePreparedStatement plan) {
    WConn connection = SETUP(plan->owner);
    long err;

    if (!pthread_equal(connection->transaction_owner, pthread_self())) {
        char msg[256];
        err = 454;
        snprintf(msg, 255, "transaction is owned by thread %d, cannot make call from this context", connection->transaction_owner);
        SetError(connection, err, "CONTEXT", msg);
        return err;
    }

    READY(connection, err);

    int pos = 0;

    if (CheckForCancel()) {
        elog(ERROR, "Query Cancelled");
    }
    if (plan->stage != STMT_EXEC && plan->stage != STMT_FETCH) {
        elog(ERROR, "no statement executed");
    }
    if (plan->stage == STMT_EOD) {
        coded_elog(ERROR, 1405, "end of data already reached");
    }
    if (plan->fetch_cxt == NULL) {
        plan->fetch_cxt = AllocSetContextCreate(plan->exec_cxt,
                "FetchContext",
                ALLOCSET_DEFAULT_MINSIZE,
                (32 * 1024),
                ALLOCSET_DEFAULT_MAXSIZE);
    }
    
    MemoryContextResetAndDeleteChildren(plan->fetch_cxt);
    MemoryContext old = MemoryContextSwitchTo(plan->fetch_cxt);

    TupleTableSlot *slot = ExecProcNode(plan->qdesc->plantree);

    if (TupIsNull(slot)) {
        err = 4; /*  EOT ( End of Transmission ascii code */
        plan->stage = STMT_EOD;
        WResetExecutor(plan);
   } else {
        HeapTuple tuple = slot->val;
        TupleDesc tdesc = slot->ttc_tupleDescriptor;

        while (plan->output[pos].index != 0 && pos <= MAX_ARGS) {
            Datum val = (Datum) NULL;
            char isnull = 0;

            if (tuple->t_data->t_natts < plan->output[pos].index || plan->output[pos].index <= 0) {
                coded_elog(ERROR, 104, "no attribute");
            }
            if (tuple->t_data->t_natts < pos || pos < 0) {
                coded_elog(ERROR, 107, "wrong number of attributes");
            }

            val = HeapGetAttr(tuple, plan->output[pos].index, tdesc, &isnull);

            if (!isnull) {
                plan->output[pos].freeable = NULL;
                if (!TransferValue(&plan->output[pos], tdesc->attrs[pos], val)) {
                    /* field was not transfered, try and coerce to see if it should someday  */
                    if (can_coerce_type(1, &tdesc->attrs[pos]->atttypid, &plan->output[pos].type)) {
                        coded_elog(ERROR, 105, "Types are compatible but conversion not implemented link type: %d result type: %d",
                                plan->output[pos].type, tdesc->attrs[pos]->atttypid);
                        break;
                    } else {
                        coded_elog(ERROR, 106, "Types do not match, no type conversion . position: %d type: %d result type: %d",
                                pos + 1, plan->output[pos].type, tdesc->attrs[pos]->atttypid);
                        break;
                    }
                }
                if (plan->output[pos].freeable != NULL) *plan->output[pos].notnull = 2;
                else *plan->output[pos].notnull = 1; /*  the value is not null */
            } else {
                *plan->output[pos].notnull = 0; /*  the value is null */
            }
            pos++;
        }
        ExecClearTuple(slot);
        plan->state->es_processed++;
        plan->processed++;
        plan->stage = STMT_FETCH;
    }

    MemoryContextSwitchTo(old);

    RELEASE(connection);
    return err;
}

extern long
WFetchIsComplete(OpaquePreparedStatement stmt) {
    if (stmt->stage == STMT_EOD) return TRUE;
    else return FALSE;
}

extern long
WPrepare(OpaqueWConn conn) {
    WConn connection = SETUP(conn);
    long err = 0;
    if (!pthread_equal(connection->transaction_owner, pthread_self())) {
        char msg[256];
        err = 454;
        snprintf(msg, 255, "transaction is owned by thread %d, cannot make call from this context", connection->transaction_owner);
        SetError(connection, err, "CONTEXT", msg);
        return err;
    }
    READY(connection, err);
    if (CheckForCancel()) {
        elog(ERROR, "Query Cancelled");
    }
    RELEASE(connection);
    return err;
}

extern long
WCommit(OpaqueWConn conn) {
    WConn connection = SETUP(conn);
    long err;
    if (!pthread_equal(connection->transaction_owner, pthread_self())) {
        char msg[256];
        err = 454;
        snprintf(msg, 255, "transaction is owned by thread %d, cannot make call from this context", connection->transaction_owner);
        SetError(connection, err, "CONTEXT", msg);
        return err;
    }
    READY(connection, err);

    if (connection->stage == TRAN_INVALID) {
        elog(ERROR, "connection is currently in an invalid state for commit");
    }
    if (CheckForCancel()) {
        elog(ERROR, "Query Cancelled");
    }

    /* clean up executor   */
    if (connection->stage == TRAN_ABORTONLY) {
        if (CurrentXactInProgress()) {
            WResetQuery(connection);
            AbortTransaction();
        }
        elog(NOTICE, "transaction in abort only mode");
    } else {
        connection->stage = TRAN_COMMIT;
        WResetQuery(connection);
        if (connection->parent == NULL) {
            CommitTransaction();
        } else {
            CloseSubTransaction();
        }
    }

    RELEASE(connection);

    connection->stage = TRAN_INVALID;
    connection->transaction_owner = 0;

    return err;
}

extern long
WRollback(OpaqueWConn conn) {
    WConn connection = SETUP(conn);
    long err;

    if (!pthread_equal(connection->transaction_owner, pthread_self())) {
        char msg[256];
        err = 454;
        snprintf(msg, 255, "transaction is owned by thread %d, cannot make call from this context", connection->transaction_owner);
        SetError(connection, err, "CONTEXT", msg);
        return err;
    }

    READY(connection, err);

    if (connection->stage == TRAN_INVALID) {
        elog(ERROR, "connection is currently in an invalid state for commit");
    }

    connection->stage = TRAN_ABORT;
    if (CurrentXactInProgress()) {
        WResetQuery(connection);
        if (connection->parent == NULL) {
            AbortTransaction();
        } else {
            CloseSubTransaction();
        }
    }

    RELEASE(connection);

    connection->transaction_owner = 0;
    connection->stage = TRAN_INVALID;

    return err;
}

extern long
WBindLink(OpaquePreparedStatement plan, const char *var, void *varAdd, int varSize, short *indAdd, int varType, int cType) {
    WConn connection = SETUP(plan->owner);
    long err = 0;

    if (!pthread_equal(connection->transaction_owner, pthread_self())) {
        char msg[256];
        err = 454;
        snprintf(msg, 255, "transaction is owned by thread %d, cannot make call from this context", connection->transaction_owner);
        SetError(connection, err, "CONTEXT", msg);
        return err;
    }

    READY(connection, err);

    /*  remove the marker flag of the named parameter if there is one */
    switch (*var) {
        case '$':
        case '?':
        case ':':
            var++;
    }

    int index = 0;
    if (CheckForCancel()) {
        elog(ERROR, "Query Cancelled");
    }

    /* find the right binder */
    for (index = 0; index < MAX_ARGS; index++) {
        if (plan->input[index].index == 0 || strcmp(var, plan->input[index].name) == 0)
            break;
    }

    if (index >= MAX_ARGS) {
        coded_elog(ERROR, 105, "too many bind values, max is %d", MAX_ARGS);
    }
/*  if the input is now different, we need to re-parse the statement */
    if ( plan->input[index].index == 0 || plan->input[index].type != varType ) {
        plan->querytreelist = NULL;
        plan->input_count = index+1;
   }
    plan->input[index].index = index + 1;
    strncpy(plan->input[index].name, var, 64);
    plan->input[index].varSize = varSize;
    plan->input[index].type = varType;
    plan->input[index].ctype = cType;
    plan->input[index].target = varAdd;
    plan->input[index].isNotNull = indAdd;
    
    RELEASE(connection);

    return err;
}

extern long
WExecCount(OpaquePreparedStatement stmt) {
    return stmt->processed;
}

extern long
WCancel(OpaqueWConn conn) {
    WConn connection = (WConn) conn;
    int sqlError = 0;

    connection->env->cancelled = true;

    return sqlError;
}

extern long
WCancelAndJoin(OpaqueWConn conn) {
    WConn connection = (WConn) conn;
    int sqlError = 0;

    CancelEnvAndJoin(connection->env);

    return sqlError;
}

static long
WDisposeConnection(OpaqueWConn conn) {
    WConn connection = (WConn) conn;
    int sqlError = 0;

    if (connection->env == NULL)
        return sqlError;
    
    SetEnv(connection->env);
    if (setjmp(connection->env->errorContext) == 0) {
        if (connection->validFlag == 1 && CurrentXactInProgress()) {
            if (connection->parent != NULL) {
                CloseSubTransaction();
            } else {
                AbortTransaction();
            }
        }
    }
    
    FreeXactSnapshot();
    DropNoNameRels();

    if (setjmp(connection->env->errorContext) == 0) {
        MasterUnLock();
        TransactionUnlock();
    }
#ifdef  USE_ASSERT_CHECKING  
    if (setjmp(connection->env->errorContext) == 0) {
        if (BufferPoolCheckLeak()) {
            elog(NOTICE, "Buffer leak in dispose connection");
            ResetBufferPool(false);
        }
    }
#endif
    if (setjmp(connection->env->errorContext) == 0)
        ShutdownDolHelpers();
    if (setjmp(connection->env->errorContext) == 0)
        remove_all_temp_relations();
    if (setjmp(connection->env->errorContext) == 0)
        CallableCleanupInvalidationState();
    if (setjmp(connection->env->errorContext) == 0)
        RelationCacheShutdown();
    if (setjmp(connection->env->errorContext) == 0)
        ThreadReleaseLocks(false);
    if (setjmp(connection->env->errorContext) == 0)
        ThreadReleaseSpins(GetMyThread());
    if (setjmp(connection->env->errorContext) == 0)
        DestroyThread();


    connection->validFlag = -1;
    SetEnv(NULL);
    return sqlError;
}

extern long
WGetTransactionId(OpaqueWConn conn) {
    WConn connection = SETUP(conn);
    long err = 0;
    long xid = -1;

    if (!pthread_equal(connection->transaction_owner, pthread_self())) {
        char msg[256];
        err = 454;
        snprintf(msg, 255, "transaction is owned by thread %d, cannot make call from this context", connection->transaction_owner);
        SetError(connection, err, "CONTEXT", msg);
        return err;
    }

    READY(connection, err);

    if (CheckForCancel()) {
        elog(ERROR, "Query Cancelled");
    }
    if (connection->stage == TRAN_INVALID) {
        elog(ERROR, "transaction not begun");
    }

    xid = GetCurrentTransactionId();

    RELEASE(connection);

    return xid;
}

extern long
WGetCommandId(OpaqueWConn conn) {
    WConn connection = SETUP(conn);
    long err = 0;
    long cid = -1;
    if (!pthread_equal(connection->transaction_owner, pthread_self())) {
        char msg[256];
        err = 454;
        snprintf(msg, 255, "transaction is owned by thread %d, cannot make call from this context", connection->transaction_owner);
        SetError(connection, err, "CONTEXT", msg);
        return err;
    }
    READY(connection, err);

    if (CheckForCancel()) {
        elog(ERROR, "Query Cancelled");
    }
    if (connection->stage == TRAN_INVALID) {
        elog(ERROR, "transaction not begun");
    }

    cid = GetCurrentCommandId();

    RELEASE(connection);

    return cid;
}

extern long
WBeginProcedure(OpaqueWConn conn) {
    WConn connection = SETUP(conn);
    long err = 0;
    if (!pthread_equal(connection->transaction_owner, pthread_self())) {
        char msg[256];
        err = 454;
        snprintf(msg, 255, "transaction is owned by thread %d, cannot make call from this context", connection->transaction_owner);
        SetError(connection, err, "CONTEXT", msg);
        return err;
    }
    READY(connection, err);

    if (CheckForCancel()) {
        elog(ERROR, "Query Cancelled");
    }
    if (connection->stage == TRAN_INVALID) {
        elog(ERROR, "transaction not begun");
    }

    TakeUserSnapshot();

    RELEASE(connection);

    return err;
}

extern long
WEndProcedure(OpaqueWConn conn) {
    WConn connection = SETUP(conn);
    long err = 0;
    if (!pthread_equal(connection->transaction_owner, pthread_self())) {
        char msg[256];
        err = 454;
        snprintf(msg, 255, "transaction is owned by thread %d, cannot make call from this context", connection->transaction_owner);
        SetError(connection, err, "CONTEXT", msg);
        return err;
    }
    READY(connection, err);

    if (CheckForCancel()) {
        elog(ERROR, "Query Cancelled");
    }
    if (connection->stage == TRAN_INVALID) {
        elog(ERROR, "transaction not begun");
    }

    DropUserSnapshot();

    RELEASE(connection);

    return err;
}

void*
WAllocConnectionMemory(OpaqueWConn conn, size_t size) {
    return WAllocMemory(conn, CONNECTION_MEMORY, size);
}

void*
WAllocTransactionMemory(OpaqueWConn conn, size_t size) {
    return WAllocMemory(conn, TRANSACTION_MEMORY, size);
}

void*
WAllocStatementMemory(OpaquePreparedStatement conn, size_t size) {
    WConn connection = SETUP(conn->owner);
    void* pointer;
    int err;

    READY(connection, err);

    if (CheckForCancel()) {
        elog(ERROR, "query cancelled");
    }

    pointer = MemoryContextAlloc(conn->created_cxt, size);
    RELEASE(connection);
    return pointer;
}

void*
WAllocMemory(OpaqueWConn conn, mem_type type, size_t size) {
    WConn connection = SETUP(conn);
    void* pointer;
    MemoryContext cxt;
    int err;

    READY(connection, err);

    if (CheckForCancel()) {
        elog(ERROR, "query cancelled");
    }

    switch (type) {
        case TRANSACTION_MEMORY:
            cxt = MemoryContextGetEnv()->QueryContext;
            break;
        case STATEMENT_MEMORY:
            cxt = MemoryContextGetEnv()->TransactionCommandContext;
            break;
        case CONNECTION_MEMORY:
            cxt = GetEnvMemoryContext();
            break;
        default:
            elog(ERROR, "bad memory context");
    }

    pointer = MemoryContextAlloc(cxt, size);
    RELEASE(connection);
    return pointer;
}

void
WFreeMemory(OpaqueWConn conn, void* pointer) {
    WConn connection = SETUP(conn);
    int err;

    READY(connection, err);

    if (CheckForCancel()) {
        elog(ERROR, "query cancelled");
    }

    pfree(pointer);
    RELEASE(connection);
}

extern long
WUserLock(OpaqueWConn conn, const char *group, uint32_t val, char lockit) {
    WConn connection = SETUP(conn);
    Oid grouplockid = (Oid) - 3;
    long err = 0;

    READY(connection, err);

    char *trax;
    char gname[256];
    Relation rel;

    if (CheckForCancel()) {
        elog(ERROR, "query cancelled");
    }

    memset(gname, 0x00, 255);

    trax = gname;
    while (*group != 0x00) {
        *trax++ = tolower(*group++);
    }
    *trax++ = '/';
    strcpy(trax, "ownerinfo");

    rel = RelationNameGetRelation(gname, GetDatabaseId());
    if (rel != NULL) {
        grouplockid = rel->rd_id;
        RelationDecrementReferenceCount(rel);
    } else {
        strncpy(connection->env->state, "USER", 39);
        coded_elog(ERROR, 502, "user unlock failed -- no relation");
    }

    int lockstate = 0;
    LOCKTAG tag;
    memset(&tag, 0, sizeof (tag));

    tag.relId = grouplockid;
    tag.dbId = GetDatabaseId();
    tag.objId.blkno = (BlockNumber) val;
    if (lockit) {
        TransactionId xid = 0;
        if (LockAcquire(USER_LOCKMETHOD, &tag, xid, ExclusiveLock, true)) {
            lockstate = 0;
        } else {
            lockstate = 1;
        }
        elog(DEBUG, "user lock on group:%s item:%d result:%d", gname, val, lockstate);
    } else {
        TransactionId xid = 0;

        if (LockRelease(USER_LOCKMETHOD, &tag, xid, ExclusiveLock)) {
            lockstate = 0;
        } else {
            strncpy(connection->env->state, "USER", 39);
            coded_elog(ERROR, 501, "user unlock failed");
        }
        elog(DEBUG, "user unlock on group:%s item:%d result:%d", gname, val, lockstate);
    }

    RELEASE(connection);

    return err;
}

extern long
WIsValidConnection(OpaqueWConn conn) {
    WConn connection = (WConn) conn;
    if ( connection == NULL ) return 0;
    if (connection->validFlag > 0) {
        return 1;
    }
    return 0;
}

extern long
WGetErrorCode(OpaqueWConn conn) {
    WConn connection = (WConn) conn;
    if (connection == NULL)
        return -99;
    /*  int to long conversion */
    return (long) connection->CDA.rc;
}

extern const char *
WGetErrorText(OpaqueWConn conn) {
    WConn connection = (WConn) conn;
    if (connection == NULL)
        return "no connection";
    return connection->CDA.text;
}

extern const char *
WGetErrorState(OpaqueWConn conn) {
    WConn connection = (WConn) conn;
    if (connection == NULL)
        return "DISCONNECTED";
    return connection->CDA.state;
}

extern void
WConnectStdIO(OpaqueWConn conn, void* args, pipefunc in, pipefunc out) {
    WConn connection = SETUP(conn);
    long err;
    MemoryContext cxt;

    READY(connection, err);
    cxt = MemoryContextSwitchTo(GetEnvMemoryContext());
    ConnectIO(args, in, out);
    MemoryContextSwitchTo(cxt);
    RELEASE(connection);
}

extern void*
WDisconnectStdIO(OpaqueWConn conn) {
    WConn connection = SETUP(conn);
    long err;
    void* args;

    READY(connection, err);
    args = DisconnectIO();
    RELEASE(connection);

    return args;
}

extern long
WStreamExec(OpaqueWConn conn, char *statement) {
    WConn connection = SETUP(conn);
    long err;

    SetEnv(connection->env);

    err = setjmp(connection->env->errorContext);
    if (err != 0) {
        /*  cannot use READY/RELEASE b/c semantics for streamed connections is different */
        SetAbortOnly();
        CommitTransactionCommand();
        WHandleError(connection, err);
    } else {
        connection->CDA.rc = 0;

        if (CheckForCancel()) {
            elog(ERROR, "query cancelled");
        }
        /* this is really in GetEnv() and defined there  */
        SetWhereToSendOutput(Remote);
        StartTransactionCommand();
        connection->stage = STMT_EXEC;
        pg_exec_query_dest((char *) statement, Remote, false);
        pq_flush();

        CommitTransactionCommand();
    }
    WResetQuery(connection);
    connection->stage = TRAN_INVALID;
    SetEnv(NULL);
    return err;
}

extern Pipe WPipeConnect(OpaqueWConn conn, void* args, pipefunc func) {
    WConn connection = SETUP(conn);
    MemoryContext cxt;
    long err = 0;
    Pipe pipe = NULL;

    READY(connection, err);
    cxt = MemoryContextSwitchTo(GetEnvMemoryContext());
    pipe = ConnectCommBuffer(args, func);
    MemoryContextSwitchTo(cxt);
    RELEASE(connection);

    return pipe;
}

extern void*
WPipeDisconnect(OpaqueWConn conn, Pipe pipe) {
    WConn connection = SETUP(conn);
    void* userargs;
    long err = 0;

    READY(connection, err);

    userargs = DisconnectCommBuffer(pipe);

    RELEASE(connection);
    return userargs;
}

extern int
WPipeSize(OpaqueWConn conn) {
    return sizeof (CommBuffer);
}

void
SetError(WConn connection, int sqlError, char* state, char* err) {
    connection->CDA.rc = sqlError;

    if (sqlError) {
        strncpy(connection->CDA.text, err, 255);
        strncpy(connection->CDA.state, state, 39);
    }
}

void
WHandleError(WConn connection, int sqlError) {
    if (connection == NULL) {
        return;
    }

    if (connection->env == NULL) {
        return;
    }

    memset(connection->CDA.state, '\0', 40);
    memset(connection->CDA.text, '\0', 256);

    SetError(connection, sqlError, connection->env->state, connection->env->errortext);
    clearerror(connection->env);
}

void
WResetQuery(WConn connection) {
    /*  if we are in abort don't worry about shutting down,
    abort cleanup will take care of it.  */
    while ( connection->plan ) {
        WResetExecutor(connection->plan);
    }
    MemoryContextSwitchTo(MemoryContextGetEnv()->QueryContext);
#ifdef MEMORY_STATS
    fprintf(stderr, "memory at query: %d\n", MemoryContextStats(MemoryContextGetEnv()->QueryContext));
#endif
    MemoryContextResetAndDeleteChildren(MemoryContextGetEnv()->QueryContext);
}

void
WResetExecutor(PreparedPlan * plan) {
    if (plan->qdesc != NULL) {
        ExecutorEnd(plan->qdesc, plan->state);
    }

    if ( plan->exec_cxt != NULL ) {
#ifdef MEMORY_STATS
    fprintf(stderr, "memory at exec: %d\n", MemoryContextStats(plan->exec_cxt));
#endif
        MemoryContextResetAndDeleteChildren(plan->exec_cxt);
    } else {
        plan->exec_cxt = AllocSetContextCreate(MemoryContextGetEnv()->QueryContext,
            "ExecutorContext",
            ALLOCSET_DEFAULT_MINSIZE,
            ALLOCSET_DEFAULT_INITSIZE,
            ALLOCSET_DEFAULT_MAXSIZE);
    }

    MemoryContextSwitchTo(plan->exec_cxt);        
    plan->tupdesc = NULL;
    plan->state = NULL;
    plan->qdesc = NULL;

    plan->bind_cxt = NULL;
    plan->fetch_cxt = NULL;
}

/* create copies in case underlying buffer goes away  */
static int
FillExecArgs(PreparedPlan* plan) {
    int k = 0;
    MemoryContext old = NULL;
    ParamListInfo paramLI;

    Assert(plan != NULL);

    plan->bind_cxt = SubSetContextCreate(plan->exec_cxt, "StatementArgumentContext");
    old = MemoryContextSwitchTo(plan->bind_cxt);

    paramLI = (ParamListInfo) palloc((plan->input_count + 1) * sizeof (ParamListInfoData));

    plan->state->es_param_list_info = paramLI;
    for (k = 0; k < plan->input_count; k++) {
        paramLI->kind = PARAM_NAMED;
        paramLI->name = plan->input[k].name;
        paramLI->id = plan->input[k].index;
        paramLI->type = plan->input[k].type;
        paramLI->isnull = !(*plan->input[k].isNotNull);

        if (paramLI->isnull) {
            paramLI->length = 0;
        } else switch (plan->input[k].ctype) {
                case CHAROID:
                    paramLI->length = 1;
                    paramLI->value = *(char *) plan->input[k].target;
                    break;
                case BOOLOID:
                    paramLI->length = 1;
                    paramLI->value = *(char *) plan->input[k].target;
                    break;
                case INT4OID:
                    paramLI->length = 4;
                    paramLI->value = *(int *) plan->input[k].target;
                    break;
                case TIMESTAMPOID:
                    paramLI->length = 8;
                    paramLI->value = (Datum) PointerGetDatum(plan->input[k].target);
                    break;
                case FLOAT8OID:
                    paramLI->length = 8;
                    paramLI->value = (Datum) PointerGetDatum(plan->input[k].target);
                    break;
                case INT8OID:
                    paramLI->length = 8;
                    paramLI->value = (Datum) PointerGetDatum(plan->input[k].target);
                    break;
                case STREAMINGOID:
                    paramLI->length = sizeof (CommBuffer); /* -1 == variable size slot */
                    paramLI->value = (Datum) PointerGetDatum(plan->input[k].target);
                    break;
                case VARCHAROID:
                case BYTEAOID:
                case TEXTOID:
                case BLOBOID:
                case JAVAOID:
                default:
                    /*  EXPERIMENTAL!!! try with no copying  */
                    if (*(int *) plan->input[k].target < 0) {
                        paramLI->value = PointerGetDatum(*(void**) ((char*) plan->input[k].target + 4));
                    } else {
                        if (*(int *) plan->input[k].target > plan->input[k].varSize) {
                            strncpy(plan->owner->env->errortext, "binary truncation on input", 255);
                            longjmp(plan->owner->env->errorContext, 103);
                        }
                        paramLI->value = PointerGetDatum(plan->input[k].target);
                    }
                    /*
                     * need to create data copies here b/c the buffer
                     * holding data may go away after exec
                     */
                    /*
                     * paramLI->value =
                     * (Datum)connection->input[k].target;
                     */
                    /*
                                    paramLI->value = (Datum) palloc(connection->input[k].varSize + 4);
                                    memcpy((char *) paramLI->value, connection->input[k].target, connection->input[k].varSize + 4);
                     */
                    paramLI->length = (Size) (-1); /* -1 == variable size slot */ /* (*(int*)input[k].targe
                                                 * t) + 4;  */
                    break;
            }
        paramLI++;

    }
    paramLI->kind = PARAM_INVALID;
    MemoryContextSwitchTo(old);
}

static PreparedPlan *
ParsePlan(PreparedPlan* plan) {
    List* querytree_list;
    List* plantree_list;
    Oid*    targs;
    char**  names;
    int x;

    Plan *plantree = NULL;
    Query *querytree = NULL;
    MemoryContext old;
    /* parse out a new query and setup plan  */
    /* init for set type */

    if (!plan->querytreelist) {
        MemoryContextResetAndDeleteChildren(plan->created_cxt);
        old = MemoryContextSwitchTo(plan->created_cxt);
        if ( plan->input_count > 0 ) {
            targs = palloc(sizeof(Oid) * plan->input_count);
            names = palloc(sizeof(char*) * plan->input_count);
            for (x=0;x<plan->input_count;x++) {
                targs[x] = plan->input[x].type;
                names[x] = plan->input[x].name;
            }
        }
        querytree_list = pg_parse_and_rewrite(plan->statement, targs, names, plan->input_count, FALSE);
        if (!querytree_list) {
            elog(ERROR, "parsing error");
        }
        plan->querytreelist = querytree_list;

        /*
         * should only be calling one statement at a time if not, you need to
         * do a foreach on the querytree_list to get a plan for each query
         */
        querytree = lfirst(querytree_list);
        while (querytree_list) {
            plantree = pg_plan_query(querytree);
            plantree_list = lappend(plantree_list, plantree);
            querytree_list = lnext(querytree_list);
        }

        plan->plantreelist = plantree_list;
        plan->processed = -1;
        /* set the bind context to NULL until a memory context is created  */
        plan->stage = STMT_PARSED;
        MemoryContextSwitchTo(old);
    }
    return plan;
}
