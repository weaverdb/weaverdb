
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
StreamOutValue(InputOutput* dest, Datum val, Oid type) {
    if ( ISINDIRECT(val) ) {
        int buf_sz = (sizeof_max_tuple_blob() * 5);
        void* buffer = palloc(buf_sz);

        int   length = 0;
        Datum pointer = open_read_pipeline_blob(val,false);
        while (read_pipeline_segment_blob(pointer,buffer,&length,buf_sz) ) {
            Assert(length > 0);
            if ( dest->transfer(dest->userargs,type,buffer,length) == COMM_ERROR) {
                elog(ERROR,"piping error occurred");
            }
        }
        close_read_pipeline_blob(pointer);
        pfree(buffer);
    } else {
        if ( dest->transfer(dest->userargs,type,VARDATA((bytea*)val),VARSIZE((bytea*)val) - VARHDRSZ)  ==COMM_ERROR ) {
            elog(ERROR,"piping error occurred");
        }
    }    
}

static void
ConvertValueToText(InputOutput* output,Oid type,int4 typmod, Datum val) {
        Oid             foutoid,
                        typelem;
        char           *texto;

        if (!getTypeOutAndElem(type, &foutoid, &typelem)) {
            coded_elog(ERROR,108,"type conversion error");
        }
        texto = (char *) (fmgr(foutoid, val, typelem,typmod));

        output->transfer(output->userargs, VARCHAROID,texto,strlen(texto));
}

static void
BinaryCopyOutValue(InputOutput* output, Form_pg_attribute desc, Datum value) {
    if (desc->attlen > 0) {
        if (desc->attbyval) {
            output->transfer(output->userargs, desc->atttypid, (void *)&(value), desc->attlen);
        } else {
            output->transfer(output->userargs, desc->atttypid, (void *)DatumGetPointer(value), desc->attlen);
        }
    } else {
        if ( ISINDIRECT(value) ) {
            int size = sizeof_indirect_blob(value);
            int length = 0;
            int moved = 0;
            int buf_sz = (sizeof_max_tuple_blob() * 5);
            void* buffer = palloc(buf_sz);

            Datum pointer = open_read_pipeline_blob(value,false);
            char* target = palloc(size);
            while (read_pipeline_segment_blob(pointer,buffer,&length,buf_sz) ) {
                Assert(length > 0);
                output->transfer(output->userargs, desc->atttypid, buffer, length);
            }
            close_read_pipeline_blob(pointer);
            pfree(buffer);
        } else {
            output->transfer(output->userargs, desc->atttypid, VARDATA(value), VARSIZE(value) - 4);
        }
    }
}

static void
DirectIntCopyValue(InputOutput* output, Datum value) {
    int32 val = DatumGetInt32(value);
    output->transfer(output->userargs, INT4OID, &val, 4);
}

static void
DirectFloatCopyValue(InputOutput* output, Datum value) {
    float32 val = DatumGetFloat32(value);
    output->transfer(output->userargs, FLOAT4OID, &val, 4);
}

static void
DirectCharCopyValue(InputOutput* output, Datum value) {
    char val = DatumGetChar(value);
    output->transfer(output->userargs, CHAROID, &val, 1);
}
 
static void
IndirectLongCopyValue(InputOutput* output, Datum value) {
    output->transfer(output->userargs, INT8OID, DatumGetPointer(value), 8);
}

static void
DirectLongCopyValue(InputOutput* output,long value) {
    output->transfer(output->userargs, INT8OID, &value, 8);
}

static void
IndirectDoubleCopyValue(InputOutput* output, Datum value) {
    output->transfer(output->userargs, FLOAT8OID, DatumGetPointer(value), 8);
}

static void
DirectDoubleCopyValue(InputOutput* output, double value) {
    output->transfer(output->userargs, FLOAT8OID, &value, 8);
}

bool
TransferToRegistered(InputOutput* output, Form_pg_attribute desc, Datum value) {
    if (desc->atttypid != output->varType) {
        switch (output->varType) {
            case STREAMINGOID:
                StreamOutValue(output,value,desc->atttypid);
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
                else if (desc->atttypid == BOOLOID) DirectIntCopyValue(output,Int32GetDatum((value) ? 1 : 0));
                else if ( desc->atttypid == INT8OID ) {
                    if ( DatumGetInt64(value) > 0x7fffffff ) return false;
                    else DirectIntCopyValue(output,value);
                }
                else return false;
                break;
            case BOOLOID:
                if (desc->atttypid == INT4OID) DirectCharCopyValue(output,CharGetDatum((value == 0) ? FALSE : TRUE));
                else return false;
                break;
            case INT8OID:
                if (desc->atttypid == XIDOID) IndirectLongCopyValue(output,value);
                else if (desc->atttypid == OIDOID) IndirectLongCopyValue(output,value);
                else if (desc->atttypid == INT4OID) DirectLongCopyValue(output,DatumGetInt32(value));
                else return false;
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
                StreamOutValue(output,value,desc->atttypid);
                break;
            default:
                return false;
        }
    }
    return true;
}

