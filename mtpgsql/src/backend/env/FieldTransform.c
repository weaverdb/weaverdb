
#include <stdlib.h>
#include <unistd.h>

#include <sys/types.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <assert.h>

#include "env/connectionutil.h"
#include "env/WeaverConnection.h"

#include "access/heapam.h"
#include "access/blobstorage.h"
#include "catalog/pg_attribute.h"

#include "miscadmin.h"
#include "access/printtup.h"
#include "access/htup.h"
#include "parser/gramparse.h"
#include "parser/parse_coerce.h"
#include "parser/parserinfo.h"

static void
StreamOutValue(Output* output,Datum val) {
    CommBuffer* pipe = output->target;
    if ( ISINDIRECT(val) ) {
        int buf_sz = (sizeof_max_tuple_blob() * 5);
        void* buffer = palloc(buf_sz);

        int   length = 0;
        Datum pointer = open_read_pipeline_blob(val,false);
        while (read_pipeline_segment_blob(pointer,buffer,&length,buf_sz) ) {
            Assert(length > 0);
            if ( pipe->pipe(pipe->args,buffer,0,length) == COMM_ERROR) {
                elog(ERROR,"piping error occurred");
            }
        }
        close_read_pipeline_blob(pointer);
        pfree(buffer);
    } else {
        if ( pipe->pipe(pipe->args,VARDATA((bytea*)val),0,VARSIZE((bytea*)val) - VARHDRSZ)  ==COMM_ERROR ) {
            elog(ERROR,"piping error occurred");
        }
    }    
}

static void
ConvertValueToText(Output* output,Oid type,int4 typmod, Datum val) {
        Oid             foutoid,
                        typelem;
        char           *texto;
        int             textlen;
        char*           target = output->target;

        if (!getTypeOutAndElem(type, &foutoid, &typelem)) {
            coded_elog(ERROR,108,"type conversion error");
        }
        texto = (char *) (fmgr(foutoid, val, typelem,typmod));

        textlen = strlen(texto);
        if (textlen > output->size) {
            output->freeable = MemoryContextAlloc(MemoryContextGetEnv()->TransactionCommandContext,textlen);
            *(void**)output->target = output->freeable;
            target = output->freeable;
        } 
        *output->length = textlen;
        memcpy(target, texto, textlen);
}

static void
BinaryCopyOutValue(Output* output, Form_pg_attribute desc, Datum value) {
    char* target = output->target;
    
    if (desc->attlen > 0) {
        if (desc->attlen > output->size) {
            output->freeable = MemoryContextAlloc(MemoryContextGetEnv()->TransactionCommandContext,desc->attlen);
            *(void**)output->target = output->freeable;
            target = output->freeable;
        }
        if (desc->attbyval) {
                *output->length = desc->attlen;
                memcpy(target, (void *)&(value), desc->attlen);
        } else {
                *output->length = desc->attlen;
                memcpy(target, (void *)DatumGetPointer(value), desc->attlen);
        }
    } else {
        if ( ISINDIRECT(value) ) {
            int size = sizeof_indirect_blob(value);
            int length = 0;
            int moved = 0;

            if (size > output->size ) {
                output->freeable = MemoryContextAlloc(MemoryContextGetEnv()->TransactionCommandContext,size);
                *(void**)output->target = output->freeable;
                target = output->freeable;
            } 

            Datum pointer = open_read_pipeline_blob(value,false);
            while (read_pipeline_segment_blob(pointer,target,&length,output->size - moved) ) {
                Assert(length > 0);
                moved += length;
                target += length;
            }

            close_read_pipeline_blob(pointer);
      /* last iteration of pipeline read needs to be added to length */
            *output->length = moved + length;
        } else {
            if (VARSIZE(value) - 4 > output->size) {
                output->freeable = MemoryContextAlloc(MemoryContextGetEnv()->TransactionCommandContext,VARSIZE(value) - 4);
                *(void**)output->target = output->freeable;
                target = output->freeable;
            }
            *output->length = VARSIZE(value) - 4;
            memcpy(target, VARDATA(value), VARSIZE(value) - 4);
        }
    }
}

static void
DirectIntCopyValue(Output* output, Datum value) {
    *(int32_t*)output->target = DatumGetInt32(value);
    *output->length = 4;
}

static void
DirectFloatCopyValue(Output* output, Datum value) {
    *(float32*)output->target = DatumGetFloat32(value);
    *output->length = 4;
}

static void
DirectCharCopyValue(Output* output, Datum value) {
    *(char*)output->target = DatumGetChar(value);
    *output->length = 1;
}
 
static void
IndirectLongCopyValue(Output* output, Datum value) {
    *(int64_t *) output->target = *(int64_t *)DatumGetPointer(value);
    *output->length = 8;
}

static void
IndirectDoubleCopyValue(Output* output, Datum value) {
    *(double *) output->target = *(double *)DatumGetPointer(value);
    *output->length = 8;
}

static void
DirectDoubleCopyValue(Output* output, double value) {
    *(double *) output->target = value;
    *output->length = 8;
}

bool
TransferValue(Output* output, Form_pg_attribute desc, Datum value) {
    if ( *output->notnull < 0 ) {
        elog(ERROR,"Output variable is no longer valid");
    }
    if (desc->atttypid != output->type) {
        switch (output->type) {
            case STREAMINGOID:
                StreamOutValue(output,value);
                break;
            case CHAROID:
            case VARCHAROID:
                ConvertValueToText(output,desc->atttypid,desc->atttypmod,value);
                break;
            case TEXTOID:
            case BPCHAROID:
            case BYTEAOID:
            case BLOBOID:
                BinaryCopyOutValue(output,desc,value);
                break;
            case INT4OID:
                if (desc->atttypid == CONNECTOROID ) DirectIntCopyValue(output,value);
                else if (desc->atttypid == BOOLOID) DirectIntCopyValue(output,(value) ? 1 : 0);
                else return false;
                break;
            case BOOLOID:
                if (desc->atttypid == INT4OID) DirectCharCopyValue(output,CharGetDatum((value == 0) ? FALSE : TRUE));
                else return false;
                break;
            case INT8OID:
                if (desc->atttypid == XIDOID) IndirectLongCopyValue(output,value);
                else if (desc->atttypid == OIDOID) IndirectLongCopyValue(output,value);
                break;
            case FLOAT8OID:
                if (desc->atttypid == FLOAT4OID) DirectDoubleCopyValue(output,(double)*(float*)DatumGetPointer(value));
                else return false;
                break;
            default:
                return false;
        }
    } else {
        switch (desc->atttypid) {
            case BOOLOID:
            case CHAROID:
                DirectCharCopyValue(output,value);
                break;
            case INT4OID:
                DirectIntCopyValue(output,value);
                break;
            case FLOAT4OID:
                DirectFloatCopyValue(output,value);
                break;
            case TIMESTAMPOID:
            case FLOAT8OID:
                IndirectDoubleCopyValue(output,value);
                break;
            case INT8OID:
                IndirectLongCopyValue(output,value);
                break;
            case BLOBOID:
            case TEXTOID:
            case VARCHAROID:
            case BPCHAROID:
            case BYTEAOID:
            case JAVAOID:
                BinaryCopyOutValue(output,desc,value);
                break;
            case STREAMINGOID: 
                StreamOutValue(output,value);
                break;
            default:
                return false;
        }
    }
    return true;
}

