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

struct outputObj {
        struct bound            base;
	long			index;
	int			clength;
};


struct bindObj {
        struct bound            base;
	char			binder[64];
};

typedef struct  WeaverStmtManager {
        short				statementParsed;

        long				transactionId;	
        long				commandId;	
        void*				dataStack;
        long				stackSize;
        long				blob_size;

        Error				errordelegate;
        long				errorlevel;	

        OpaqueWConn 			theConn;  

        long				holdingArea;
        long				statementLength;

        struct bindObj			bindLog[MAX_FIELDS];
        struct outputObj		outputLog[MAX_FIELDS];

} WeaverStmtManager;

static Input  GetBind(StmtMgr mgr, const char * vari, short type);
static Input  AddBind(StmtMgr mgr, const char * vari, short type);
static void Clean( StmtMgr mgr,usercleanup input,usercleanup output);
static long  align(StmtMgr mgr, long pointer);
static void* Advance(StmtMgr mgr,long size);


StmtMgr
CreateWeaverStmtManager( const char* name, const char * paslong, const char* connect)
{
    StmtMgr mgr = (StmtMgr)os_malloc(sizeof(WeaverStmtManager));
    
    mgr->theConn = WCreateConnection(name,paslong,connect);
        
    mgr->dataStack = NULL;
    mgr->stackSize = 0;

    if ( mgr->theConn && WIsValidConnection(mgr->theConn) ) {
        short x;
        int counter = 0;

        mgr->holdingArea = 0;

        mgr->blob_size = BLOBSIZE;
        mgr->stackSize = mgr->blob_size * 4;
        mgr->dataStack = os_malloc(mgr->stackSize);
        mgr->statementParsed = 0;
        mgr->statementLength = 0;
        mgr->transactionId = 0;
        
        memset(&mgr->errordelegate,0x00,sizeof(Error));

/*  zero statement structures */
       for ( counter = 0;counter < MAX_FIELDS;counter++ ) {
            memset(&mgr->outputLog[counter],0,sizeof(struct outputObj));
            mgr->outputLog[counter].base.indicator = -1;
            mgr->outputLog[counter].base.byval = 0;
            mgr->outputLog[counter].base.userspace = NULL;
            memset(&mgr->bindLog[counter],0,sizeof(struct bindObj));
            mgr->bindLog[counter].base.indicator = -1;
            mgr->bindLog[counter].base.byval = 0;
            mgr->bindLog[counter].base.userspace = NULL;
        }
    } else {
        if ( mgr->theConn ) WDestroyConnection(mgr->theConn);
        mgr->theConn = NULL;
        os_free(mgr);
        mgr = NULL;
    }
    
    return mgr;
} 

void DestroyWeaverStmtManager( StmtMgr mgr )
{
    mgr->holdingArea = 0;
    if ( mgr->theConn != NULL ) {
        /*  it's possible the the owning thread is not the
         *  destroying thread so to a cancel/join for
         *  safety's sake.
         */
        WCancelAndJoin(mgr->theConn);
        WDestroyConnection(mgr->theConn);
        mgr->theConn = NULL;
    }
    if ( mgr->dataStack != NULL ) os_free(mgr->dataStack);
    os_free(mgr);
}

StmtMgr
CreateSubConnection( StmtMgr parent) 
{
    StmtMgr mgr = (StmtMgr)os_malloc(sizeof(WeaverStmtManager));
    mgr->theConn = WCreateSubConnection(parent->theConn);
        
    mgr->dataStack = NULL;
    mgr->stackSize = 0;

    if ( WIsValidConnection(mgr->theConn) ) {
        short x;
        int counter = 0;

        mgr->holdingArea = 0;
        mgr->blob_size = BLOBSIZE;
        mgr->stackSize = mgr->blob_size * 4;
        mgr->dataStack = os_malloc(mgr->stackSize);
        mgr->statementParsed = 0;
        mgr->statementLength = 0;
        mgr->transactionId = 0;
        
        memset(&mgr->errordelegate,0x00,sizeof(Error));

/*  zero statement structures */
       for ( counter = 0;counter < MAX_FIELDS;counter++ ) {
            memset(&mgr->outputLog[counter],0,sizeof(struct outputObj));
            mgr->outputLog[counter].base.indicator = -1;
            mgr->outputLog[counter].base.byval = 0;
            mgr->outputLog[counter].base.userspace = NULL;
            memset(&mgr->bindLog[counter],0,sizeof(struct bindObj));
            mgr->bindLog[counter].base.indicator = -1;
            mgr->bindLog[counter].base.byval = 0;
            mgr->bindLog[counter].base.userspace = NULL;
        }
    } else {
        WDestroyConnection(mgr->theConn);
        mgr->theConn = NULL;
        os_free(mgr);
    }

    return mgr;
}

short IsValid( StmtMgr mgr )
{
    if ( mgr->theConn == NULL ) return (short)0;
    return (short)(WIsValidConnection(mgr->theConn));
}

void Clean( StmtMgr mgr, usercleanup input, usercleanup output )
{
        short x;

        ClearData(mgr);
        for (x=0;x<MAX_FIELDS;x++)
        {
                mgr->bindLog[x].binder[0] = '\0';
                mgr->bindLog[x].base.pointer = 0;
                mgr->bindLog[x].base.maxlength = 0;
                mgr->bindLog[x].base.indicator = -1;
                mgr->bindLog[x].base.byval = 0;
                if ( mgr->bindLog[x].base.userspace != NULL && input != NULL )
                    input(mgr, mgr->bindLog[x].base.type,mgr->bindLog[x].base.userspace);
                mgr->bindLog[x].base.userspace = NULL;
                mgr->bindLog[x].base.type = 0;

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
    if ( mgr->statementParsed ) {
        memset(mgr->dataStack,0x00,mgr->statementLength);
        mgr->statementParsed = 0;
        memset(&mgr->errordelegate,0,sizeof(Error));
        mgr->errorlevel = 0;
        Clean(mgr,input,output);
    }
}

short Begin( StmtMgr mgr ) 
{
    long err = 0;

    mgr->statementLength = 0;

    if ( !IsValid(mgr) ) return -1;

    err = WBegin(mgr->theConn,0);
    
    if ( err == 0 ) mgr->transactionId = WGetTransactionId(mgr->theConn);

    err = CheckForErrors(mgr);
    
    return err;
}

short Fetch( StmtMgr mgr ) 
{
    long val = WFetch(mgr->theConn);
    if ( val == 4 ) return 1;
    return CheckForErrors(mgr);
}

long Count( StmtMgr mgr )
{
    if ( !IsValid(mgr) ) return -1;
    return WExecCount(mgr->theConn);
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
    int x=0;
    short err = 0;
    
    if ( !IsValid(mgr) ) return -1;
/*  make sure all the bound variables have been set to null or 
*  a value  */              
    for (x=0;x<MAX_FIELDS;x++)
    {
        if (mgr->bindLog[x].binder[0] == '\0') break;
        if ( mgr->bindLog[x].base.indicator < 0 ) {
            char        msg[255];
            snprintf(msg,255,"bound variable %s not set",mgr->bindLog[x].binder);
            return DelegateError(mgr,"EXEC",msg,849);
        }
    }

    WExec(mgr->theConn);
    err = CheckForErrors(mgr);
    if ( !err ) {
        mgr->commandId = WGetCommandId(mgr->theConn);
    }

    return err;
}

long GetTransactionId( StmtMgr mgr ) {
    return mgr->transactionId;
}

long GetCommandId( StmtMgr mgr ) {
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

short ParseStatement(StmtMgr mgr, const char* thePass, long passLen)
{	
    mgr->statementLength = 0;

    if ( !IsValid(mgr) ) return -1;

    if ( mgr->dataStack != NULL ) {
        memmove((char*)mgr->dataStack,thePass,passLen + 1);
        mgr->holdingArea = passLen + 1;
        mgr->statementLength = passLen + 1;
        mgr->statementParsed = 1;
        WParsingFunc(mgr->theConn,(char*)mgr->dataStack);
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
        void*       savestack = mgr->dataStack;
        long        stmtsz = GetStatementSpaceSize(mgr);
        
    /*  remove the marker flag of the named parameter if there is one */
        switch (*vari) {
            case '$':
            case '?':
            case ':':
                vari++;
        }
    
        if ( !IsValid(mgr) ) {
            return NULL;
        } else {
            short x;
            for (x=0;x<MAX_FIELDS;x++)
            {
                if ( mgr->bindLog[x].binder[0] == '\0') break;
            }
            if ( x == MAX_FIELDS ) {
                DelegateError(mgr,"BINDING","too many bind variables",851);
                return NULL;
            } else {
                bind = &mgr->bindLog[x];
                base = &bind->base;
            }
        }

        base->indicator = 0;
        if ( base->type == type ) return bind;
        
	strncpy(bind->binder,vari,255);
	base->type = type;
	
	switch(type)
	{
            case INT4TYPE:
                base->maxlength = 4;
                base->byval = 1;
                break;
            case VARCHARTYPE:
                base->maxlength = 259;
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
                base->maxlength = 259;
                base->byval = 0;
                break;
            case BLOBTYPE:
            case TEXTTYPE:
            case JAVATYPE:
                 base->maxlength = (mgr->blob_size + 4);
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
                base->maxlength = (mgr->blob_size + 4);
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
            short x;
            for (x=0;x<MAX_FIELDS;x++) {
                bind = &mgr->bindLog[x];
                if ( WBindWithIndicate(mgr->theConn,bind->binder,Advance(mgr, base->pointer),base->maxlength,&base->indicator,otype,type) ) {
                    break;
                }
            }
        } else {
            WBindWithIndicate(mgr->theConn,bind->binder,Advance(mgr, base->pointer),base->maxlength,&base->indicator,otype,type);
        } 

        return bind;
}

Input  GetBind(StmtMgr mgr, const char * vari, short type) {
        Input    bind = NULL;

     /*  remove the marker flag of the named parameter if there is one */
        switch (*vari) {
            case '$':
            case '?':
            case ':':
                vari++;
        }
        
        if ( IsValid(mgr) ) {
            short x;
            for (x=0;x<MAX_FIELDS;x++)
            {
                if (mgr->bindLog[x].binder == '\0' || strcmp(vari,mgr->bindLog[x].binder) == 0) break;
            }
            if ( x == MAX_FIELDS || mgr->bindLog[x].binder == '\0' ) {
                return AddBind(mgr,vari,type);
            } else {
                bind = &mgr->bindLog[x];
            }
        }
        
        return bind;
}
/*
short GetType(StmtMgr mgr, Bound bound) {
    return bound->type;
}
*/
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
    for (x=0;x<MAX_FIELDS;x++) {
        Output      output = &mgr->outputLog[x];
        void* value = NULL;
        
        if ( output->index == 0 ) break;
        if ( output->base.indicator == 1 ) value = Advance(mgr,output->base.pointer);
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
            
            for (x=0;x<MAX_FIELDS;x++)
            {
                if ((index == mgr->outputLog[x].index) ||(mgr->outputLog[x].index == 0)) break;
            }

            if ( x == MAX_FIELDS ) {
                DelegateError(mgr,"LINKING","too many output variables",852);
                return NULL;
            } else {
                link = &mgr->outputLog[x];
                base = &link->base;
            }
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
                base->maxlength = 259;
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
                base->maxlength = 259;
                base->byval = 0;
                break;
            case JAVATYPE:
                base->maxlength = (mgr->blob_size + 4);
                base->byval = 0;
                break;
            case BLOBTYPE:
                base->maxlength = (mgr->blob_size + 4);
                base->byval = 0;
                break;
            case TEXTTYPE:
                base->maxlength = (mgr->blob_size + 4);
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
            for (x=0;x<MAX_FIELDS;x++) {
                link = &mgr->outputLog[x];
                if ( mgr->outputLog[x].index != 0 ) {
                    WOutputLinkInd(mgr->theConn,link->index,Advance(mgr,base->pointer),base->maxlength,type,&base->indicator,&link->clength);
                } else {
                    break;
                }
            }
        } else {
            WOutputLinkInd(mgr->theConn,index,Advance(mgr,base->pointer),base->maxlength,type,&base->indicator,&link->clength);
        }
        
        return link;
}

void ClearData(StmtMgr mgr)
{
	if (mgr->dataStack != NULL && mgr->statementLength > 0) memset(Advance(mgr, mgr->statementLength),'\0',mgr->stackSize - mgr->statementLength);
}

void* Advance(StmtMgr mgr, long size)
{
	if (size >= mgr->stackSize ) return NULL;
	return ((char*)mgr->dataStack)+size;
}

short SetStatementSpaceSize(StmtMgr mgr, long size ) 
{
	mgr->stackSize = size;
	if ( mgr->dataStack == NULL ) {
		mgr->stackSize == 0;
		return 1;
	}
	printf("DEBUG: setting statement space to %d\n",size);
	mgr->dataStack = os_realloc(mgr->dataStack,size);
	return 0;
}


short SetStatementBlobSize(StmtMgr mgr, long size ) 
{
	mgr->blob_size = size;
	if ( ( mgr->blob_size * 4 ) > mgr->stackSize ) SetStatementSpaceSize(mgr, mgr->blob_size * 4);
	return 0;
}

long GetStatementSpaceSize( StmtMgr mgr ) 
{
	return mgr->stackSize;
}


long GetStatementBlobSize( StmtMgr mgr ) 
{
	return mgr->blob_size;
}

short DelegateError(StmtMgr mgr, const char* state,const char* text,int code)
{
	mgr->errorlevel = 2;
	mgr->errordelegate.rc = code;
	strncpy(mgr->errordelegate.text,text,255);
	strncpy(mgr->errordelegate.state,text,40);
}

short CheckForErrors( StmtMgr mgr ) 
{
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
    return  WPipeConnect(mgr->theConn,args,func);

}

void* PipeDisconnect(StmtMgr mgr, Pipe comm)
{
     return WPipeDisconnect(mgr->theConn,comm);
}

void ConnectStdIO(StmtMgr mgr,void *args,pipefunc in, pipefunc out)
{
    WConnectStdIO(mgr->theConn,args,in,out);
}

void DisconnectStdIO(StmtMgr mgr) {
    WDisconnectStdIO(mgr->theConn);
}

