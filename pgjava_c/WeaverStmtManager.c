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

#include "WeaverStmtManager.h"
#include "env/WeaverInterface.h"  

struct bound {
	long			pointer;
        int                     maxlength;
        void*                   userspace;
        short                   byval;
        short			indicator;
	short			type;
};

typedef struct outputObj {
        struct bound            base;
	long			index;
	int			clength;
} outputDef;


typedef struct bindObj {
        struct bound            base;
	char			binder[64];
} inputDef;

typedef struct  WeaverStmtManager {
        long				transactionId;	
        long				commandId;	
        void*				dataStack;
        long				stackSize;
        long				blob_size;

        Error				errordelegate;
        long				errorlevel;	

        OpaqueWConn 			theConn;  
        int                             refcount;
        OpaquePreparedStatement         statement;  

        long				holdingArea;

        inputDef*                           inputLog;
        outputDef*                          outputLog;
        
        short                           log_count;

} WeaverStmtManager;

static Input  GetBind(StmtMgr mgr, const char * vari, short type);
static Input  AddBind(StmtMgr mgr, const char * vari, short type);
static void Clean( StmtMgr mgr,usercleanup input,usercleanup output);
static long  align(StmtMgr mgr, long pointer);
static void* Advance(StmtMgr mgr,long size);
static short ExpandBindings(StmtMgr mgr);

OpaqueWConn
CreateWeaverConnection( const char* name, const char * paslong, const char* connect) {
    OpaqueWConn conn = WCreateConnection(name,paslong,connect);
    if ( !WIsValidConnection(conn) ) {
        WDestroyConnection(conn);
        return NULL;
    }
    return conn;
}

StmtMgr
CreateWeaverStmtManager(OpaqueWConn connection)
{
    int counter = 0;

    if ( !WIsValidConnection(connection) ) return NULL;
    
    StmtMgr mgr = (StmtMgr)WAllocConnectionMemory(connection,sizeof(WeaverStmtManager));
    
    mgr->theConn = connection;
    mgr->refcount = 1;

    mgr->statement = NULL;

    mgr->holdingArea = 0;

    mgr->blob_size = BLOBSIZE;
    mgr->stackSize = mgr->blob_size * 2;
    mgr->dataStack = WAllocConnectionMemory(connection,mgr->stackSize);
    mgr->transactionId = 0;

    memset(&mgr->errordelegate,0x00,sizeof(Error));

    mgr->log_count = MAX_FIELDS;
    if ( mgr->log_count > 0 ) {
        mgr->inputLog = WAllocConnectionMemory(connection,sizeof(inputDef) * mgr->log_count);
        mgr->outputLog = WAllocConnectionMemory(connection,sizeof(outputDef) * mgr->log_count);
    /*  zero statement structures */
       for ( counter = 0;counter < mgr->log_count;counter++ ) {
            memset(&mgr->outputLog[counter],0,sizeof(outputDef));
            mgr->outputLog[counter].base.indicator = -1;
            mgr->outputLog[counter].base.byval = 0;
            mgr->outputLog[counter].base.userspace = NULL;
            memset(&mgr->inputLog[counter],0,sizeof(inputDef));
            mgr->inputLog[counter].base.indicator = -1;
            mgr->inputLog[counter].base.byval = 0;
            mgr->inputLog[counter].base.userspace = NULL;
        }
    } else {
        mgr->inputLog = NULL;
        mgr->outputLog = NULL;
    }
    return mgr;
} 

void DestroyWeaverConnection( StmtMgr mgr ) {
    if ( mgr == NULL ) return;
    if ( mgr->theConn != NULL ) {
        /*  it's possible the the owning thread is not the
         *  destroying thread so to a cancel/join for
         *  safety's sake.
         */
        WCancelAndJoin(mgr->theConn);
        WDestroyConnection(mgr->theConn);
        mgr->theConn = NULL;
    }
}

OpaqueWConn GetWeaverConnection( StmtMgr mgr )
{
    if ( mgr == NULL ) return NULL;
    mgr->refcount += 1;
    return mgr->theConn;
}

void DestroyWeaverStmtManager( StmtMgr mgr )
{
    if ( mgr == NULL ) return;
    if ( mgr->dataStack != NULL ) WFreeMemory(mgr->theConn,mgr->dataStack);
    if ( mgr->inputLog != NULL ) WFreeMemory(mgr->theConn,mgr->inputLog);
    if ( mgr->outputLog != NULL ) WFreeMemory(mgr->theConn,mgr->outputLog);
    WFreeMemory(mgr->theConn,mgr);
}

StmtMgr
CreateSubConnection( StmtMgr parent) 
{
    if ( parent == NULL ) return NULL;
    OpaqueWConn connection = WCreateSubConnection(parent->theConn);
    return CreateWeaverStmtManager(connection);
}

short IsValid( StmtMgr mgr )
{
    if ( mgr == NULL ) return 0;
    if ( mgr->theConn == NULL ) return (short)0;
    return (short)(WIsValidConnection(mgr->theConn));
}

void Clean( StmtMgr mgr, usercleanup input, usercleanup output )
{
        short x;
        if ( !IsValid(mgr) )  return;
        ClearData(mgr);
        for (x=0;x<mgr->log_count;x++)
        {
                mgr->inputLog[x].binder[0] = '\0';
                mgr->inputLog[x].base.pointer = 0;
                mgr->inputLog[x].base.maxlength = 0;
                mgr->inputLog[x].base.indicator = -1;
                mgr->inputLog[x].base.byval = 0;
                if ( mgr->inputLog[x].base.userspace != NULL && input != NULL )
                    input(mgr, mgr->inputLog[x].base.type,mgr->inputLog[x].base.userspace);
                mgr->inputLog[x].base.userspace = NULL;
                mgr->inputLog[x].base.type = 0;

                mgr->outputLog[x].index = 0;
                mgr->outputLog[x].base.pointer = 0;
                mgr->outputLog[x].base.maxlength = 0;
                mgr->outputLog[x].base.indicator = -1;
                mgr->outputLog[x].base.byval = 0;
                if ( mgr->outputLog[x].base.userspace != NULL && output != NULL )
                    output(mgr, mgr->outputLog[x].base.type,mgr->outputLog[x].base.userspace);
                mgr->outputLog[x].base.userspace = NULL;
                mgr->outputLog[x].base.type = 0;
        }
}

void Init( StmtMgr mgr, usercleanup input, usercleanup output )
{
        if ( !IsValid(mgr) )  return;
    if ( mgr->statement ) {
        if ( mgr->theConn ) {
            WDestroyPreparedStatement(mgr->statement);
        }
        mgr->statement = NULL;
        memset(&mgr->errordelegate,0,sizeof(Error));
        mgr->errorlevel = 0;
        Clean(mgr,input,output);
    }
}

short Begin( StmtMgr mgr ) 
{
    long err = 0;

    if ( !IsValid(mgr) ) return -1;

    err = WBegin(mgr->theConn,0);
    
    if ( err == 0 ) mgr->transactionId = WGetTransactionId(mgr->theConn);

    err = CheckForErrors(mgr);
    
    return err;
}

short Fetch( StmtMgr mgr ) 
{
    if ( !IsValid(mgr) )  return -1;
    long val = WFetch(mgr->statement);
    if ( val == 4 ) return 1;
    return CheckForErrors(mgr);
}

long Count( StmtMgr mgr )
{
    if ( !IsValid(mgr) ) return -1;
    return WExecCount(mgr->statement);
}

short Cancel( StmtMgr mgr ) 
{
    if ( !IsValid(mgr) ) return -1;
    WCancel(mgr->theConn);
    return CheckForErrors(mgr);
}

short Prepare( StmtMgr mgr ) 
{
    if ( !IsValid(mgr) ) return -1;
    WPrepare(mgr->theConn);
    return CheckForErrors(mgr);
}

short BeginProcedure( StmtMgr mgr ) 
{
    if ( !IsValid(mgr) ) return -1;
    WBeginProcedure(mgr->theConn);
    return CheckForErrors(mgr);
}

short EndProcedure( StmtMgr mgr ) 
{
    if ( !IsValid(mgr) ) return -1;
    WEndProcedure(mgr->theConn);
    return CheckForErrors(mgr);
}

short Exec( StmtMgr mgr ) 
{
    short err = 0;
    
    if ( !IsValid(mgr) ) return -1;

    WExec(mgr->statement);
    err = CheckForErrors(mgr);
    if ( !err ) {
        mgr->commandId = WGetCommandId(mgr->theConn);
    }

    return err;
}

long GetTransactionId( StmtMgr mgr ) {
        if ( !IsValid(mgr) )  return -1;
    return mgr->transactionId;
}

long GetCommandId( StmtMgr mgr ) {
        if ( !IsValid(mgr) )  return -1;
    return mgr->commandId;
}

short Commit( StmtMgr mgr )
{
    if ( !IsValid(mgr) ) return -1;
    mgr->transactionId = 0;
    WCommit(mgr->theConn);
    return CheckForErrors(mgr);
}

short Rollback( StmtMgr mgr ) 
{
    if ( !IsValid(mgr) ) return -1;
    mgr->transactionId = 0;
    WRollback(mgr->theConn);
    return CheckForErrors(mgr);
}

short UserLock(StmtMgr mgr, const char* grouptolock,uint32_t val,char lock)
{
    if ( !IsValid(mgr) ) return -1;
    WUserLock(mgr->theConn,grouptolock,val,lock);
    return CheckForErrors(mgr);
}

long  GetErrorCode( StmtMgr mgr )
{
    if ( !IsValid(mgr) ) return -1;
    if ( mgr->errorlevel == 2 ) return mgr->errordelegate.rc;
    return WGetErrorCode(mgr->theConn);
}

const char*  GetErrorText( StmtMgr mgr ) 
{
    if ( !IsValid(mgr) ) return "connection not valid";
    if ( mgr->errorlevel == 2 ) return mgr->errordelegate.text;
    return WGetErrorText(mgr->theConn);
}

const char* GetErrorState( StmtMgr mgr )
{
    if ( !IsValid(mgr) ) return "INVALID";
    if ( mgr->errorlevel == 2 ) return mgr->errordelegate.state;
    return WGetErrorState(mgr->theConn);
}

short ParseStatement(StmtMgr mgr, const char* statement)
{	
    if ( !IsValid(mgr) ) return -1;

    if ( mgr->dataStack != NULL ) {
        mgr->statement = WPrepareStatement(mgr->theConn,statement);
        return CheckForErrors(mgr);
    } else {
        return -1;
    }
}

Input AddBind(StmtMgr mgr, const char * vari, short type)
{
        int         otype;
        Input        bind = NULL;
        Bound       base = NULL;
        
        if ( !IsValid(mgr) ) return NULL;
        
        void*       savestack = mgr->dataStack;
        long        stmtsz = GetStatementSpaceSize(mgr);
             short x;
       
    /*  remove the marker flag of the named parameter if there is one */
        switch (*vari) {
            case '$':
            case '?':
            case ':':
                vari++;
        }
    
        for (x=0;x<mgr->log_count;x++)
        {
            if ( mgr->inputLog[x].binder[0] == '\0' || strcmp(mgr->inputLog[x].binder,vari) == 0 ) break;
        }
        if ( x == mgr->log_count ) {
            ExpandBindings(mgr);
        }
        
        bind = &mgr->inputLog[x];
        base = InputToBound(bind);

        base->indicator = 0;
        if ( base->type == type ) return bind;
        
	strncpy(bind->binder,vari,64);
	base->type = type;
	
	switch(type)
	{
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
                base->maxlength = WPipeSize(mgr->theConn);
                base->byval = 1;
                break;
            default:
                break;
	}
        /*  first check to see if the data will fit  */
        mgr->holdingArea = align(mgr, mgr->holdingArea);
        if ( mgr->holdingArea + base->maxlength > MAX_STMTSIZE ) {
            DelegateError(mgr,"PREPARE","no statement binding space left",803);
            return NULL; 
        }
        while ( stmtsz < (mgr->holdingArea + base->maxlength) ) {
            stmtsz = ( stmtsz * 2 < MAX_STMTSIZE ) ? stmtsz * 2 : MAX_STMTSIZE;
            SetStatementSpaceSize(mgr,stmtsz);
        }   
        
        if ( type == STREAMTYPE ) otype = BLOBTYPE;
        else otype = type;
        
        base->pointer = mgr->holdingArea;
        mgr->holdingArea += base->maxlength;
/*  if the dataStack has been moved, all the pointers need to be reset  */
        if ( savestack != mgr->dataStack ) {
            for (x=0;x<mgr->log_count;x++) {
                bind = &mgr->inputLog[x];
                base = InputToBound(bind);
                if ( bind->binder[0] != '\0' ) {
                    WBindLink(mgr->statement,bind->binder,Advance(mgr, base->pointer),base->maxlength,&base->indicator,otype,type);
                }
            }
        } else {
            WBindLink(mgr->statement,bind->binder,Advance(mgr, base->pointer),base->maxlength,&base->indicator,otype,type);
        } 

        return bind;
}

Input  GetBind(StmtMgr mgr, const char * vari, short type) {
        Input    bind = NULL;
        if ( !IsValid(mgr) ) return NULL;

     /*  remove the marker flag of the named parameter if there is one */
        switch (*vari) {
            case '$':
            case '?':
            case ':':
                vari++;
        }
        
        short x;
        for (x=0;x<mgr->log_count;x++)
        {
            if (mgr->inputLog[x].binder[0] == '\0' || strcmp(vari,mgr->inputLog[x].binder) == 0) break;
        }
        if ( x == mgr->log_count || mgr->inputLog[x].binder[0] == '\0' ) {
            return AddBind(mgr,vari,type);
        } else {
            bind = &mgr->inputLog[x];
        }
        
        return bind;
}

void* SetUserspace(StmtMgr mgr, Bound bound, void* target) {
    if ( bound != NULL ) {
        void* old = bound->userspace;
        bound->userspace = target;
        return old;
    }
    return NULL;
}

Input SetInputValue(StmtMgr mgr, const char * vari, short type, void* data, int length)
{
    Input bound = GetBind(mgr,vari,type);

    if ( bound != NULL ) {
        Bound base = InputToBound(bound);
        if ( data == NULL ) {
            base->indicator = 0;
        } else {
            if ( base->byval ) {
                 if ( length < 0 || base->maxlength < length ) {
                    length = base->maxlength;
                 }
                 memcpy(Advance(mgr, base->pointer),data,length);
                 base->indicator = 1;
            } else {
                char*  space = Advance(mgr, base->pointer);
                if ( base->indicator == 2 ) {
                    WFreeMemory(mgr->theConn,*(void**)(space+4));
                }
                if ( base->maxlength < length + 4 ) {
                    *(int32_t*)space = -1;
                    space += 4;
                    *(void**)space = WAllocStatementMemory(mgr->statement,length + 4);
                    space = *(void**)space;
                    base->indicator = 2;
                } else {
                    base->indicator = 1;
                }
                *(int32_t*)space = length + 4;
                space += 4;
                memcpy(space,data,length);
            }
        }
    }
    return bound;
}

Output SetOutputValue(StmtMgr mgr, int index, short type, void* data, int length)
{
    Output bound = OutputLink(mgr,index,type);

    if ( bound != NULL ) {
        Bound base = OutputToBound(bound);
        if ( data == NULL ) {
            base->indicator = 0;
        } else {
            if ( base->byval ) {
                 if ( length < 0 || base->maxlength < length ) {
                    length = base->maxlength;
                 }
                 memcpy(Advance(mgr, base->pointer),data,length);
            } else {
                char*  space = Advance(mgr, base->pointer);
                if ( base->maxlength < length + 4 ) {
                    length = base->maxlength - 4;
                }
                memcpy(space + 4,data,length);
                *(int32_t*)space = length + 4;
            }
            base->indicator = 1;
        }
    }
    return bound;
}

short GetOutputs(StmtMgr mgr, void* funcargs, outputfunc sendfunc) 
{
    short x;
    
    if ( !IsValid(mgr) ) return -1;
    for (x=0;x<mgr->log_count;x++) {
        Output      output = &mgr->outputLog[x];
        void* value = NULL;
        
        if ( output->index == 0 ) break;
        if ( output->base.indicator == 1 ) value = Advance(mgr,output->base.pointer);
        else if ( output->base.indicator == 2 ) value = *(void**)Advance(mgr,output->base.pointer);
        if ( output->base.type == 0 ) value = NULL;
        sendfunc(mgr,output->base.type,value,output->clength,output->base.userspace,funcargs);
    }

    return 0;
}

Output OutputLink(StmtMgr mgr, int index, short type)
{
        Output      link = NULL;
        Bound       base = NULL;
        void*       savestack = mgr->dataStack;

        if ( !IsValid(mgr) ) {
            return NULL;
        } else {
            short x;
            
            for (x=0;x<mgr->log_count;x++)
            {
                if ((index == mgr->outputLog[x].index) ||(mgr->outputLog[x].index == 0)) break;
            }

            if ( x == mgr->log_count ) {
                ExpandBindings(mgr);
            }
            
            link = &mgr->outputLog[x];
            base = &link->base;
        }

        base->indicator = 0;
        if ( base->type == type ) return link;
                        
        switch(type)
        {
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
                base->maxlength = WPipeSize(mgr->theConn);
                base->byval = 1;
            default:
                break;
        }

        mgr->holdingArea = align(mgr,mgr->holdingArea);
        base->pointer = mgr->holdingArea;
        link->index = index;
        base->type = type;
        mgr->holdingArea += base->maxlength;

        if ( mgr->holdingArea > MAX_STMTSIZE ) {
            DelegateError(mgr,"PREPARE","no statement binding space left",803);
            return NULL;
        }

        while ( GetStatementSpaceSize(mgr) < (mgr->holdingArea) ) {
            long stmtsz = ( stmtsz * 2 < MAX_STMTSIZE ) ? stmtsz * 2 : MAX_STMTSIZE;
            SetStatementSpaceSize(mgr,stmtsz);
        }
/*  if the dataStack has been moved, all the pointers need to be reset  */
       if ( savestack != mgr->dataStack ) {
            short x;
            for (x=0;x<mgr->log_count;x++) {
                link = &mgr->outputLog[x];
                base = OutputToBound(link);
                if ( mgr->outputLog[x].index != 0 ) {
                    WOutputLink(mgr->statement,link->index,Advance(mgr,base->pointer),base->maxlength,type,&base->indicator,&link->clength);
                }
            }
        } else {
            WOutputLink(mgr->statement,link->index,Advance(mgr,base->pointer),base->maxlength,type,&base->indicator,&link->clength);
        }
        
        return link;
}

void ClearData(StmtMgr mgr)
{
    if ( !IsValid(mgr) ) return;
	if (mgr->dataStack != NULL ) memset(mgr->dataStack,'\0',mgr->stackSize);
        mgr->holdingArea = 0;
}

void* Advance(StmtMgr mgr, long size)
{
    if ( !IsValid(mgr) ) return NULL;
	if (size >= mgr->stackSize ) return NULL;
	return ((char*)mgr->dataStack)+size;
}

short SetStatementSpaceSize(StmtMgr mgr, long size ) 
{
    if ( !IsValid(mgr) ) return -1;
    char* newbuf = WAllocConnectionMemory(mgr->theConn,size);
    if ( mgr->dataStack != NULL) {
        memmove(newbuf,mgr->dataStack,mgr->stackSize);
        WFreeMemory(mgr->theConn,mgr->dataStack);
    }
    mgr->dataStack = newbuf;
    mgr->stackSize = size;
    return 0;
}

short ExpandBindings(StmtMgr mgr) 
{
    if ( !IsValid(mgr) ) return -1;
    int count = mgr->log_count * 2;
    if ( count <= 0 ) count = 2;
    inputDef* inputs = WAllocConnectionMemory(mgr->theConn,sizeof(inputDef) * count);
    memset(inputs,0x00,sizeof(inputDef) * count);
    outputDef* outputs = WAllocConnectionMemory(mgr->theConn,sizeof(outputDef) * count);
    memset(outputs,0x00,sizeof(outputDef) * count);
    if ( mgr->inputLog != NULL ) {
        memmove(inputs,mgr->inputLog,sizeof(inputDef) * mgr->log_count);
        WFreeMemory(mgr->theConn,mgr->inputLog);
    }
    if ( mgr->outputLog != NULL ) {
        memmove(outputs,mgr->outputLog,sizeof(outputDef) * mgr->log_count);
        WFreeMemory(mgr->theConn,mgr->outputLog);
    }    
    mgr->inputLog = inputs;
    mgr->outputLog = outputs;
    mgr->log_count = count;
    for (count=0;count<mgr->log_count;count++) {
        Output link = &mgr->outputLog[count];
        Bound base = OutputToBound(link);
        if ( link->index != 0 ) {
            WOutputLink(mgr->statement,link->index,Advance(mgr,base->pointer),base->maxlength,base->type,&base->indicator,&link->clength);
        }
    }    
    for (count=0;count<mgr->log_count;count++) {
        Input bind = &mgr->inputLog[count];
        Bound base = InputToBound(bind);
        if ( bind->binder[0] != '\0' ) {  
            int otype = ( base->type == STREAMTYPE ) ? BLOBTYPE : base->type;
            WBindLink(mgr->statement,bind->binder,Advance(mgr, base->pointer),base->maxlength,&base->indicator,otype,base->type);
        }
    }
    return 0;
}


short SetStatementBlobSize(StmtMgr mgr, long size ) 
{
    if ( !IsValid(mgr) ) return -1;
	mgr->blob_size = size;
	if ( ( mgr->blob_size * 4 ) > mgr->stackSize ) SetStatementSpaceSize(mgr, mgr->blob_size * 4);
	return 0;
}

long GetStatementSpaceSize( StmtMgr mgr ) 
{
    if ( !IsValid(mgr) ) return -1;
	return mgr->stackSize;
}


long GetStatementBlobSize( StmtMgr mgr ) 
{
    if ( !IsValid(mgr) ) return -1;
	return mgr->blob_size;
}

short DelegateError(StmtMgr mgr, const char* state,const char* text,int code)
{
    if ( !IsValid(mgr) ) return -1;
	mgr->errorlevel = 2;
	mgr->errordelegate.rc = code;
	strncpy(mgr->errordelegate.text,text,255);
	strncpy(mgr->errordelegate.state,text,40);
}

short CheckForErrors( StmtMgr mgr ) 
{
    if ( !IsValid(mgr) ) return -1;
	long cc = WGetErrorCode(mgr->theConn);
        if ( mgr->errorlevel == 2 ) return 2;
        if ( cc != 0 ) {  
            mgr->errorlevel = 1;
            return 1;
        } 
	mgr->errorlevel = 0;
        
	return 0;
}

long align(StmtMgr mgr, long pointer ) 
{
	pointer += (sizeof(long) - ((long)Advance(mgr, pointer) % sizeof(long)));
	return pointer;
}

short StreamExec(StmtMgr mgr, char* statement)
{
    if ( !IsValid(mgr) ) return -1;

    WStreamExec(mgr->theConn,statement);
    
    return CheckForErrors(mgr);
}

Pipe PipeConnect(StmtMgr mgr, void* args, pipefunc func)
{    
    if ( !IsValid(mgr) ) return NULL;
    return  WPipeConnect(mgr->theConn,args,func);

}

void* PipeDisconnect(StmtMgr mgr, Pipe comm)
{
    if ( !IsValid(mgr) ) return NULL;
     return WPipeDisconnect(mgr->theConn,comm);
}

void ConnectStdIO(StmtMgr mgr,void *args,pipefunc in, pipefunc out)
{
    if ( !IsValid(mgr) ) return;
    WConnectStdIO(mgr->theConn,args,in,out);
}

void DisconnectStdIO(StmtMgr mgr) {
    if ( !IsValid(mgr) ) return;
    WDisconnectStdIO(mgr->theConn);
}

