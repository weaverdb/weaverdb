/*-------------------------------------------------------------------------
 *
 *	WeaverConnection.h 
 *		Lowest level of Weaver interface
 *
 *
 * IDENTIFICATION
*		Myron Scott, mkscott@sacadia.com, 2.05.2001 
 *
 *-------------------------------------------------------------------------
 */



#ifndef _WEAVER_CONNECTION_H_
#define _WEAVER_CONNECTION_H_
#define HAVE_UNION_SEMUN

#define START_ARGS 8
#define MAX_ARGS 64
#define MAX_CHILDREN 4

#include "env/env.h"
#include "interface/WeaverInterface.h"
#include "catalog/pg_attribute.h"
#include "nodes/execnodes.h"
#include "executor/executor.h"
#include "utils/palloc.h"
#include "utils/portal.h"

typedef enum ttype {
    TFREE,
    TINPUT,
    TOUTPUT
} TransferType;

typedef struct inout {
    TransferType transferType;
    int     index;
    char*   name;
    int     varType;
    void*   userargs;
    transferfunc  transfer;
} InputOutput;

typedef enum t_stage {
	TRAN_BEGIN,
        TRAN_COMMIT,
        TRAN_ABORT,
        TRAN_ABORTONLY,
	TRAN_INVALID
} TransactionStage;

typedef enum s_stage {
        STMT_NEW,
	STMT_PARSED,
	STMT_EXEC,
	STMT_FETCH,
        STMT_EOD,
        STMT_EMPTY,
        STMT_ABORT
} StatementStage;

typedef struct Connection {
    double              align;
    Error		CDA;	
    short		validFlag;

    char*		password;
    char* 		name;
    char* 		connect;

    TransactionStage		stage;

/*   Query Stuff   */		
    OpaquePreparedStatement	plan;
    OpaquePreparedStatement	inselect;
/* private */
    Env*                            env;
    MemoryContext                   memory;
    
    struct Connection*              parent;             
    int                             child_count;
    int                             child_trans;
    
    pthread_t                       transaction_owner;
    pthread_mutex_t                 child_lock;

} * WConn;

typedef struct preparedplan {
    WConn               owner;
    char*               statement;
    StatementStage               stage;
        List*		querytreelist;
        List*		plantreelist;

       MemoryContext   plan_cxt;
       MemoryContext   node_cxt;

       MemoryContext   exec_cxt;
       MemoryContext   fetch_cxt;

        TupleDesc	tupdesc;
        EState*		state;
        QueryDesc*	qdesc;
        int             processed;

        short           slots;

    InputOutput*        slot;

    OpaquePreparedStatement   next;
} PreparedPlan;

#ifdef __cplusplus
extern "C" {
#endif
LIB_EXTERN void WHandleError( WConn conn,int sqlError );
LIB_EXTERN void  WResetExecutor(PreparedPlan* plan);
LIB_EXTERN void WResetQuery(WConn conn,bool err);

LIB_EXTERN bool
TransferToRegistered(InputOutput* output, Form_pg_attribute desc, Datum value, bool isnull);
LIB_EXTERN bool
TransferColumnName(InputOutput* output, Form_pg_attribute desc);
#ifdef __cplusplus
}
#endif

#endif
