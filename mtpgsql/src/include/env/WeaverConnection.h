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

#define MAX_ARGS 16
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
	char	name[64];
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

    char		password[64];
    char 		name[64];
    char 		connect[64];

    Stage		stage;

/*   Query Stuff   */		
    OpaquePreparedStatement	plan;
/* private */
    Env*                            env;
    
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

       MemoryContext   created_cxt;
       MemoryContext   bind_cxt;
       MemoryContext   exec_cxt;
       MemoryContext   fetch_cxt;

        TupleDesc	tupdesc;
        EState*		state;
        QueryDesc*	qdesc;
        int             processed;

        int            input_count;
    Binder		input[MAX_ARGS];
    Output		output[MAX_ARGS];

    OpaquePreparedStatement   next;
} PreparedPlan;

#ifdef __cplusplus
extern "C" {
#endif
void WHandleError( WConn conn,int sqlError );
void  WResetExecutor(PreparedPlan* plan);
void WResetQuery(WConn conn);

bool
TransferValue(Output* output, Form_pg_attribute desc, Datum value);
#ifdef __cplusplus
}
#endif

#endif
