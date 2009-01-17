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

/* buffer flusher for external world */

static long WDisposeConnection( OpaqueWConn conn );

static int      FillExecArgs(WConn connection);
static int      FreeExecArgs(PreparedPlan * plan);
static PreparedPlan *PreparePlan(char *statement, Oid * targs, char **lineup, int nargs);
static void SetError(WConn connection,int sqlError,char* state,char* err);
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
        if (GetTransactionInfo()->SharedBufferChanged) target->abortonly = 1;\
        WHandleError(target,err);\
    } else {\
        target->CDA.rc = 0\

#define RELEASE(target) \
    } \
    SetEnv(NULL);  \


    
    
extern OpaqueWConn 
WCreateConnection(const char *tName, const char *pass, const char *conn)
{
        int             sqlError = 0;
        long            opCode;
        char            dbpath[512];
        Oid             dbid = InvalidOid;
        WConn          connection = NULL;
        
        if ( !isinitialized() ) return NULL;
        
        connection = os_malloc(sizeof(struct Connection));
        memset(connection, 0x00, sizeof(struct Connection));
        
        connection->validFlag = -1;
        strncpy(connection->password, pass, 255);
        strncpy(connection->name, tName, 255);
        strncpy(connection->connect, conn, 255);

        connection->env = CreateEnv(NULL);
        if (connection->env == NULL) {
                sqlError = 99;
                strncpy(connection->env->errortext, "unsuccessful connection -- too many connections", 255);
                strncpy(connection->env->state, "DISCONNECTED", 39);
                WHandleError(connection, sqlError);
                os_free(connection);
                return NULL;
        }
        
        SetEnv(connection->env);

        connection->env->Mode = InitProcessing;
        connection->nargs = 0;

        MemoryContextInit();

        SetDatabaseName(conn);
        GetRawDatabaseInfo(conn, &dbid, dbpath);

        if (dbid == InvalidOid) {
            sqlError = 99;
            strncpy(connection->env->errortext, "unsuccessful connection -- too many connections", 255);
            strncpy(connection->env->state, "DISCONNECTED", 39);
/*  destroy env taakes care of the memory cxt  */
            SetEnv(NULL);
            DestroyEnv(connection->env);

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
        if ( dbid != InvalidOid ) {
                short           winner = false;
                HeapTuple       ht = NULL;
                char            isNull = true;

                ht = SearchSysCacheTuple(SHADOWNAME, PointerGetDatum(tName), 0, 0, 0);
                if (HeapTupleIsValid(ht)) {
                        Datum           dpass = SysCacheGetAttr(SHADOWNAME, ht, Anum_pg_shadow_passwd, &isNull);
                        if (!isNull) {
                                char            cpass[256];
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

        }

        connection->stage = STMT_INVALID;

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
WCreateSubConnection(OpaqueWConn parent)  {
        int             sqlError = 0;
        long            opCode;
        WConn          connection = NULL;

        if ( parent->parent != NULL ) {
                sqlError = 99;
                strncpy(parent->env->errortext, "a sub-connection cannot spawn children", 255);
                strncpy(parent->env->state, "DISCONNECTED", 39);
                WHandleError(parent, sqlError);
                return connection;
        }
   
        connection = MemoryContextAlloc(parent->env->global_context,sizeof(struct Connection));
        memmove(connection,parent,sizeof(struct Connection));

        connection->env = CreateEnv(parent->env);
        if (connection->env == NULL) {
                sqlError = 99;
                strncpy(connection->env->errortext, "unsuccessful connection -- too many connections", 255);
                strncpy(connection->env->state, "DISCONNECTED", 39);
                WHandleError(connection, sqlError);
                return connection;
        }
        SetEnv(connection->env);

        connection->env->Mode = InitProcessing;
        connection->nargs = 0;
        
        connection->env->DatabaseId = parent->env->DatabaseId;
        connection->env->DatabaseName = parent->env->DatabaseName;
        connection->env->DatabasePath = parent->env->DatabasePath;
        connection->env->UserName = parent->env->UserName;
        connection->env->UserId = parent->env->UserId;

        MemoryContextInit();
        InitThread(NORMAL_THREAD);

        RelationInitialize();
        InitCatalogCache();

        CallableInitInvalidationState();
        connection->env->Mode = NormalProcessing;
        connection->stage = STMT_INVALID;
        
        pthread_mutex_lock(&parent->child_lock);
        connection->parent = parent;
        connection->child_count++;
        pthread_mutex_unlock(&parent->child_lock);

        SetEnv(NULL);
       
        return (OpaqueWConn) connection;
}

extern long 
WDestroyConnection(OpaqueWConn conn)
{
        int             status = 0;
        WConn     parent = conn->parent;

        if ( parent ) {
            int x = 0; 
            int land = 0;
            pthread_mutex_lock(&parent->child_lock);
            parent->child_count--;
            pthread_mutex_unlock(&parent->child_lock);
        } else {
            pthread_mutex_lock(&conn->child_lock);
            while ( conn->child_count > 0 ) {
                pthread_t  waitfor = 0;
                void*       result;
                pthread_mutex_unlock(&conn->child_lock);
                waitfor = FindChildThread(conn->env);
                if ( waitfor != 0 ) pthread_join(waitfor,&result);
                pthread_mutex_lock(&conn->child_lock);
            }
            pthread_mutex_unlock(&conn->child_lock);
        }
        
        if (conn->validFlag >= 0)
            WDisposeConnection(conn);
        if (conn->env != NULL) {
            DestroyEnv(conn->env);
            conn->env = NULL;
        }

        if ( !parent ) 
            os_free(conn);
        else 
            pfree(conn);
}

extern long 
WBegin(OpaqueWConn conn, long trans)
{
    long  err;
    WConn connection = SETUP(conn);
    
    clearerror(connection->env);
    
    if ( connection->transaction_owner != 0 ) {
        SetError(connection,454,"CONTEXT","transaction owner already set");
    }

    READY(connection,err);

    if (connection->stage != STMT_INVALID) {
        elog(ERROR, "already in transaction %d", connection->stage);
    }
    connection->abortonly = 0;

    connection->plan = NULL;
    connection->stage = STMT_INVALID;
/*  only do this if we are a top level connection  */
    if ( connection->parent == NULL ) {
        StartTransaction();
        SetQuerySnapshot();
    } else {
        if ( connection->parent->stage == STMT_INVALID ) {
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

extern long 
WParsingFunc(OpaqueWConn conn, const char *smt)
{
        WConn          connection = SETUP(conn);
        List           *querytree_list = NULL;
        List           *plantree_list = NULL;
        int             c = 0;
        long            err = 0;
        int stmtlen = strlen(smt);

        if ( !pthread_equal(connection->transaction_owner,pthread_self()) ) {
            char    msg[256];
            err = 454;
            snprintf(msg,255,"transaction is owned by thread %d, cannot make call from this context",connection->transaction_owner);
            SetError(connection,err,"CONTEXT",msg);
            return err;
        }
        /* If begin was not called, call it  */
        if (connection->stage == STMT_INVALID) {
            err = 455;
            SetError(connection,err,"CONTEXT","context not valid, check call sequence");
            return err;
        }

        if ( stmtlen > 8191 ) {
            err = 456;
            SetError(connection,err,"CONTEXT","statement is longer than the 8k character max.");
            return err;
        }

        READY(connection,err);
     
        if ( CheckForCancel() ) {
            elog(ERROR,"Query Cancelled");
        }

        WResetQuery(connection);
            
        memmove(connection->statement, smt, stmtlen+1);

        connection->stage = STMT_PARSED;

        connection->plan = NULL;
        memset(connection->input, 0, sizeof(Binder) * MAX_ARGS);
        memset(connection->lineup, 0, sizeof(char *) * MAX_ARGS);
        memset(connection->targs, 0, sizeof(Oid) * MAX_ARGS);
        connection->nargs = 0;

        connection->cursor = -1;
        connection->processed = -1;
    
        RELEASE(connection);

        return err;
}


extern long 
WOutputLinkInd(OpaqueWConn conn, short pos, void *varAdd, int varSize, int varType, short *ind, int *clength)
{
    WConn          connection = SETUP(conn);
    long           err;

    if ( !pthread_equal(connection->transaction_owner,pthread_self()) ) {
        char    msg[256];
        err = 454;
        snprintf(msg,255,"transaction is owned by thread %d, cannot make call from this context",connection->transaction_owner);
        SetError(connection,err,"CONTEXT",msg);
        return err;
    }

    READY(connection,err);

    if ( CheckForCancel() ) {
        elog(ERROR,"Query Cancelled");
    }
            
    if (pos > MAX_ARGS || pos <= 0) {
        coded_elog(ERROR,101,"bad value - index must be greater than 0 and less than %d", MAX_ARGS);
    } else {
        connection->output[pos - 1].index = pos;
        connection->output[pos - 1].target = varAdd;
        connection->output[pos - 1].size = varSize;
        connection->output[pos - 1].type = varType;
        connection->output[pos - 1].notnull = ind;
        connection->output[pos - 1].length = clength;
    }

    RELEASE(connection);
    
    return err;
}

extern long 
WExec(OpaqueWConn conn)
{
    WConn          connection = SETUP(conn);
    long            err;
    
    List           *trackquery = NULL;
    List           *trackplan = NULL;
    Plan           *plantree = NULL;
    Query          *querytree = NULL;

    if ( !pthread_equal(connection->transaction_owner,pthread_self()) ) {
        char    msg[256];
        err = 454;
        snprintf(msg,255,"transaction is owned by thread %d, cannot make call from this context",connection->transaction_owner);
        SetError(connection,err,"CONTEXT",msg);
        return err;
    }

    READY(connection,err);

    PreparedPlan   *plan = NULL;

    if ( CheckForCancel() ) {
        elog(ERROR,"Query Cancelled");
    }
    if (connection->stage == STMT_INVALID) {
            elog(ERROR, "no statement parsed");
    }
    if (connection->plan == NULL) {
            connection->plan = PreparePlan(connection->statement, connection->targs, connection->lineup, connection->nargs);
    }
    plan = connection->plan;

    trackquery = plan->querytreelist;
    trackplan = plan->plantreelist;

    while (trackquery) {
        /*
         * Increment Command Counter so we see everything
         * that happend in this transaction to here
         */
        CommandCounterIncrement();

        querytree = (Query *) lfirst(trackquery);
        trackquery = lnext(trackquery);

        plantree = (Plan *) lfirst(trackplan);
        trackplan = lnext(trackplan);

        SetQuerySnapshot();

        WResetExecutor(connection->plan);

        if (querytree->commandType == CMD_UTILITY) {
            ProcessUtility(querytree->utilityStmt, None);
            /*
             * increment after any utility if there are
             * more subqueiries to execute
             */
        } else {
            plan->state = CreateExecutorState();

            if (connection->nargs > 0) {
                    FillExecArgs(connection);
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
                int             delete_count = 0;

                do {
                    slot = ExecProcNode(plan->qdesc->plantree);
                    if (TupIsNull(slot))
                            break;

                    tuple_ctid = slot->val->t_self;

                    switch (plan->qdesc->operation) {
                        case CMD_INSERT:
                                slot->val->t_data->t_oid = GetGenId();
                                ExecAppend(slot, NULL, plan->state);
                                break;
                        case CMD_DELETE:
                                ExecDelete(slot, &tuple_ctid, plan->state);
                                delete_count++;
                                break;
                        case CMD_UPDATE:
                                ExecReplace(slot, &tuple_ctid, plan->state);
                                delete_count++;
                                break;
                        default:
                                elog(DEBUG, "ExecutePlan: unknown operation in queryDesc");
                                break;
                    }
                } while (true);

                if ( CheckForCancel() ) {
                    elog(ERROR,"Query Cancelled");
                }
            }
        }
    }
    connection->stage = STMT_EXEC;
    RELEASE(connection);
    return err;
}

extern long 
WFetch(OpaqueWConn conn)
{
    WConn          connection = SETUP(conn);
    long            err;
    
    if ( !pthread_equal(connection->transaction_owner,pthread_self()) ) {
        char    msg[256];
        err = 454;
        snprintf(msg,255,"transaction is owned by thread %d, cannot make call from this context",connection->transaction_owner);
        SetError(connection,err,"CONTEXT",msg);
        return err;
    }

    PreparedPlan   *plan = connection->plan;
    
    READY(connection,err);

    int             pos = 0;

    long            check;
    unsigned long   rows;
    unsigned short  stats;

    if ( CheckForCancel() ) {
        elog(ERROR,"Query Cancelled");
    }
    if (connection->stage != STMT_EXEC && connection->stage != STMT_FETCH) {
        elog(ERROR, "no statement executed");
    }
    if ( connection->stage == STMT_EOD ) {
        coded_elog(ERROR,1405,"end of data already reached");
    }
    MemoryContext   parent = MemoryContextGetCurrentContext();
    if ( plan->fetch_cxt == NULL ) {
        plan->fetch_cxt = AllocSetContextCreate(parent,
            "FetchContext",
            ALLOCSET_DEFAULT_MINSIZE,
            (32 * 1024),
            ALLOCSET_DEFAULT_MAXSIZE);
    }
    MemoryContextSwitchTo(plan->fetch_cxt);

    TupleTableSlot *slot = ExecProcNode(plan->qdesc->plantree);
    
    if (TupIsNull(slot)) {
        err = 4;  /*  EOT ( End of Transmission ascii code */
        connection->stage = STMT_EOD;
    } else {
        HeapTuple       tuple = slot->val;
        TupleDesc       tdesc = slot->ttc_tupleDescriptor;

        while (connection->output[pos].index != 0 && pos <= MAX_ARGS) {
            Datum       val = (Datum) NULL;
            char            isnull = 0;
            
            if (tuple->t_data->t_natts < connection->output[pos].index || connection->output[pos].index <= 0) {
                coded_elog(ERROR,104,"no attribute");
            }
            if (tuple->t_data->t_natts < pos || pos < 0) {
                coded_elog(ERROR,107,"wrong number of attributes");
            }

            val = HeapGetAttr(tuple, connection->output[pos].index, tdesc, &isnull);

            if (!isnull) {
                if ( !TransferValue(&connection->output[pos],tdesc->attrs[pos],val) ) {
            /* field was not transfered, try and coerce to see if it should someday  */
                    if (can_coerce_type(1, &tdesc->attrs[pos]->atttypid, &connection->output[pos].type)) {
                        coded_elog(ERROR,105,"Types are compatible but conversion not implemented link type: %d result type: %d",
                                 connection->output[pos].type, tdesc->attrs[pos]->atttypid);
                        break;
                    } else {
                        coded_elog(ERROR,106,"Types do not match, no type conversion . position: %d type: %d result type: %d",
                                pos + 1, connection->output[pos].type, tdesc->attrs[pos]->atttypid);
                        break;
                    }
                }
                *connection->output[pos].notnull = 1;  /*  the value is not null */
            } else {
                *connection->output[pos].notnull = 0;  /*  the value is null */
            }
            pos++;
        }
        ExecClearTuple(slot);
        plan->state->es_processed++;
        connection->stage = STMT_FETCH;
    }
                
    MemoryContextSwitchTo(parent);
#ifdef MEMORY_CONTEXT_CHECKING
    fprintf(stderr, "memory at fetch: %d\n", MemoryContextStats(plan->fetch_cxt));
#endif
    MemoryContextResetAndDeleteChildren(plan->fetch_cxt);

    RELEASE(connection);
    return err;
}

extern long
WFetchIsComplete(OpaqueWConn conn) {
	WConn connection = SETUP(conn);
	if ( connection->stage == STMT_EOD ) return TRUE;
	else return FALSE;
}

extern long 
WPrepare(OpaqueWConn conn)
{
        WConn          connection = SETUP(conn);
        long            err = 0;
        if ( !pthread_equal(connection->transaction_owner,pthread_self()) ) {
            char    msg[256];
            err = 454;
            snprintf(msg,255,"transaction is owned by thread %d, cannot make call from this context",connection->transaction_owner);
            SetError(connection,err,"CONTEXT",msg);
            return err;
        }
        READY(connection,err);
        if ( CheckForCancel() ) {
            elog(ERROR,"Query Cancelled");
        }
        RELEASE(connection);
        return err;
}

extern long 
WCommit(OpaqueWConn conn)
{
        WConn          connection = SETUP(conn);
        long           err;
        if ( !pthread_equal(connection->transaction_owner,pthread_self()) ) {
            char    msg[256];
            err = 454;
            snprintf(msg,255,"transaction is owned by thread %d, cannot make call from this context",connection->transaction_owner);
            SetError(connection,err,"CONTEXT",msg);
            return err;
        }
        READY(connection,err);

        if ( connection->stage == STMT_INVALID ) {
            elog(ERROR,"connection is currently in an invalid state for commit");
        }
        if ( CheckForCancel() ) {
            elog(ERROR,"Query Cancelled");
        }

        connection->stage = STMT_COMMIT;
        /* clean up executor   */
        if (connection->abortonly) {
            if (CurrentXactInProgress()) {
                WResetQuery(connection);
                AbortTransaction();
            }
            elog(NOTICE, "transaction in abort only mode");
        } else {
            WResetQuery(connection);
            if ( connection->parent == NULL ) {
                CommitTransaction();
            } else {
                CloseSubTransaction();
            }
        }
        
        RELEASE(connection);

        connection->stage = STMT_INVALID;
        connection->plan = NULL;
        connection->transaction_owner = 0;
        
        return err;
}


extern long 
WRollback(OpaqueWConn conn)
{
    WConn          connection = SETUP(conn);
    long            err;

    if ( !pthread_equal(connection->transaction_owner,pthread_self()) ) {
        char    msg[256];
        err = 454;
        snprintf(msg,255,"transaction is owned by thread %d, cannot make call from this context",connection->transaction_owner);
        SetError(connection,err,"CONTEXT",msg);
        return err;
    }

    READY(connection,err);

    if ( connection->stage == STMT_INVALID ) {
        elog(ERROR,"connection is currently in an invalid state for commit");
    }

    if ( CurrentXactInProgress() ) {
        WResetQuery(connection);
        if ( connection->parent == NULL ) {
            AbortTransaction();
        } else {
            CloseSubTransaction();
        }
    }
    connection->stage = STMT_ABORT;

    RELEASE(connection);
    
    connection->transaction_owner = 0;
    connection->stage = STMT_INVALID;
    connection->plan = NULL;

    return err;
}

extern long 
WBindWithIndicate(OpaqueWConn conn, const char *var, void *varAdd, int varSize, short *indAdd, int varType, int cType)
{
    WConn          connection = SETUP(conn);
    long            err = 0;

    if ( !pthread_equal(connection->transaction_owner,pthread_self()) ) {
        char    msg[256];
        err = 454;
        snprintf(msg,255,"transaction is owned by thread %d, cannot make call from this context",connection->transaction_owner);
        SetError(connection,err,"CONTEXT",msg);
        return err;
    }

    READY(connection,err);
    
/*  remove the marker flag of the named parameter if there is one */
    switch (*var) {
        case '$':
        case '?':
        case ':':
            var++;
    }
            
    int  index = 0;
    if ( CheckForCancel() ) {
        elog(ERROR,"Query Cancelled");
    }
    /* find the right binder */
    for (index = 0; index < MAX_ARGS; index++) {
            if (connection->input[index].index == 0 || strcmp(var, connection->input[index].name) == 0)
                    break;
    }

    if (index >= MAX_ARGS) {
            coded_elog(ERROR,105, "too many bind values, max is %d", MAX_ARGS);
    }
    connection->input[index].index = index + 1;
    strncpy(connection->input[index].name, var, 64);
    connection->input[index].varSize = varSize;
    connection->input[index].type = varType;
    connection->input[index].ctype = cType;
    connection->input[index].target = varAdd;
    connection->input[index].isNotNull = indAdd;

    connection->lineup[index] = connection->input[index].name;
    connection->targs[index] = connection->input[index].type;

    if (index + 1 > connection->nargs)
            connection->nargs = index + 1;
    
    RELEASE(connection);
    
    return err;
}


extern long 
WCancel(OpaqueWConn conn)
{
    WConn          connection = (WConn) conn;
    int             sqlError = 0;
    
    connection->env->cancelled = true;

    return sqlError;
}

extern long
WCancelAndJoin(OpaqueWConn conn) {
    WConn          connection = (WConn) conn;
    int             sqlError = 0;

    CancelEnvAndJoin(connection->env);

    return sqlError;
}

static long 
WDisposeConnection(OpaqueWConn conn)
{
    WConn          connection = (WConn) conn;
    int             sqlError = 0;

    if (connection->env == NULL)
            return sqlError;
    SetEnv(connection->env);
    if (setjmp(connection->env->errorContext) == 0) {
        if (connection->validFlag == 1 && CurrentXactInProgress()) {
            WResetQuery(connection);
            if ( connection->parent != NULL ) {
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
        if ( BufferPoolCheckLeak() ) {
            elog(NOTICE,"Buffer leak in dispose connection");
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
            MemoryContextDestroyEnv();
    if (setjmp(connection->env->errorContext) == 0)
            DestroyThread();

    connection->validFlag = -1;
    SetEnv(NULL);
    return sqlError;
}

extern long 
WGetTransactionId(OpaqueWConn conn)
{
    WConn          connection = SETUP(conn);
    long            err = 0;
    long            xid = -1;

    if ( !pthread_equal(connection->transaction_owner,pthread_self()) ) {
        char    msg[256];
        err = 454;
        snprintf(msg,255,"transaction is owned by thread %d, cannot make call from this context",connection->transaction_owner);
        SetError(connection,err,"CONTEXT",msg);
        return err;
    }

    READY(connection,err);

    if ( CheckForCancel() ) {
        elog(ERROR,"Query Cancelled");
    }
    if (connection->stage == STMT_INVALID) {
        elog(ERROR, "transaction not begun");
    }
    
    xid = GetCurrentTransactionId();
    
    RELEASE(connection);
    
    return xid;
}

extern long 
WGetCommandId(OpaqueWConn conn)
{
    WConn          connection = SETUP(conn);
    long            err = 0;
    long            cid = -1;
    if ( !pthread_equal(connection->transaction_owner,pthread_self()) ) {
        char    msg[256];
        err = 454;
        snprintf(msg,255,"transaction is owned by thread %d, cannot make call from this context",connection->transaction_owner);
        SetError(connection,err,"CONTEXT",msg);
        return err;
    }
    READY(connection,err);

    if ( CheckForCancel() ) {
        elog(ERROR,"Query Cancelled");
    }
    if (connection->stage == STMT_INVALID) {
        elog(ERROR, "transaction not begun");
    }
    
    cid = GetCurrentCommandId();
    
    RELEASE(connection);
    
    return cid;
}

extern long 
WBeginProcedure(OpaqueWConn conn)
{
    WConn          connection = SETUP(conn);
    long           err = 0;
    if ( !pthread_equal(connection->transaction_owner,pthread_self()) ) {
        char    msg[256];
        err = 454;
        snprintf(msg,255,"transaction is owned by thread %d, cannot make call from this context",connection->transaction_owner);
        SetError(connection,err,"CONTEXT",msg);
        return err;
    }
    READY(connection,err);

    if ( CheckForCancel() ) {
        elog(ERROR,"Query Cancelled");
    }
    if (connection->stage == STMT_INVALID) {
        elog(ERROR, "transaction not begun");
    }
    
    TakeUserSnapshot();

    RELEASE(connection);
    
    return err;
}


extern long 
WEndProcedure(OpaqueWConn conn)
{
    WConn          connection = SETUP(conn);
    long            err = 0;
    if ( !pthread_equal(connection->transaction_owner,pthread_self()) ) {
        char    msg[256];
        err = 454;
        snprintf(msg,255,"transaction is owned by thread %d, cannot make call from this context",connection->transaction_owner);
        SetError(connection,err,"CONTEXT",msg);
        return err;
    }
    READY(connection,err);

    if ( CheckForCancel() ) {
        elog(ERROR,"Query Cancelled");
    }
    if (connection->stage == STMT_INVALID) {
        elog(ERROR, "transaction not begun");
    }
    
    DropUserSnapshot();

    RELEASE(connection);
    
    return err;
}

extern long 
WUserLock(OpaqueWConn conn, const char *group, uint32_t val, char lockit)
{
    WConn          connection = SETUP(conn);
    Oid             grouplockid = (Oid) - 3;
    long            err = 0;
    
    READY(connection,err);

    char           *trax;
    char            gname[256];
    Relation        rel;

    if ( CheckForCancel() ) {
        elog(ERROR,"query cancelled");
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
            coded_elog(ERROR,502,"user unlock failed -- no relation");
    }

    int lockstate   = 0;
    LOCKTAG         tag;
    memset(&tag, 0, sizeof(tag));

    tag.relId = grouplockid;
    tag.dbId = GetDatabaseId();
    tag.objId.blkno = (BlockNumber) val;
    if (lockit) {
        TransactionId   xid = 0;
        if (LockAcquire(USER_LOCKMETHOD, &tag, xid, ExclusiveLock, true)) {
            lockstate = 0;
        } else {
            lockstate = 1;
        }
        elog(DEBUG, "user lock on group:%s item:%d result:%d", gname, val, lockstate);
    } else {
        TransactionId   xid = 0;

        if (LockRelease(USER_LOCKMETHOD, &tag, xid, ExclusiveLock)) {
            lockstate = 0;
        } else {
            strncpy(connection->env->state, "USER", 39);
            coded_elog(ERROR,501,"user unlock failed");
        }
        elog(DEBUG, "user unlock on group:%s item:%d result:%d", gname, val, lockstate);
    }

    RELEASE(connection);
    
    return err;
}

extern long 
WIsValidConnection(OpaqueWConn conn)
{
    WConn          connection = (WConn) conn;
    if (connection->validFlag > 0) {
        return 1;
    }
    return 0;
}

extern long 
WGetErrorCode(OpaqueWConn conn)
{
    WConn          connection = (WConn) conn;
    if (connection == NULL)
            return -99;
    /*  int to long conversion */
    return (long)connection->CDA.rc;
}

extern const char *
WGetErrorText(OpaqueWConn conn)
{
    WConn          connection = (WConn) conn;
    if (connection == NULL)
            return "no connection";
    return connection->CDA.text;
}

extern const char *
WGetErrorState(OpaqueWConn conn)
{
    WConn          connection = (WConn) conn;
    if (connection == NULL)
            return "DISCONNECTED";
    return connection->CDA.state;
}

extern void
WConnectStdIO(OpaqueWConn conn, void* args, pipefunc in, pipefunc out) {
    WConn          connection = SETUP(conn);
    long           err;
    MemoryContext    cxt;
    
    READY(connection,err);
    cxt = MemoryContextSwitchTo(GetEnvMemoryContext());
    ConnectIO(args,in,out);
    MemoryContextSwitchTo(cxt);
    RELEASE(connection);
}

extern void*
WDisconnectStdIO(OpaqueWConn conn) {
    WConn          connection = SETUP(conn);
    long           err;
    void*          args;

    READY(connection,err);
    args = DisconnectIO();
    RELEASE(connection);

    return args;
}

extern long 
WStreamExec(OpaqueWConn conn, char *statement)
{
    WConn          connection = SETUP(conn);
    long            err;
    bool            IsEmptyQuery = false;
    
    SetEnv(connection->env);
    
    err = setjmp(connection->env->errorContext);
    if (err != 0) {
        /*  cannot use READY/RELEASE b/c semantics for streamed connections is different */
        SetAbortOnly();
        CommitTransactionCommand();
        WHandleError(connection,err);
    } else {
        connection->CDA.rc = 0;
            
        if ( CheckForCancel() ) {
            elog(ERROR,"query cancelled");
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
    connection->stage = STMT_INVALID;
    SetEnv(NULL);
    return err;
}

extern Pipe WPipeConnect(OpaqueWConn conn, void* args, pipefunc func)
{
    WConn connection = SETUP(conn);
    MemoryContext cxt;
    long err;
    Pipe pipe = NULL;
   
    READY(connection,err);
    cxt = MemoryContextSwitchTo(GetEnvMemoryContext());
    pipe = ConnectCommBuffer(args,func);
    MemoryContextSwitchTo(cxt);
    RELEASE(connection);
        
    return pipe;
}

extern void* 
WPipeDisconnect(OpaqueWConn conn, Pipe pipe)
{
    WConn          connection = SETUP(conn);
    void*          userargs;
    long            err = 0;
    
    READY(connection,err);
        
    userargs = DisconnectCommBuffer(pipe);
    
    RELEASE(connection);
    return userargs;
}

extern int 
WPipeSize(OpaqueWConn conn)
{
    return sizeof(CommBuffer);
}

void
SetError(WConn connection,int sqlError,char* state,char* err) {
    connection->CDA.rc = sqlError;

    if (sqlError) {
        strncpy(connection->CDA.text, err, 255);
        strncpy(connection->CDA.state, state, 39);
    }
}

void 
WHandleError(WConn connection, int sqlError)
{
    if ( connection == NULL ) {
        return;
    }

    if ( connection->env == NULL ) {
        return;
    }

    memset(connection->CDA.state, '\0', 40);
    memset(connection->CDA.text, '\0', 256);

    SetError(connection,sqlError,connection->env->state,connection->env->errortext);
    clearerror(connection->env);
}

void 
WResetQuery(WConn connection)
{
    int size = 0;
/*  if we are in abort don't worry about shutting down,
abort cleanup will take care of it.  */
    if (connection->plan != NULL )
        WResetExecutor(connection->plan);

    MemoryContextSwitchTo(MemoryContextGetEnv()->QueryContext);
#ifdef MEMORY_CONTEXT_CHECKING
    size = MemoryContextStats(MemoryContextGetEnv()->QueryContext);
    fprintf(stderr, "memory at query: %d\n", size);
#endif
    MemoryContextResetAndDeleteChildren(MemoryContextGetEnv()->QueryContext);
    memset(connection->output, 0, sizeof(Output) * MAX_ARGS);
    memset(connection->statement, '\0', 8192);

    connection->plan = NULL;
}

void 
WResetExecutor(PreparedPlan * plan)
{
    if (plan->qdesc != NULL) {
        ExecutorEnd(plan->qdesc, plan->state);
    }

    MemoryContextSwitchTo(plan->exec_cxt);
#ifdef MEMORY_CONTEXT_CHECKING
    fprintf(stderr, "memory at exec: %d\n", MemoryContextStats(plan->exec_cxt));
#endif
    MemoryContextResetAndDeleteChildren(plan->exec_cxt);

    plan->qdesc = NULL;
    plan->state = NULL;
    plan->fetch_cxt = NULL;
}

/* create copies in case underlying buffer goes away  */
static int 
FillExecArgs(WConn connection)
{
    int             k = 0;
    PreparedPlan   *plan = connection->plan;
    MemoryContext   old = NULL;
    ParamListInfo   paramLI;

    Assert(plan != NULL);

    plan->bindcontext = SubSetContextCreate(MemoryContextGetCurrentContext(),"StatementArgumentContext");
    old = MemoryContextSwitchTo(plan->bindcontext);

    paramLI = (ParamListInfo) palloc((connection->nargs + 1) * sizeof(ParamListInfoData));

    plan->state->es_param_list_info = paramLI;
    for (k = 0; k < connection->nargs; k++) {
        paramLI->kind = PARAM_NAMED;
        paramLI->name = connection->input[k].name;
        paramLI->id = connection->input[k].index;
        paramLI->type = connection->input[k].type;
        paramLI->isnull = !(*connection->input[k].isNotNull);
        
        if ( *connection->input[k].isNotNull < 0 ) {
            elog(ERROR,"bound variable %s has not been set",paramLI->name);
        }
        if ( paramLI->isnull ) {
                paramLI->length = 0;
        } else switch (connection->input[k].ctype) {
            case CHAROID:
                paramLI->length = 1;
                paramLI->value = *(char *) connection->input[k].target;
                break;
            case BOOLOID:
                paramLI->length = 1;
                paramLI->value = *(char *) connection->input[k].target;
                break;
            case INT4OID:
                paramLI->length = 4;
                paramLI->value = *(int *) connection->input[k].target;
                break;
            case TIMESTAMPOID:
                paramLI->length = 8;
                paramLI->value = (Datum) palloc(connection->input[k].varSize);
                memcpy((char *) paramLI->value, connection->input[k].target, connection->input[k].varSize);
                break;
             case FLOAT8OID:
                paramLI->length = 8;
                paramLI->value = (Datum) palloc(connection->input[k].varSize);
                memcpy((char *) paramLI->value, connection->input[k].target, connection->input[k].varSize);
                break;
            case INT8OID:
                paramLI->length = 8;
                paramLI->value = (Datum) palloc(connection->input[k].varSize);
                memcpy((char *) paramLI->value, connection->input[k].target, connection->input[k].varSize);
                break;      
            case STREAMINGOID:
                paramLI->length = sizeof(CommBuffer);/* -1 == variable size slot */ 
                paramLI->value = (Datum) palloc(connection->input[k].varSize);
                memcpy((char *) paramLI->value, connection->input[k].target, connection->input[k].varSize);
                break;
            case VARCHAROID:
            case TEXTOID:
            case BLOBOID:
            case JAVAOID:
            default:
                if (*(int *) connection->input[k].target > connection->input[k].varSize) {
                        strncpy(connection->env->errortext, "binary truncation on input", 255);
                        longjmp(connection->env->errorContext, 103);
                }
                /*
                 * need to create data copies here b/c the buffer
                 * holding data may go away after exec
                 */
                /*
                 * paramLI->value =
                 * (Datum)connection->input[k].target;
                 */
                paramLI->value = (Datum) palloc(connection->input[k].varSize + 4);
                memcpy((char *) paramLI->value, connection->input[k].target, connection->input[k].varSize + 4);
                paramLI->length = (Size)(-1);/* -1 == variable size slot */ /* (*(int*)input[k].targe
                                                 * t) + 4;  */
                break;
        }
        paramLI++;

    }
    paramLI->kind = PARAM_INVALID;
    MemoryContextSwitchTo(old);
}

/* create copies in case underlying buffer goes away  */
static int 
FreeExecArgs(PreparedPlan * plan)
{
    if (plan->bindcontext) {
        MemoryContextDelete(plan->bindcontext);
        plan->bindcontext = NULL;
    } else {
        printf("argument memory context error\n");
        return 8;
    }
    return 0;
}

static PreparedPlan *
PreparePlan(char *statement, Oid * targs, char **lineup, int nargs)
{
    List           *querytree_list = NULL;
    List           *plantree_list = NULL;
    Plan           *plantree = NULL;
    Query          *querytree = NULL;


    PreparedPlan   *plan = (PreparedPlan *) palloc(sizeof(PreparedPlan));

    /* parse out a new query and setup plan  */
    /* init for set type */

    querytree_list = pg_parse_and_rewrite(statement, targs, lineup, nargs, FALSE);
    if (!querytree_list) {
            elog(ERROR, "parsing error");
    }
    /*
     * should only be calling one statement at a time if not, you need to
     * do a foreach on the querytree_list to get a plan for each query
     */

    plan->querytreelist = querytree_list;

    querytree = lfirst(querytree_list);
    while (querytree_list) {
        plantree = pg_plan_query(querytree);
        plantree_list = lappend(plantree_list, plantree);
        querytree_list = lnext(querytree_list);
    }

    plan->plantreelist = plantree_list;
    /* set the bind context to NULL until a memory context is created  */

    plan->tupdesc = NULL;
    plan->state = NULL;
    plan->qdesc = NULL;

    plan->exec_cxt = AllocSetContextCreate(MemoryContextGetCurrentContext(),
                                           "ExecutorContext",
                                           ALLOCSET_DEFAULT_MINSIZE,
                                           ALLOCSET_DEFAULT_INITSIZE,
                                           ALLOCSET_DEFAULT_MAXSIZE);
    plan->bindcontext = NULL;
    plan->fetch_cxt = NULL;

    return plan;
}

