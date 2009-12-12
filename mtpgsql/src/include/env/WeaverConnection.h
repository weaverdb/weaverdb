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

typedef struct preparedplan {
        List*		querytreelist;
        List*		plantreelist;

       MemoryContext   bindcontext;
       MemoryContext   exec_cxt;
       MemoryContext   fetch_cxt;

        TupleDesc	tupdesc;
        EState*		state;
        QueryDesc*	qdesc;
} PreparedPlan;

typedef struct output {
	short index;
	void* target;
	int	 size;
	Oid	 type;
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
	STMT_NEW,
	STMT_PARSED,
	STMT_EXEC,
	STMT_BOUND,
	STMT_LINKED,
	STMT_SELECT,
	STMT_FETCH,
        STMT_EOD,
        STMT_COMMIT,
        STMT_ABORT,
	STMT_INVALID
} Stage;

typedef struct Connection {
    double              align;
    Error		CDA;	
    short		validFlag;

    char		password[256];
    char 		name[256];
    char 		connect[256];

    char		statement[8192];
    Stage		stage;

    Binder		input[MAX_ARGS];
    char*		lineup[MAX_ARGS];
    Oid                 targs[MAX_ARGS];
    int                 nargs;

    short		openCursor;
    short		indie;

/*   Query Stuff   */		
    PreparedPlan*	plan;
    Output		output[MAX_ARGS];
/* private */
    Env*                            env;
    
    struct Connection*              parent;             
    int                             child_count;
    int                             child_trans;

    int 		processed;
    int                 abortonly;
    
    pthread_t                       transaction_owner;
    pthread_mutex_t                 child_lock;

} * WConn;

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
