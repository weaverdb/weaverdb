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

static int TransferExecArgs(PreparedPlan* plan);
static PreparedPlan *ParsePlan(PreparedPlan * plan);
static void SetError(WConn connection, int sqlError, char* state, char* err);
static void* AllocMemory(WConn connection, mem_type type, size_t size);
static int ExpandSlots(PreparedPlan* connection,TransferType type);
static short CheckThreadContext(WConn);
static PreparedPlan* ClearPlan(PreparedPlan* plan);

static SectionId   connection_section_id = SECTIONID("CONN");

#define GETERROR(conn) conn->CDA.rc

#define SETUP(target) (WConn)target

#define READY(target, err)  \
    SetEnv(target->env);\
    \
    err = setjmp(target->env->errorContext);\
    if (err != 0) {\
        strncpy(connection->env->state, "ABORTONLY", 39);\
        target->stage = TRAN_ABORTONLY;\
        SetAbortOnly();\
        WHandleError(target,err);\
        WResetQuery(connection,true);\
    } else {\
        target->CDA.rc = 0\

#define RELEASE(target) \
    } \
    SetEnv(NULL);  \


OpaqueWConn
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
    connection->password = pass == NULL ? NULL : pstrdup(pass);
    connection->name = tName == NULL ? NULL : pstrdup(tName);
    connection->connect = pstrdup(conn);

    connection->memory = AllocSetContextCreate(GetEnvMemoryContext(),
						    "Connection",
						    ALLOCSET_DEFAULT_MINSIZE,
						  ALLOCSET_DEFAULT_INITSIZE,
						  ALLOCSET_DEFAULT_MAXSIZE);
    connection->env = env;
    connection->plan = NULL;

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

    if ( !CallableInitInvalidationState() ) {
        strncpy(connection->env->errortext, "unsuccessful connection -- too many connections", 255);
        strncpy(connection->env->state, "DISCONNECTED", 39);
        
        DestroyThread();
        SetEnv(NULL);
        DestroyEnv(env);

        return NULL;
    }
    
    RelationInitialize();
    InitCatalogCache();

    SetProcessingMode(NormalProcessing);

    /* this code checks to see if the user is valid  */
    if (dbid != InvalidOid) {
        short winner = false;
        HeapTuple ht = NULL;
        char isNull = true;

        if (tName != NULL && strlen(tName) > 0) {
            ht = SearchSysCacheTuple(SHADOWNAME, PointerGetDatum(tName), 0, 0, 0);
            if (HeapTupleIsValid(ht)) {
                Datum dpass = SysCacheGetAttr(SHADOWNAME, ht, Anum_pg_shadow_passwd, &isNull);
                if (!isNull) {
                    char cpass[64];
                    memset(cpass,0x00,64);
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
    int sqlError = 0;
    WConn connection = NULL;
    Env*              env;

    if (parent->parent != NULL) {
        sqlError = 99;
        strncpy(parent->env->errortext, "a sub-connection cannot spawn children", 255);
        strncpy(parent->env->state, "DISCONNECTED", 39);
        WHandleError(parent, sqlError);
        return connection;
    }
    
    env = CreateEnv(parent->env);
    
    if (env == NULL) {
        sqlError = 99;
        strncpy(parent->env->errortext, "unsuccessful connection -- too many connections", 255);
        strncpy(parent->env->state, "DISCONNECTED", 39);
        WHandleError(parent, sqlError);
        return connection;
    } else {
        SetEnv(env);
        MemoryContextInit();
    }
    
    connection = AllocateEnvSpace(connection_section_id,sizeof (struct Connection));
    memmove(connection, parent, sizeof (struct Connection));

    
    connection->env = env;
    SetProcessingMode(InitProcessing);

    connection->env->DatabaseId = parent->env->DatabaseId;
    connection->env->DatabaseName = parent->env->DatabaseName;
    connection->env->DatabasePath = parent->env->DatabasePath;
    connection->env->UserName = parent->env->UserName;
    connection->env->UserId = parent->env->UserId;

    InitThread(NORMAL_THREAD);
    if ( !CallableInitInvalidationState() ) {
        DestroyThread();
        SetEnv(NULL);
        DestroyEnv(env);
        return NULL;
    }
    
    RelationInitialize();
    InitCatalogCache();

    connection->env->Mode = NormalProcessing;
    connection->stage = TRAN_INVALID;

    pthread_mutex_lock(&parent->child_lock);
    connection->parent = parent;
    connection->child_count++;
    pthread_mutex_unlock(&parent->child_lock);

    SetEnv(NULL);

    return (OpaqueWConn) connection;
}

long
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
        SetEnv(conn->env);
        WDestroyPreparedStatement(conn->plan);
        SetEnv(NULL);
    }
    
    if (conn->validFlag >= 0) {
        WCancelAndJoin(conn);
        WDisposeConnection(conn);
    }

    if (conn->env != NULL) {
        DestroyEnv(conn->env);
    }

    return 0;
}

long
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

    /*  only do this if we are a top level connection  */
    if (connection->parent == NULL) {
        WResetQuery(connection,false);
        BeginTransactionBlock();
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
    connection->stage = TRAN_BEGIN;

    RELEASE(connection);
    return err;
}

char*
WStatement(OpaquePreparedStatement plan) {
    return plan->statement;
}

OpaquePreparedStatement
WPrepareStatement(OpaqueWConn conn, const char *smt) {
    WConn connection = SETUP(conn);
    long err = 0;
    int k = 0;
    PreparedPlan* plan;
    MemoryContext plan_cxt,old;

    if (connection->stage == TRAN_INVALID) {
        err = 455;
        SetError(connection, err, "CONTEXT", "context not valid, check call sequence");
        return NULL;
    }
    
    if ( connection->stage == TRAN_ABORTONLY ) {
        err = 456;
        SetError(connection, err, "CONTEXT", "context not valid, an error has already occured");
    }

    if (CheckThreadContext(conn)) {
        return NULL;
    }

    READY(connection, err);

    if (CheckForCancel()) {
        elog(ERROR, "Query Cancelled");
    }
    
    plan_cxt = AllocSetContextCreate(GetEnvMemoryContext(),
            "PreparedPlanContext",
            ALLOCSET_DEFAULT_MINSIZE,
            ALLOCSET_DEFAULT_INITSIZE,
            ALLOCSET_DEFAULT_MAXSIZE);

    old = MemoryContextSwitchTo(plan_cxt);
    plan = (PreparedPlan *) palloc(sizeof (PreparedPlan));
    plan->statement = pstrdup(smt);
    plan->plan_cxt = plan_cxt;
    plan->owner = connection;
    plan->slots = START_ARGS;
    plan->slot = palloc(sizeof(InputOutput) * START_ARGS);
    memset(plan->slot, 0, sizeof (InputOutput) * START_ARGS);
    for (k=0;k<START_ARGS;k++) {
        plan->slot[k].transferType = TFREE;
    }

    plan->tupdesc = NULL;
    plan->state = NULL;
    plan->qdesc = NULL;
    
    plan->querytreelist = NULL;
    plan->plantreelist = NULL;
    
    plan->node_cxt = NULL;
    plan->exec_cxt = NULL;
    plan->fetch_cxt = NULL;
    plan->stage = STMT_NEW;
    
    plan->next = connection->plan;
    connection->plan = plan;

    RELEASE(connection);

    return plan;
}

long
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

    if ( stmt->exec_cxt ) MemoryContextDelete(stmt->exec_cxt);
    MemoryContextDelete(stmt->plan_cxt);
    
    err = ( connection->plan == NULL ) ? 1 : 0;
    RELEASE(connection);
    return err;
}

long
WOutputTransfer(OpaquePreparedStatement plan, short pos, int type, void* userenv, transferfunc func) {
    WConn connection = SETUP(plan->owner);
    long err;
    int index;

    if (CheckThreadContext(connection)) {
        return GETERROR(connection);
    }

    READY(connection, err);

    if (CheckForCancel()) {
        elog(ERROR, "Query Cancelled");
    }

    if (pos > MAX_ARGS || pos <= 0) {
        coded_elog(ERROR, 101, "bad value - index must be greater than 0 and less than %d", MAX_ARGS);
    }
    /* find the right binder */
    for (index = 0; index < plan->slots; index++) {
        if (plan->slot[index].transferType == TFREE || plan->slot[index].index == pos)
            break;
    }

    plan->slot[index].transferType = TOUTPUT;
    plan->slot[index].index = pos;
    plan->slot[index].varType = type;
    plan->slot[index].userargs = userenv;
    plan->slot[index].transfer = func;

    RELEASE(connection);

    return err;
}

long
WExec(OpaquePreparedStatement plan) {
    WConn connection = SETUP(plan->owner);
    long err = 0;

    List *trackquery = NULL;
    List *trackplan = NULL;
    Plan *plantree = NULL;
    Query *querytree = NULL;

    if ( connection->stage == TRAN_ABORTONLY ) {
        err = 456;
        SetError(connection, err, "CONTEXT", "context not valid, an error has already occured");
        return err;
    }

    if (CheckThreadContext(connection)) {
        return GETERROR(connection);
    }  
    
    READY(connection, err);

    if (CheckForCancel()) {
        elog(ERROR, "Query Cancelled");
    }

    WResetExecutor(plan);

    plan = ParsePlan(plan);

    plan->processed = 0;
    
    trackquery = plan->querytreelist;
    trackplan = plan->plantreelist;

    while (trackquery) {

        querytree = (Query *) lfirst(trackquery);
        trackquery = lnext(trackquery);

        plantree = (Plan *) lfirst(trackplan);
        trackplan = lnext(trackplan);

        SetQuerySnapshot();

        if (querytree->commandType == CMD_UTILITY) {
            ProcessUtility(querytree->utilityStmt, None);
            plan->processed += 1;  // one util op processed
            /*
             * increment after any utility if there are
             * more subqueries to execute
             */
        } else {
            plan->state = CreateExecutorState();
            
            if (TransferExecArgs(plan) == 0) {
                pfree(plan->state->es_param_list_info);
                plan->state->es_param_list_info = NULL;
            }

            plan->qdesc = CreateQueryDesc(querytree, plantree, None);

            plan->tupdesc = ExecutorStart(plan->qdesc, plan->state);
            plan->state->es_processed = 0;
            plan->state->es_lastoid = InvalidOid;
            plan->stage = STMT_EXEC;

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
                        case CMD_PUT:
                            if ( ExecPut(slot,&tuple_ctid,plan->state) == HeapTupleUpdated ) {
                                count++;
                            }
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

        /*
         * Increment Command Counter so we see everything
         * that happened in this transaction to here
         */
        CommandCounterIncrement();
    }
    RELEASE(connection);
    return err;
}

long
WFetch(OpaquePreparedStatement plan) {
    WConn connection = SETUP(plan->owner);
    long err;

    if (CheckThreadContext(connection)) {
        return GETERROR(connection);
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
        Assert(plan->exec_cxt != NULL);
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
        WResetExecutor(plan);
        plan->stage = STMT_EOD;
   } else {
        HeapTuple tuple = slot->val;
        TupleDesc tdesc = slot->ttc_tupleDescriptor;
        int pos = 0;

        for (pos=0;pos<plan->slots;pos++) {
            if (plan->slot[pos].transferType == TOUTPUT) {
                Datum val = (Datum) NULL;
                char isnull = 0;

                if (tuple->t_data->t_natts < plan->slot[pos].index) {
                    continue;
                }
             
                if (plan->slot[pos].index <= 0) {
                    coded_elog(ERROR, 104, "unassigned attribute");
                }

                if (plan->stage != STMT_FETCH && plan->processed == 0) {
                    TransferColumnName(&plan->slot[pos], tdesc->attrs[plan->slot[pos].index - 1]);
                }

                val = HeapGetAttr(tuple, plan->slot[pos].index, tdesc, &isnull);

                if (!isnull) {
                    if (!TransferToRegistered(&plan->slot[pos], tdesc->attrs[plan->slot[pos].index - 1], val, false)) {
                        Oid vType = plan->slot[pos].varType;
                        /* field was not transfered, try and coerce to see if it should someday  */
                        if (can_coerce_type(1, &tdesc->attrs[pos]->atttypid, &vType)) {
                            coded_elog(ERROR, 105, "Types are compatible but conversion not implemented link type: %d result type: %d",
                                    plan->slot[pos].varType, tdesc->attrs[plan->slot[pos].index - 1]->atttypid);
                            break;
                        } else {
                            coded_elog(ERROR, 106, "Types do not match, no type conversion . position: %d type: %d result type: %d",
                                    plan->slot[pos].index, plan->slot[pos].varType, tdesc->attrs[plan->slot[pos].index - 1]->atttypid);
                            break;
                        }
                    }
                } else {
                    TransferToRegistered(&plan->slot[pos], tdesc->attrs[plan->slot[pos].index - 1], PointerGetDatum(NULL), true);
                }
            }
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

long
WFetchIsComplete(OpaquePreparedStatement stmt) {
    if (stmt->stage == STMT_EOD) return TRUE;
    else return FALSE;
}

long
WPrepare(OpaqueWConn conn) {
    WConn connection = SETUP(conn);
    long err = 0;

    if (CheckThreadContext(connection)) {
        return GETERROR(connection);
    }

    if (connection->stage != TRAN_BEGIN) {
        SetError(connection, 456, "CONTEXT", "no transaction active");
        return 1;
    }

    READY(connection, err);
    if (IsAbortedTransactionBlockState()) {
        elog(ERROR, "Transaction is abort only");
    }

    if (CheckForCancel()) {
        elog(ERROR, "Query Cancelled");
    }
    RELEASE(connection);
    return err;
}

long
WCommit(OpaqueWConn conn) {
    WConn connection = SETUP(conn);
    long err;

    if (CheckThreadContext(connection)) {
        return GETERROR(connection);
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
            WResetQuery(connection,false);
            AbortTransaction();
            AbandonTransactionBlock();
        }
        elog(ERROR, "transaction in abort only mode");
    } else {
        connection->stage = TRAN_COMMIT;
        WResetQuery(connection,false);
        if (connection->parent == NULL) {
            CommitTransaction();
            AbandonTransactionBlock();
        } else {
            CloseSubTransaction();
        }
    }

    RELEASE(connection);

    connection->stage = TRAN_INVALID;
    connection->transaction_owner = 0;

    return err;
}

long
WRollback(OpaqueWConn conn) {
    WConn connection = SETUP(conn);
    long err;

    if (CheckThreadContext(connection)) {
        return GETERROR(connection);
    }
    READY(connection, err);

    if (connection->stage == TRAN_INVALID) {
        elog(ERROR, "connection is currently in an invalid state for commit");
    }

    connection->stage = TRAN_ABORT;
    if (CurrentXactInProgress()) {
        WResetQuery(connection,false);
        if (connection->parent == NULL) {
            AbortTransaction();
            AbandonTransactionBlock();
        } else {
            CloseSubTransaction();
        }
    }

    RELEASE(connection);

    connection->transaction_owner = 0;
    connection->stage = TRAN_INVALID;

    return err;
}

int
ExpandSlots(PreparedPlan* plan, TransferType type) {
    int x=plan->slots;
    plan->slot = repalloc(plan->slot, sizeof(InputOutput) * plan->slots * 2);
    memset(plan->slot + (plan->slots),0x00,(sizeof(InputOutput) * plan->slots));
    for (x=plan->slots;x<plan->slots * 2;x++) {
        plan->slot[x].transferType = TFREE;
    }
    plan->slots *= 2;
    return plan->slots;
}

long
WBindTransfer(OpaquePreparedStatement plan, const char* var, int type, void* userenv, transferfunc func) {
    WConn connection = SETUP(plan->owner);
    long err = 0;

    if (CheckThreadContext(connection)) {
        return GETERROR(connection);
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
    for (index = 0; index < plan->slots; index++) {
        if (plan->slot[index].transferType == TFREE || strcmp(var, plan->slot[index].name) == 0)
            break;
    }

    if (index == plan->slots) {
        ExpandSlots(plan,TFREE);
    }
        
    if ( plan->slot[index].name == NULL ) {
        plan->slot[index].name = MemoryContextStrdup(plan->plan_cxt,var);
    }
    plan->slot[index].transferType = TINPUT;
    plan->slot[index].varType = type;
    plan->slot[index].userargs = userenv;
    plan->slot[index].transfer = func;
    
    RELEASE(connection);

    return err;
}

long
WExecCount(OpaquePreparedStatement stmt) {
    return stmt->processed;
}

long
WCancel(OpaqueWConn conn) {
    WConn connection = (WConn) conn;
    int sqlError = 0;

    connection->env->cancelled = true;

    return sqlError;
}

long
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
    
    if ( !SetEnv(connection->env) ) return -1;
    
    if (setjmp(connection->env->errorContext) == 0) {
        if (connection->validFlag == 1 && CurrentXactInProgress()) {
            if (connection->parent != NULL) {
                CloseSubTransaction();
            } else {
                AbortTransaction();
                AbandonTransactionBlock();
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
        RelationCacheShutdown();
    if (setjmp(connection->env->errorContext) == 0)
        ThreadReleaseLocks(false);
    if (setjmp(connection->env->errorContext) == 0)
        ThreadReleaseSpins(GetMyThread());
    if (setjmp(connection->env->errorContext) == 0)
        DestroyThread();
    if (setjmp(connection->env->errorContext) == 0)
        CallableCleanupInvalidationState();

    connection->validFlag = -1;
    SetEnv(NULL);
    return sqlError;
}

long
WGetTransactionId(OpaqueWConn conn) {
    WConn connection = SETUP(conn);
    long err = 0;
    long xid = -1;

    if (CheckThreadContext(connection)) {
        return GETERROR(connection);
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

long
WGetCommandId(OpaqueWConn conn) {
    WConn connection = SETUP(conn);
    long err = 0;
    long cid = -1;

    if (CheckThreadContext(connection)) {
        return GETERROR(connection);
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

long
WBeginProcedure(OpaqueWConn conn) {
    WConn connection = SETUP(conn);
    long err = 0;

    if (CheckThreadContext(connection)) {
        return GETERROR(connection);
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

long
WEndProcedure(OpaqueWConn conn) {
    WConn connection = SETUP(conn);
    long err = 0;

    if (CheckThreadContext(connection)) {
        return GETERROR(connection);
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
    return AllocMemory(conn, CONNECTION_MEMORY, size);
}

void*
WAllocTransactionMemory(OpaqueWConn conn, size_t size) {
    return AllocMemory(conn, TRANSACTION_MEMORY, size);
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

    pointer = MemoryContextAlloc(conn->plan_cxt, size);
    RELEASE(connection);
    return pointer;
}

void*
AllocMemory(WConn conn, mem_type type, size_t size) {
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
            cxt = connection->memory;
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

    pfree(pointer);
    RELEASE(connection);
}


void
WCheckMemory(OpaqueWConn conn) {
    WConn connection = SETUP(conn);
    int err;

    READY(connection, err);
    fprintf(stdout, "memory of connection: %ld\n", MemoryContextStats(conn->memory));    
   
    RELEASE(connection);
}

long
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

    memset(gname,0x00,  255);

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
    memset(&tag,0x00, sizeof (tag));

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

long
WIsValidConnection(OpaqueWConn conn) {
    WConn connection = (WConn) conn;
    if ( connection == NULL ) return 0;
    if (connection->validFlag > 0) {
        return 1;
    }
    return 0;
}

long
WGetErrorCode(OpaqueWConn conn) {
    WConn connection = (WConn) conn;
    if (connection == NULL)
        return -99;
    /*  int to long conversion */
    return (long) connection->CDA.rc;
}

const char *
WGetErrorText(OpaqueWConn conn) {
    WConn connection = (WConn) conn;
    if (connection == NULL)
        return "no connection";
    return connection->CDA.text;
}

const char *
WGetErrorState(OpaqueWConn conn) {
    WConn connection = (WConn) conn;
    if (connection == NULL)
        return "DISCONNECTED";
    return connection->CDA.state;
}

void
WConnectStdIO(OpaqueWConn conn, void* args, transferfunc in, transferfunc out) {
    WConn connection = SETUP(conn);
    long err;
    MemoryContext cxt;

    READY(connection, err);
    cxt = MemoryContextSwitchTo(GetEnvMemoryContext());
    ConnectIO(args, in, out);
    MemoryContextSwitchTo(cxt);
    RELEASE(connection);
}

void*
WDisconnectStdIO(OpaqueWConn conn) {
    WConn connection = SETUP(conn);
    long err;
    void* args;

    READY(connection, err);
    args = DisconnectIO();
    RELEASE(connection);

    return args;
}

long
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
//        SetWhereToSendOutput(Remote);
        StartTransactionCommand();
        pg_exec_query_dest((char *) statement, Remote, false);
        pq_flush();
        CommitTransactionCommand();
    }
    connection->stage = TRAN_INVALID;
    SetEnv(NULL);
    return err;
}

static void
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
    
    memset(connection->CDA.state,0x00, 40);
    memset(connection->CDA.text,0x00, 256);

    SetError(connection, sqlError, connection->env->state, connection->env->errortext);
    clearerror(connection->env);
}

void
WResetQuery(WConn connection,bool err) {
    /*  if we are in abort don't worry about shutting down,
    abort cleanup will take care of it.  */
    OpaquePreparedStatement plan = connection->plan;
    while ( plan ) {
        if (err) {
            plan->stage = STMT_ABORT;
        }
        WResetExecutor(plan);
        ClearPlan(plan); 

        plan = plan->next;
    }
    MemoryContextSwitchTo(MemoryContextGetEnv()->QueryContext);
#ifdef MEMORY_STATS
    fprintf(stderr, "memory at query: %ld\n", MemoryContextStats(MemoryContextGetEnv()->QueryContext));
#endif
    MemoryContextResetAndDeleteChildren(MemoryContextGetEnv()->QueryContext);
}

void
WResetExecutor(PreparedPlan * plan) {
    if (plan->stage == STMT_EXEC || plan->stage == STMT_FETCH) {
        Assert(plan->qdesc != NULL);
        Assert(plan->state != NULL);
        ExecutorEnd(plan->qdesc, plan->state);
        plan->stage = STMT_EMPTY;
    }

    if ( plan->exec_cxt != NULL ) {
#ifdef MEMORY_STATS
        fprintf(stderr, "memory at exec: %ld\n", MemoryContextStats(plan->exec_cxt));
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

    plan->fetch_cxt = NULL;    
}
static int
TransferExecArgs(PreparedPlan* plan) {
    int k = 0;
    int inputs = 0;
    MemoryContext old,bind_cxt;
    ParamListInfo paramLI;

    Assert(plan != NULL);

    bind_cxt = SubSetContextCreate(plan->exec_cxt, "StatementArgumentContext");
    old = MemoryContextSwitchTo(bind_cxt);

    paramLI = (ParamListInfo) palloc((plan->slots + 1) * sizeof (ParamListInfoData));

    plan->state->es_param_list_info = paramLI;
    for (k = 0; k < plan->slots; k++) {
        if (plan->slot[k].transferType == TINPUT) {
            inputs += 1;
            paramLI->kind = PARAM_NAMED;
            paramLI->name = plan->slot[k].name;
            paramLI->id = plan->slot[k].index;
            paramLI->isnull = false;
            paramLI->type = plan->slot[k].varType;

            switch (plan->slot[k].varType) {
                case CHAROID:
                case BOOLOID: {
                    char value;
                    paramLI->length = plan->slot[k].transfer(plan->slot[k].userargs, plan->slot[k].varType, &value,1);
                    paramLI->byval = true;
                    if (paramLI->length > 0) {
                        paramLI->value = CharGetDatum(value);
                    } else {
                        paramLI->isnull = true;
                    }
                    break;
                }
                case INT4OID: {
                    int32 value;
                    paramLI->length = plan->slot[k].transfer(plan->slot[k].userargs, plan->slot[k].varType, &value,4);
                    paramLI->byval = true;
                    if (paramLI->length > 0) {
                        paramLI->value = Int32GetDatum(value);
                    } else {
                        paramLI->isnull = true;
                    }
                    break;
                }
                case TIMESTAMPOID:
                case FLOAT8OID:
                case INT8OID: {
                    long* value = palloc(8);
                    paramLI->length = plan->slot[k].transfer(plan->slot[k].userargs, plan->slot[k].varType, value,8);
                    paramLI->byval = false;
                    if (paramLI->length > 0) {
                        paramLI->value = PointerGetDatum(value);
                    } else {
                        paramLI->isnull = true;
                    }
                    break;
                }
                case STREAMINGOID: {
                    int nullcheck = plan->slot[k].transfer(plan->slot[k].userargs, plan->slot[k].varType, NULL, NULL_CHECK_OP);
                    paramLI->byval = false;
                    if (nullcheck > 0) {
                        CommBuffer* value = ConnectCommBuffer(plan->slot[k].userargs, plan->slot[k].transfer);
                        paramLI->length = sizeof(CommBuffer);
                        paramLI->value = PointerGetDatum(value);
                    } else {
                        paramLI->isnull = true;
                    }
                    break;
                }
                case VARCHAROID:
                case BYTEAOID:
                case TEXTOID:
                case BLOBOID:
                case JAVAOID:
                default: {
                    int len = plan->slot[k].transfer(plan->slot[k].userargs, plan->slot[k].varType, NULL,LENGTH_QUERY_OP);
                    paramLI->byval = false;
                    if (len >= 0) {
                        char* value = palloc(len + VARHDRSZ);
                        if (len != plan->slot[k].transfer(plan->slot[k].userargs, plan->slot[k].varType, VARDATA(value),len)) {
                            coded_elog(ERROR, 889, "binary truncation expected length: %d", len);
                            // should result in jump
                            return TRUNCATION_VALUE;
                        }
                        SETVARSIZE(value, len + VARHDRSZ);
                        paramLI->value = PointerGetDatum(value);
                        paramLI->length = len + VARHDRSZ;
                    } else {
                        paramLI->isnull = true;
                    }
                    break;
                }
            }
            paramLI++;
        }
    }
    paramLI->kind = PARAM_INVALID;
    MemoryContextSwitchTo(old);
    return inputs;
}

static PreparedPlan *
ParsePlan(PreparedPlan* plan) {
    List *querytree_list,*iterator;
    List* plantree_list = NULL;
    Oid*    targs = NULL;
    char**  names = NULL;
    int count = 0;

    MemoryContext old;
    /* parse out a new query and setup plan  */
    /* init for set type */
    if (!plan->node_cxt) {
        plan->node_cxt = AllocSetContextCreate(plan->plan_cxt, "ParseContext",
						    ALLOCSET_DEFAULT_MINSIZE,
						  ALLOCSET_DEFAULT_INITSIZE,
						  ALLOCSET_DEFAULT_MAXSIZE);
        old = MemoryContextSwitchTo(plan->node_cxt);
        if ( plan->slots > 0 ) {
            int k = 0;
            targs = palloc(sizeof(Oid) * plan->slots);
            names = palloc(sizeof(char*) * plan->slots);

            for (k=0;k<plan->slots;k++) {
                if (plan->slot[k].transferType == TINPUT) {
                    targs[count] = plan->slot[k].varType;
                    names[count] = plan->slot[k].name;
                    count += 1;
                }
            }
        }
        querytree_list = pg_parse_and_rewrite(plan->statement, targs, names, count, FALSE);
        if (!querytree_list) {
            elog(ERROR, "parsing error");
        }  
        
        if ( targs ) pfree(targs);
        if ( names ) pfree(names);
        /*
         * should only be calling one statement at a time if not, you need to
         * do a foreach on the querytree_list to get a plan for each query
         */
        iterator = querytree_list;
        while (iterator) {
            plantree_list = lappend(plantree_list, pg_plan_query(lfirst(iterator)));
            iterator = lnext(iterator);
        }
        
        plan->querytreelist = querytree_list;
        plan->plantreelist = plantree_list;
        plan->processed = -1;
        /* set the bind context to NULL until a memory context is created  */
        MemoryContextSwitchTo(old);
    }

    plan->stage = STMT_PARSED;
    return plan;
}

short CheckThreadContext(WConn connection) {
    long err;
    if (connection->transaction_owner == 0) {
        char msg[256];
        err = 453;
        snprintf(msg, 255, "no transaction is active");
        SetError(connection, err, "CONTEXT", msg);
        return 1;
    } else if (!pthread_equal(connection->transaction_owner, pthread_self())) {
        char msg[256];
        err = 454;
        snprintf(msg, 255, "transaction is owned by thread %p, cannot make call from this context", connection->transaction_owner);
        SetError(connection, err, "CONTEXT", msg);
        return 1;
    }
    return 0;
}

PreparedPlan* ClearPlan(PreparedPlan* plan) {
/*  if the input is now different, we need to re-parse the statement */
    if ( plan->node_cxt ) {
        MemoryContextDelete(plan->node_cxt);
        plan->node_cxt = NULL;
        plan->plantreelist = NULL;
        plan->querytreelist = NULL;
        plan->stage = STMT_NEW;
    }

    return plan;
}