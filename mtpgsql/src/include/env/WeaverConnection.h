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

#define START_ARGS 4
#define MAX_ARGS 32
#define MAX_CHILDREN 4

#include <stdlib.h>  
#include <unistd.h>
#include <pthread.h>

#include "env/env.h"
#include "env/WeaverInterface.h"
#include "catalog/pg_attribute.h"
#include "nodes/execnodes.h"
#include "executor/executor.h"
#include "utils/palloc.h"
#include "utils/portal.h"

typedef struct output {
	short   index;
	void*   target;
	int	 size;
	Oid	 type;
        void*    freeable;
	short*  notnull;
	int*	length;
} Output;

typedef struct input {
	short 	index;
	char*	name;
	int 	varSize;
	Oid 	type;
	int 	ctype;
	short*  isNotNull;
	void*	target;
} Binder;


typedef enum stage {
	TRAN_BEGIN,
        STMT_NEW,
	STMT_PARSED,
	STMT_EXEC,
	STMT_FETCH,
        STMT_EOD,
        TRAN_COMMIT,
        TRAN_ABORT,
        TRAN_ABORTONLY,
	TRAN_INVALID
} Stage;

typedef struct Connection {
    double              align;
    Error		CDA;	
    short		validFlag;

    char*		password;
    char* 		name;
    char* 		connect;

    Stage		stage;

/*   Query Stuff   */		
    OpaquePreparedStatement	plan;
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
    Stage               stage;
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

        short            input_count;
        short            input_slots;
        short            output_slots;
        
    Binder*		input;
    Output*		output;

    OpaquePreparedStatement   next;
} PreparedPlan;

#ifdef __cplusplus
extern "C" {
#endif
void WHandleError( WConn conn,int sqlError );
void  WResetExecutor(PreparedPlan* plan);
void WResetQuery(WConn conn,bool err);

bool
TransferValue(Output* output, Form_pg_attribute desc, Datum value);
#ifdef __cplusplus
}
#endif

#endif
