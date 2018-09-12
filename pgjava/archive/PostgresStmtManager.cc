/*-------------------------------------------------------------------------
 *
 *	PostgresStmtManager.cc 
 *		Statement Manager talks to PostgresConnection
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *
 *-------------------------------------------------------------------------
 */
#include <stdlib.h>
#include <strings.h>
#include <math.h>

#include "PostgresStmtManager.h"
#include "env/PostgresInterface.h"  


PostgresStmtManager::PostgresStmtManager( const char* name, const char * paslong, const char* connect )
{
	theConn = PGCreateConnection(name,paslong,connect);
        
	dataStack = NULL;
	stackSize = 0;
        clean = false;
	
	if ( PGIsValidConnection(theConn) ) {
		short x;
		holdingArea = 0;
		bindFlag = 0;
		int counter = 0;

		blob_size = BLOBSIZE;
		stackSize = blob_size * 4;
		dataStack = os_malloc(stackSize);

                stdpipein = NULL;
                stdpipeout = NULL;

		memset(&errordelegate,0x00,sizeof(Error));

/*  zero statement structures */
	       for ( counter = 0;counter < 20;counter++ ) {
       			memset(&outputLog[counter],0,sizeof(struct outputObj));
			outputLog[counter].indicator = -1;
       			memset(&bindLog[counter],0,sizeof(struct bindObj));
			bindLog[counter].indicator = -1;
                        pipes[counter] = NULL;
       		}
	} else {
       		CheckLowerError();
		PGDestroyConnection(theConn);
       		theConn = NULL;
	}
} 


PostgresStmtManager::~PostgresStmtManager( void )
{
	holdingArea = 0;
	if ( theConn != NULL ) {
		PGDestroyConnection(theConn);
		theConn = NULL;
	}
	if ( dataStack != NULL ) os_free(dataStack);
}

PostgresStmtManager::PostgresStmtManager( PostgresStmtManager* parent ) 
{
	theConn = PGCreateSubConnection(parent->theConn);
        
	dataStack = NULL;
	stackSize = 0;
        clean = false;
	
	if ( PGIsValidConnection(theConn) ) {
		short x;
		holdingArea = 0;
		bindFlag = 0;
		int counter = 0;

		blob_size = BLOBSIZE;
		stackSize = blob_size * 4;
		dataStack = os_malloc(stackSize);

                stdpipein = NULL;
                stdpipeout = NULL;

		memset(&errordelegate,0x00,sizeof(Error));

/*  zero statement structures */
	       for ( counter = 0;counter < 20;counter++ ) {
       			memset(&outputLog[counter],0,sizeof(struct outputObj));
			outputLog[counter].indicator = -1;
       			memset(&bindLog[counter],0,sizeof(struct bindObj));
			bindLog[counter].indicator = -1;
                        pipes[counter] = NULL;
       		}
	} else {
       		CheckLowerError();
		PGDestroyConnection(theConn);
       		theConn = NULL;
	}
}

PostgresStmtManager* PostgresStmtManager::CreateSubConnection( void ) 
{

    PostgresStmtManager * setup = new PostgresStmtManager(this);
    return setup;
}

short PostgresStmtManager::IsValid( void )
{
	if ( theConn == NULL ) return (short)0;
	return (short)(PGIsValidConnection(theConn));
}

#ifdef PG_JAVA

void PostgresStmtManager::Clean(JNIEnv* env)
{
    if ( clean ) {
        Init(env);
        clean = false;
    }
}


void PostgresStmtManager::Init(JNIEnv* env)
{
	short x;
	bindFlag = 0;
        /*
	memset(dataStack,0,stackSize);
        */
	for (x=0;x<20;x++)
	{
		bindLog[x].numBind = 0;
		bindLog[x].binder[0] = '\0';
		bindLog[x].pointerRef = 0;
		bindLog[x].indicator = -1;
		if (outputLog[x].index != 0) {
			if ( outputLog[x].theObjectRef != NULL ) env->DeleteGlobalRef((jobject)outputLog[x].theObjectRef);
		}
		outputLog[x].index = 0;
		outputLog[x].indicator = -1;
                outputLog[x].theObjectRef = NULL;		

                if ( pipes[x] != NULL ) PipeDisconnect(x + 2);
                if ( stdpipein != NULL ) PipeDisconnect(0);
                if ( stdpipeout != NULL ) PipeDisconnect(1);
	}
	memset(&errordelegate,0,sizeof(errordelegate));
	errorlevel = 0;	

}

#else

void PostgresStmtManager::Clean( void )
{
    if ( clean ) {
        Init();
        clean = false;
    }
}

void PostgresStmtManager::Init( void )
{
	short x;
	bindFlag = 0;

	memset(dataStack,0x00,stackSize);
	for (x=0;x<20;x++)
	{
            bindLog[x].numBind = 0;
            bindLog[x].binder[0] = '\0';
            bindLog[x].pointerRef = 0;
            bindLog[x].indicator = -1;

            outputLog[x].index = 0;
            outputLog[x].indicator = -1;
            outputLog[x].theObjectRef = NULL;	

            if ( pipes[x] != NULL ) PipeDisconnect(x + 2);
	}

	memset(&errordelegate,0,sizeof(errordelegate));
	errorlevel = 0;
	
}

#endif

short PostgresStmtManager::Begin( void ) 
{
	int err = 0;
	statementLength = 0;
	int counter = 0;
/*  zero statement structures */
   for ( counter = 0;counter < 20;counter++ ) {
       	memset(&outputLog[counter],0,sizeof(struct outputObj));
		outputLog[counter].indicator = -1;
       	memset(&bindLog[counter],0,sizeof(struct bindObj));
		bindLog[counter].indicator = -1;
   }

   err = PGBegin(theConn,0);
	if ( err == 0 ) transactionId = PGGetTransactionId(theConn);
	return CheckLowerError();
	
}

short PostgresStmtManager::Fetch( void ) 
{
/*	ClearData();		*/
	PGFetch(theConn);
	return CheckLowerError();
}

short PostgresStmtManager::Cancel( void ) 
{
	PGCancel(theConn);
	return CheckLowerError();
}

short PostgresStmtManager::Prepare( void ) 
{
	PGPrepare(theConn);
	return CheckLowerError();
}

short PostgresStmtManager::BeginProcedure( void ) 
{
	PGBeginProcedure(theConn);
	return CheckLowerError();
}

short PostgresStmtManager::EndProcedure( void ) 
{
	PGEndProcedure(theConn);
	return CheckLowerError();
}

short PostgresStmtManager::Rollback( void ) 
{
	PGRollback(theConn);
	transactionId = 0;
	return CheckLowerError();
}

short PostgresStmtManager::Exec( void ) 
{
	PGExec(theConn);
        clean = true;
	return CheckLowerError();
}

short PostgresStmtManager::Commit( void )
{
	PGCommit(theConn);
	transactionId = 0;
	return CheckLowerError();
}

short PostgresStmtManager::UserLock(const char* grouptolock,uint32_t val,char lock)
{
	PGUserLock(theConn,grouptolock,val,lock);
	return CheckLowerError();
}

int  PostgresStmtManager::GetErrorCode( void )
{
	return (errorlevel == 1) ? PGGetErrorCode(theConn) : errordelegate.rc;
}

const char*  PostgresStmtManager::GetErrorText( void ) 
{
	return (errorlevel == 1) ? PGGetErrorText(theConn) : errordelegate.text;
}

const char* PostgresStmtManager::GetErrorState( void )
{
	return (errorlevel == 1) ? PGGetErrorState(theConn) : errordelegate.state;
}


short PostgresStmtManager::ParseStatement( const char* thePass, long passLen)
{	
	statementLength = 0;
/*
   if ( strlen(thePass) != passLen ) {
       return DelegateError("PARSING","Statement length is not correct",821);
    }
*/
	if ( dataStack != NULL ) {
            memmove((char*)dataStack,thePass,passLen + 1);
            holdingArea = passLen + 1;
            statementLength = passLen + 1;
            PGParsingFunc(theConn,(char*)dataStack);
        }
	return CheckLowerError();
}


short PostgresStmtManager::AddBind(const char * vari, long theType)
{
	short	x;

	for (x=0;x<20;x++)
	{
		if (bindLog[x].binder[0] == '\0') break;
	}
	if ( x == 20 ) {
		return DelegateError("BINDING","too many bind variables",851);
	}

	strncpy(bindLog[x].binder,vari,255);
	bindLog[x].type = theType;
	
	switch(theType)
	{
		case BINDINTEGER:
			/*  first check to see if the data will fit  */
			
			holdingArea = align(holdingArea);
			if ( holdingArea + 4 > stackSize ) 
				return DelegateError("PREPARE","no statement binding space left",803);
			PGBindWithIndicate(theConn,bindLog[x].binder,Advance(holdingArea),4,&bindLog[x].indicator,INT4TYPE,INT4TYPE);
			bindLog[x].pointerRef = holdingArea;
			holdingArea += 4;
			break;
		case BINDSTRING:
			holdingArea = align(holdingArea);
			if ( holdingArea + 259 > stackSize )
				return DelegateError("PREPARE","no statement binding space left",803);
			PGBindWithIndicate(theConn,bindLog[x].binder,Advance(holdingArea),259,&bindLog[x].indicator,VARCHARTYPE,VARCHARTYPE);
			bindLog[x].pointerRef = holdingArea;
			holdingArea += 259;
			break;
		case BINDCHARACTER:
			/*  first check to see if the data will fit  */
			
			holdingArea = align(holdingArea);
			if ( holdingArea + 1 > stackSize ) 
				return DelegateError("PREPARE","no statement binding space left",803);
			PGBindWithIndicate(theConn,bindLog[x].binder,Advance(holdingArea),1,&bindLog[x].indicator,CHARTYPE,CHARTYPE);
			bindLog[x].pointerRef = holdingArea;
			holdingArea += 1;
			break;
		case BINDBOOLEAN:
			/*  first check to see if the data will fit  */
			
			holdingArea = align(holdingArea);
			if ( holdingArea + 1 > stackSize )
				return DelegateError("PREPARE","no statement binding space left",803);
			PGBindWithIndicate(theConn,bindLog[x].binder,Advance(holdingArea),1,&bindLog[x].indicator,BOOLTYPE,BOOLTYPE);
			bindLog[x].pointerRef = holdingArea;
			holdingArea += 1;
			break;
		case BINDBINARY:
			holdingArea = align(holdingArea);
			if ( holdingArea + 259 > stackSize )
				return DelegateError("PREPARE","no statement binding space left",803);
			PGBindWithIndicate(theConn,bindLog[x].binder,Advance(holdingArea),259,&bindLog[x].indicator,BYTEATYPE,BYTEATYPE);
			bindLog[x].pointerRef = holdingArea;
			holdingArea += 259;
			break;
		case BINDBLOB:
			holdingArea = align(holdingArea);
			while ( (holdingArea + blob_size + 4) > stackSize ) {
				SetStatementSpaceSize(stackSize * 2);
			} 
			PGBindWithIndicate(theConn,bindLog[x].binder,Advance(holdingArea),blob_size + 4,&bindLog[x].indicator,BLOBTYPE,BLOBTYPE);
			bindLog[x].pointerRef = holdingArea;
			holdingArea += (blob_size) + 4;
			break;
		case BINDTEXT:
			holdingArea = align(holdingArea);
			while ( (holdingArea + blob_size + 4) > stackSize ) {
				SetStatementSpaceSize(stackSize * 2);
			} 
			PGBindWithIndicate(theConn,bindLog[x].binder,Advance(holdingArea),blob_size + 4,&bindLog[x].indicator,TEXTTYPE,TEXTTYPE);
			bindLog[x].pointerRef = holdingArea;
			holdingArea += (blob_size) + 4;
			break;
		case BINDJAVA:
			holdingArea = align(holdingArea);
			while ( (holdingArea + blob_size + 4) > stackSize ) {
				SetStatementSpaceSize(stackSize * 2);
			} 
			PGBindWithIndicate(theConn,bindLog[x].binder,Advance(holdingArea),blob_size + 4,&bindLog[x].indicator,JAVATYPE,JAVATYPE);
			bindLog[x].pointerRef = holdingArea;
			holdingArea += (blob_size) + 4;
			break;
		case BINDDATE:
			holdingArea = align(holdingArea);
			if ( holdingArea + 8 > stackSize )
				return DelegateError("PREPARE","no statement binding space left",803);
			PGBindWithIndicate(theConn,bindLog[x].binder,Advance(holdingArea),8,&bindLog[x].indicator,TIMESTAMPTYPE,TIMESTAMPTYPE);
			bindLog[x].pointerRef = holdingArea;
			holdingArea += 8;
			break;
		case BINDDOUBLE:
			holdingArea = align(holdingArea);
			if ( holdingArea + 8 > stackSize )
				return DelegateError("PREPARE","no statement binding space left",803);
			PGBindWithIndicate(theConn,bindLog[x].binder,Advance(holdingArea),8,&bindLog[x].indicator,DOUBLETYPE,DOUBLETYPE);
			bindLog[x].pointerRef = holdingArea;
			holdingArea += 8;
			break;
            case BINDLONG:
			holdingArea = align(holdingArea);
			if ( holdingArea + 8 > stackSize ) 
				return DelegateError("PREPARE","no statement binding space left",803);
			PGBindWithIndicate(theConn,bindLog[x].binder,Advance(holdingArea),8,&bindLog[x].indicator,LONGTYPE,LONGTYPE);
			bindLog[x].pointerRef = holdingArea;
			holdingArea += 8;
			break;			
       case BINDFUNCTION:
           break;
       case BINDSLOT:
			holdingArea = align(holdingArea);
			if ( holdingArea + blob_size + 4> stackSize )
				return DelegateError("PREPARE","no statement binding space left",803);
			PGBindWithIndicate(theConn,bindLog[x].binder,Advance(holdingArea),blob_size + 4,&bindLog[x].indicator,SLOTTYPE,SLOTTYPE);
			bindLog[x].pointerRef = holdingArea;
			holdingArea += blob_size + 4;
			break;
       case BINDSTREAM:
       case BINDDIRECT:
                        holdingArea = align(holdingArea);
			PGBindWithIndicate(theConn,bindLog[x].binder,Advance(holdingArea),PGPipeSize(theConn),&bindLog[x].indicator,BLOBTYPE,STREAMTYPE);
			bindLog[x].pointerRef = holdingArea;
			holdingArea += PGPipeSize(theConn);
			break;
            default:
			break;
	}

		return 0;
}

short PostgresStmtManager::SetLink(const char * vari,long theType,void* theData,int theLength)
{
	short   x;
	long 	storeLength = 0;
	int     pipe_pos;

	for (x=0;x<20;x++)
	{
		if (strcmp(vari,bindLog[x].binder) == 0) break;
	}
	
	if ( x == 20 ) {
		return DelegateError("PASSING","variable is not valid",800);
	}

	if ( theType == BINDNULL ) {
		bindLog[x].indicator = 0;
		return 0;
	}

	if ( theType != bindLog[x].type ) {
		return DelegateError("PASSING","variable type does not match the type bound for this variable",801); 
	}

	if ( theData == NULL ) {
		bindLog[x].indicator = 0;
		return 0;
	}
	
	switch(theType)
	{
		case BINDINTEGER:
				memcpy(Advance(bindLog[x].pointerRef),theData,4);
				bindLog[x].indicator = 1;
				break;
		case BINDSTRING:
			if ( strlen((char*)theData) > 255 ) {
				return DelegateError("PASSING","variable data must be smaller than 255 bytes",802);
			} else {
				*(int*)Advance(bindLog[x].pointerRef) = strlen((char*)theData) + 4;
				strncpy((char*)Advance(bindLog[x].pointerRef + 4),(char*)theData,255);
				bindLog[x].indicator = 1;
			}
			break;
		case BINDCHARACTER:
			memcpy(Advance(bindLog[x].pointerRef),theData,1);
			bindLog[x].indicator = 1;
			break;
		case BINDBOOLEAN:
				memcpy(Advance(bindLog[x].pointerRef),theData,1);
				bindLog[x].indicator = 1;
				break;
		case BINDBINARY:
				*(int*)Advance(bindLog[x].pointerRef) = theLength + 4;
				memcpy(Advance(bindLog[x].pointerRef + 4),theData,theLength);
				bindLog[x].indicator = 1;
			break;
		case BINDDATE:
			memcpy(Advance(bindLog[x].pointerRef),theData,sizeof(double));
			bindLog[x].indicator = 1;
			break;
		case BINDDOUBLE:
			memcpy(Advance(bindLog[x].pointerRef),theData,sizeof(double));
			bindLog[x].indicator = 1;
			break;
                case BINDLONG:
			memcpy(Advance(bindLog[x].pointerRef),theData,theLength);
			bindLog[x].indicator = 1;
			break;
		case BINDBLOB:
		case BINDTEXT:
		case BINDSLOT:
		case BINDJAVA:
                        if ( theLength > blob_size ) {
                                return DelegateError("PASSING","binary object does not fit in statement window, increase BLOB size",880);
                        }
                        *(int*)Advance(bindLog[x].pointerRef) = theLength + 4;
                        memcpy(Advance(bindLog[x].pointerRef + 4),theData,theLength);
                        bindLog[x].indicator = 1;
			break;
		case BINDSTREAM:
 		case BINDDIRECT:
                        pipe_pos = atoi((char*)theData) - 2;
                        if (pipe_pos >= 20) {
                                return DelegateError("PASSING","invalid pipe specified",881);
                        }
                        memcpy(Advance(bindLog[x].pointerRef),pipes[pipe_pos],PGPipeSize(theConn));
                        bindLog[x].indicator = 1;
			break;
            default:
		 	break;
	}
	return 0;
}

#ifdef PG_JAVA
short PostgresStmtManager::OutputLinker(jobject theRef, const char* slotname,const char* signature,
				const char* className, long theType, long index)
#else
short PostgresStmtManager::OutputLinker(void* theRef, long theType, long index)
#endif
{
	short  x;
        int pipe_pos;

	for (x=0;x<20;x++)
	{
		if ((index == outputLog[x].index)||(outputLog[x].index == 0)) break;
	}
	
	if ( x == 20 ) return DelegateError("LINKING","too many output variables",852);
	if (outputLog[x].index == 0)
	{
		switch(theType)
		{
			case BINDINTEGER:
				holdingArea = align(holdingArea);
				outputLog[x].pointerRef = holdingArea;
				holdingArea += 4;
				PGOutputLinkInd(theConn,index,Advance(outputLog[x].pointerRef),4,INT4TYPE,&outputLog[x].indicator,&outputLog[x].clength);
				break;
			case BINDSTRING:
				holdingArea = align(holdingArea);
				outputLog[x].pointerRef = holdingArea;
				holdingArea += 256;
				PGOutputLinkInd(theConn,index,Advance(outputLog[x].pointerRef),255,VARCHARTYPE,&outputLog[x].indicator,&outputLog[x].clength);
				break;
			case BINDCHARACTER:
				holdingArea = align(holdingArea);
				outputLog[x].pointerRef = holdingArea;
				holdingArea += 1;
				PGOutputLinkInd(theConn,index,Advance(outputLog[x].pointerRef),1,CHARTYPE,&outputLog[x].indicator,&outputLog[x].clength);
				break;
			case BINDBOOLEAN:
				holdingArea = align(holdingArea);
				outputLog[x].pointerRef = holdingArea;
				holdingArea += 1;
				PGOutputLinkInd(theConn,index,Advance(outputLog[x].pointerRef),1,BOOLTYPE,&outputLog[x].indicator,&outputLog[x].clength);
				break;
			case BINDBINARY:
				holdingArea = align(holdingArea);
				outputLog[x].pointerRef = holdingArea;
				holdingArea += 256;
				PGOutputLinkInd(theConn,index,Advance(outputLog[x].pointerRef),256,BYTEATYPE,&outputLog[x].indicator,&outputLog[x].clength);
				break;
			case BINDJAVA:
				holdingArea = align(holdingArea);
				outputLog[x].pointerRef = holdingArea;
				while ( (holdingArea + blob_size + 4) > stackSize ) {
					SetStatementSpaceSize(stackSize * 2);
				}
				holdingArea += (blob_size) + 4;
				PGOutputLinkInd(theConn,index,Advance(outputLog[x].pointerRef),(blob_size) + 4,JAVATYPE,&outputLog[x].indicator,&outputLog[x].clength);
				break;
			case BINDBLOB:
				holdingArea = align(holdingArea);
				outputLog[x].pointerRef = holdingArea;
				while ( (holdingArea + blob_size + 4) > stackSize ) {
					SetStatementSpaceSize(stackSize * 2);
				}
				holdingArea += (blob_size) + 4;
				PGOutputLinkInd(theConn,index,Advance(outputLog[x].pointerRef),(blob_size) + 4,BLOBTYPE,&outputLog[x].indicator,&outputLog[x].clength);
				break;
			case BINDTEXT:
				holdingArea = align(holdingArea);
				outputLog[x].pointerRef = holdingArea;
				while ( (holdingArea + blob_size + 4) > stackSize ) {
					SetStatementSpaceSize(stackSize * 2);
				}
				holdingArea += (blob_size) + 4;
				PGOutputLinkInd(theConn,index,Advance(outputLog[x].pointerRef),(blob_size) + 4,TEXTTYPE,&outputLog[x].indicator,&outputLog[x].clength);
				break;
			case BINDDATE:
				holdingArea = align(holdingArea);
				outputLog[x].pointerRef = holdingArea;
				holdingArea += 8;
				PGOutputLinkInd(theConn,index,Advance(outputLog[x].pointerRef),8,TIMESTAMPTYPE,&outputLog[x].indicator,&outputLog[x].clength);
				break;
			case BINDDOUBLE:
				holdingArea = align(holdingArea);
				outputLog[x].pointerRef = holdingArea;
				holdingArea += 8;
				PGOutputLinkInd(theConn,index,Advance(outputLog[x].pointerRef),8,DOUBLETYPE,&outputLog[x].indicator,&outputLog[x].clength);
				break;
                        case BINDLONG:
				holdingArea = align(holdingArea);
				outputLog[x].pointerRef = holdingArea;
				holdingArea += 8;
				PGOutputLinkInd(theConn,index,Advance(outputLog[x].pointerRef),8,LONGTYPE,&outputLog[x].indicator,&outputLog[x].clength);
				break;
                        case BINDFUNCTION:
				holdingArea = align(holdingArea);
				outputLog[x].pointerRef = holdingArea;
				holdingArea += blob_size + 4;
				PGOutputLinkInd(theConn,index,Advance(outputLog[x].pointerRef),8,FUNCTIONTYPE,&outputLog[x].indicator,&outputLog[x].clength);
				break;                                
                        case BINDSTREAM:
                        case BINDDIRECT:
				holdingArea = align(holdingArea);
				outputLog[x].pointerRef = holdingArea;
				holdingArea += PGPipeSize(theConn);
                                pipe_pos = atoi(slotname) - 2;
                                if (pipe_pos >= 20) {
                                        return DelegateError("PASSING","invalid pipe specified",882);
                                }
                                memmove(Advance(outputLog[x].pointerRef),pipes[pipe_pos],PGPipeSize(theConn));
				PGOutputLinkInd(theConn,index,Advance(outputLog[x].pointerRef),PGPipeSize(theConn),STREAMTYPE,&outputLog[x].indicator,&outputLog[x].clength);
				break;
			default:
				break;
		}
	}

	outputLog[x].index = index;
#ifdef PG_JAVA
	strncpy(outputLog[x].slotName,slotname,255);
	strncpy(outputLog[x].signature,signature,255);
	strncpy(outputLog[x].className,className,255);
	outputLog[x].theObjectRef = theRef;
#else
	outputLog[x].theObjectRef = Advance(outputLog[x].pointerRef);
#endif
	outputLog[x].type = theType;
	
	if ( holdingArea > stackSize ) 
		return DelegateError("OUTPUTLINK","no statement linking space left",803);
	else
		return 0;
}

short PostgresStmtManager::GetOutputRef(short index,long type,void** ref)
{
	short id = 0;
	for (id=0;id<20;id++)
	{
		if ((index == outputLog[id].index)) break;
	}

	if ( id == 20 ) return DelegateError("PASSING","variable is not valid",800);
	if ( type != outputLog[id].type ) return DelegateError("PASSING","type does not match the type \
	linked to this index",801);
	

	if ( id < 20 ) {
		if ( type != outputLog[id].type ) {
			return 1;
		} else if ( GetErrorCode() == 1403 ) {
			return 3;
		} else {
			*ref = outputLog[id].theObjectRef;
			return 0;
		}
	} else {
		return 2;
	}
			
}

void PostgresStmtManager::ClearData()
{
/*
	if ( statementLength > stackSize ) elog(FATAL,"ClearData()--dataoverrun");
*/
	if (dataStack != NULL && statementLength > 0) memset(Advance(statementLength),'\0',stackSize - statementLength);
}

void* PostgresStmtManager::Advance(long size)
{
	if (size >= stackSize ) return NULL;
	return ((char*)dataStack)+size;
}

short PostgresStmtManager::SetStatementSpaceSize( long size ) 
{
	stackSize = size;
	if ( dataStack == NULL ) {
		stackSize == 0;
		return 1;
	}
	printf("DEBUG: setting statement space to %d\n",size);
	dataStack = os_realloc(dataStack,size);
	return 0;
}


short PostgresStmtManager::SetStatementBlobSize( long size ) 
{
	blob_size = size;
	if ( ( blob_size * 4 ) > stackSize ) SetStatementSpaceSize(blob_size * 4);
	return 0;
}

long PostgresStmtManager::GetStatementSpaceSize( void ) 
{
	return stackSize;
}


long PostgresStmtManager::GetStatementBlobSize( void ) 
{
	return blob_size;
}

short PostgresStmtManager::DelegateError(const char* state,const char* text,int code)
{
	errorlevel = 2;
	errordelegate.rc = code;
	strncpy(errordelegate.text,text,255);
	strncpy(errordelegate.state,text,40);
	return code;
}

short PostgresStmtManager::CheckLowerError( void ) 
{
	short cc = PGGetErrorCode(theConn);
	if ( cc != 0 ) {
		errorlevel = 1;
	}
	return cc;
}

long PostgresStmtManager::align( long pointer ) 
{
	pointer += (sizeof(long) - ((long)Advance(pointer) % sizeof(long)));
	return pointer;
}


#ifdef PG_JAVA

short PostgresStmtManager::PassResults( JNIEnv* env )
{
	short   x;
	jclass		classID;
	jfieldID		fieldID; 
	jfieldID		nullfield; 

	for(x=0;x<20;x++)
	{
		if (outputLog[x].index == 0) break;
                if (outputLog[x].theObjectRef == NULL ) continue;

		if ( outputLog == NULL ) {
			printf("bad output\n");
			exit(99);
		}
		
		classID = env->GetObjectClass((jobject)outputLog[x].theObjectRef);
		fieldID = env->GetFieldID(classID, outputLog[x].slotName,outputLog[x].signature);
		nullfield = env->GetFieldID(classID,"isnull","Z");

		if ( env->ExceptionCheck() ) {
			env->ExceptionClear();
		}

		if ( fieldID == NULL ) {
			return -3;
		}

		if ( nullfield != NULL ) {
			jboolean isnull = ( outputLog[x].indicator != 0 ) ? true : false;
			env->SetBooleanField((jobject)outputLog[x].theObjectRef,nullfield,isnull);
			if ( isnull ) return 0;
		}

			switch(outputLog[x].type)
			{
				case BINDINTEGER:
				{
					jint var = *(int *)Advance(outputLog[x].pointerRef);
					env->SetIntField((jobject)outputLog[x].theObjectRef,fieldID,var);
					break;
				}
				case BINDSTRING:
				{
					*((char*)Advance(outputLog[x].pointerRef + outputLog[x].clength)) = 0x00;
					jstring utfStr = env->NewStringUTF((char*)Advance(outputLog[x].pointerRef));
					env->SetObjectField((jobject)outputLog[x].theObjectRef,fieldID,utfStr);
					break;
				}
				case BINDCHARACTER:
				{
					jchar cch  = (jchar)*(char*)Advance(outputLog[x].pointerRef);
					env->SetCharField((jobject)outputLog[x].theObjectRef,fieldID,cch);
					break;
				}
				case BINDBOOLEAN:
				{
					jboolean cch  = (jboolean)*(char*)Advance(outputLog[x].pointerRef);
					env->SetBooleanField((jobject)outputLog[x].theObjectRef,fieldID,cch);
					break;
				}
                                caseBINDDOUBLE:
				{
					int bytestep = 0;
					double* dbl_ptr = *(double*)Advance(outputLog[x].pointerRef);
					jbyteArray jb = NULL;
					jbyte  prim[8];
					unsigned char*  tick = NULL;
					bool neg = false;
					jboolean copy = JNI_FALSE;

					if ( outputLog[x].clength != 8 ) {
                                            env->ThrowNew(env->FindClass("java/lang/SynSQLException"),"double variable is not the right size");
					}
					jb = env->NewByteArray(outputLog[x].clength);
					tick = (unsigned char*)prim;

					if ( jb == NULL ) {
						if ( !env->ExceptionOccurred() ) {
                                                    env->ThrowNew(env->FindClass("java/lang/OutOfMemoryError"),"binary fetch");
						}
						break;
					}
					env->SetByteArrayRegion(jb,0,outputLog[x].clength,dbl_ptr);
					env->SetObjectField((jobject)outputLog[x].theObjectRef,fieldID,jb);
					break;
				}
				case BINDBINARY:
				case BINDBLOB:
				case BINDTEXT:
				case BINDJAVA:
				{
					jbyteArray jb = NULL;
					jbyte*  prim = NULL;
					jboolean  copy = JNI_FALSE;
					jb = env->NewByteArray(outputLog[x].clength);

					if ( jb == NULL ) {
						if ( !env->ExceptionOccurred() ) {
                    		env->ThrowNew(env->FindClass("java/lang/OutOfMemoryError"),"binary fetch");
						}
						break;
					}
					
					env->SetByteArrayRegion(jb,0,outputLog[x].clength,(jbyte*)Advance(outputLog[x].pointerRef));
					env->SetObjectField((jobject)outputLog[x].theObjectRef,fieldID,jb);
					break;
				}
				case BINDDATE:
				{
/*  do it this way for endianess, java is always big endian  */
					int bytestep = 0;
					int64_t movetime = (int64_t)*(double*)Advance(outputLog[x].pointerRef);
					jbyteArray jb = NULL;
					signed char  prim[8];
					unsigned char*  tick = NULL;
					bool neg = false;
					jboolean copy = JNI_FALSE;

					if ( outputLog[x].clength != 8 ) {
                    		env->ThrowNew(env->FindClass("java/lang/SynSQLException"),"date variable is not the right size");
					}
					
					jb = env->NewByteArray(outputLog[x].clength);
					tick = (unsigned char*)prim;
				
					if ( jb == NULL ) {
						if ( !env->ExceptionOccurred() ) {
                                                    env->ThrowNew(env->FindClass("java/lang/OutOfMemoryError"),"binary fetch");
						}
						break;
					}

					movetime += (10957 * 86400);
					movetime *= 1000;

					if ( movetime < 0 ) {
						neg = -1;
						movetime *= -1;
					}
					for(bytestep=7;bytestep>0;bytestep--) {
						int64_t div = (int64_t)pow((double)256,(double)bytestep);
						*tick++ = (char)(movetime / div);
						movetime = (movetime % div);
					}
					*tick = (char)movetime;
					if ( neg < 0 ) *prim |= 0x80;
					env->SetByteArrayRegion(jb,0,outputLog[x].clength,prim);
					env->SetObjectField((jobject)outputLog[x].theObjectRef,fieldID,jb);
					break;
				}
                                case BINDLONG:
				{
/*  do it this way for endianess, java is always big endian  */
					int bytestep = 0;
					int64_t movetime = *(int64_t*)Advance(outputLog[x].pointerRef);
					jbyteArray jb = NULL;
					jbyte  prim[8];
					unsigned char*  tick = NULL;
					bool neg = false;
					jboolean copy = JNI_FALSE;

					if ( outputLog[x].clength != 8 ) {
                                            env->ThrowNew(env->FindClass("java/lang/SynSQLException"),"long variable is not the right size");
					}
					jb = env->NewByteArray(outputLog[x].clength);
					tick = (unsigned char*)prim;

					if ( jb == NULL ) {
						if ( !env->ExceptionOccurred() ) {
                                                    env->ThrowNew(env->FindClass("java/lang/OutOfMemoryError"),"binary fetch");
						}
						break;
					}

					if ( movetime < 0 ) {
						neg = -1;
						movetime *= -1;
					}
					for(bytestep=7;bytestep>0;bytestep--) {
						int64_t div = (int64_t)pow((double)256,(double)bytestep);
						*tick++ = (char)(movetime / div);
						movetime = (movetime % div);
					}
					*tick = (char)movetime;
					if ( neg < 0 ) prim[0] |= (jbyte)0x80;

					env->SetByteArrayRegion(jb,0,outputLog[x].clength,prim);
					env->SetObjectField((jobject)outputLog[x].theObjectRef,fieldID,jb);
					break;
				}
				case BINDSTREAM:
                                case BINDDIRECT:
                                {
					break;
				/* do nothing passing done though stream already */
				}	
				default:
	 				DelegateError("PASSING","results no passed, type error",745);
					break;
			}
	}
	return 0;
}

long PostgresStmtManager::StreamExec(char* statement)
{
    PGStreamExec(theConn,statement);
    return CheckLowerError();
}

long PostgresStmtManager::PipeConnect(int pipeid,int streamid,pipefunc func)
{
    int size = ( streamid == 0 || streamid == 1 ) ? 8192 : 0;
    Pipe pipe = PGPipeConnect(theConn,pipeid,streamid,size,func);
    if ( streamid == 0 ) stdpipein = pipe;
    else if ( streamid == 1 ) stdpipeout = pipe;
    else if ( streamid < 52 ) {
        pipes[streamid-2] = pipe;
    }
    return 0;
}

long PostgresStmtManager::PipeDisconnect( int streamid ) 
{
       Pipe p = NULL;
     if ( streamid == 0 ) {
        p = stdpipein;
        stdpipein = NULL;
     } else if ( streamid == 1 ) {
        p = stdpipeout;
        stdpipeout = NULL;
     } else if ( streamid < 52 ) {
        p = pipes[streamid-2];
        pipes[streamid - 2] = NULL;
     }
     if ( p != NULL ) PGPipeDisconnect(theConn,p);
    return 0;
}



#endif
