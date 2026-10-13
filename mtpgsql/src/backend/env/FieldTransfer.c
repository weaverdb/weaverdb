/*-------------------------------------------------------------------------
 *
 * FieldTransfer.c
 *	  translation tunnel out of weaver
 *
 * Copyright (c) 2000-2024, Myron Scott  <myron@weaverdb.org>
 *
 *
 * IDENTIFICATION
 *
 *
 *-------------------------------------------------------------------------
 */


#include <unistd.h>


#include <ctype.h>

#include <assert.h>

#include "c.h"

#include "env/properties.h"
#include "env/WeaverConnection.h"

#include "access/heapam.h"
#include "access/blobstorage.h"
#include "catalog/pg_attribute.h"

#include <limits.h>

#include "miscadmin.h"
#include "access/printtup.h"
#include "access/htup.h"
#include "parser/gramparse.h"
#include "parser/parse_coerce.h"
#include "parser/parserinfo.h"
#include "utils/timestamp.h"
#include "utils/date.h"

static int
StreamOutValue(InputOutput* dest, Datum val, Oid type) {
    int result = 0;
    if ( ISINDIRECT(val) ) {
        int buf_sz = (sizeof_max_tuple_blob() * 5);
        void* buffer = palloc(buf_sz);

        int   length = 0;
        Datum pointer = open_read_pipeline_blob(val,false);
        while (read_pipeline_segment_blob(pointer,buffer,&length,buf_sz) ) {
            Assert(length > 0);
            int sent = 0;
            while (sent < length) {
                result = dest->transfer(dest->userargs,type,buffer,length);
                if (result < 0) {
                    return result;
                } else {
                    sent += result;
                }
            }
        }
        close_read_pipeline_blob(pointer);
        pfree(buffer);
    } else {
        result = dest->transfer(dest->userargs,type,VARDATA(DatumGetPointer(val)),VARSIZE(DatumGetPointer(val)) - VARHDRSZ);
    }
    dest->transfer(dest->userargs,type,NULL,CLOSE_OP);
    return result;
}

static int
ConvertValueToText(InputOutput* output,Oid type,int4 typmod, Datum val) {
        Oid             foutoid,
                        typelem;
        char           *texto;

        if (!getTypeOutAndElem(type, &foutoid, &typelem)) {
            coded_elog(ERROR,108,"type conversion error");
        }
        texto = (char *) (fmgr(foutoid, val, typelem,typmod));

        return output->transfer(output->userargs, VARCHAROID,texto,strlen(texto));
}

static int
BinaryCopyOutValue(InputOutput* output, Form_pg_attribute desc, Datum value) {
    if (desc->attlen > 0) {
        if (desc->attbyval) {
            return output->transfer(output->userargs, desc->atttypid, (void *)&(value), desc->attlen);
        } else {
            return output->transfer(output->userargs, desc->atttypid, (void *)DatumGetPointer(value), desc->attlen);
        }
    } else {
        if ( ISINDIRECT(value) ) {
            bytea* pointer = rebuild_indirect_blob(value);
            int size = output->transfer(output->userargs, desc->atttypid, VARDATA(pointer), VARSIZE(pointer) - VARHDRSZ);
            pfree(pointer);
            return size;
        } else {
            return output->transfer(output->userargs, desc->atttypid, VARDATA(value), VARSIZE(value) - VARHDRSZ);
        }
    }
}

static int
DirectShortCopyValue(InputOutput* output, Datum value) {
    int32 val = DatumGetInt32(value);
    return output->transfer(output->userargs, INT2OID, &val, 2);
}

static int
DirectIntCopyValue(InputOutput* output, Datum value) {
    int32 val = DatumGetInt32(value);
    return output->transfer(output->userargs, INT4OID, &val, 4);
}

static int
DirectFloatCopyValue(InputOutput* output, Datum value) {
    float32 val = DatumGetFloat32(value);
    return output->transfer(output->userargs, FLOAT4OID, &val, 4);
}

static int
DirectCharCopyValue(InputOutput* output, Datum value) {
    char val = DatumGetChar(value);
    return output->transfer(output->userargs, CHAROID, &val, 1);
}

static int
DirectBoolCopyValue(InputOutput* output, Datum value) {
    char val = DatumGetChar(value);
    return output->transfer(output->userargs, BOOLOID, &val, 1);
}
  
static int
IndirectDateCopyValue(InputOutput* output, Datum value) {
    return output->transfer(output->userargs, TIMESTAMPOID, DatumGetPointer(value), 8);
}

static int
DirectDateCopyValue(InputOutput* output,long value) {
    return output->transfer(output->userargs, TIMESTAMPOID, &value, 8);
}

static int
IndirectLongCopyValue(InputOutput* output, Datum value) {
    return output->transfer(output->userargs, INT8OID, DatumGetPointer(value), 8);
}

static int
DirectLongCopyValue(InputOutput* output,long value) {
    return output->transfer(output->userargs, INT8OID, &value, 8);
}

static int
IndirectDoubleCopyValue(InputOutput* output, Datum value) {
    return output->transfer(output->userargs, FLOAT8OID, DatumGetPointer(value), 8);
}

static int
IndirectFloatCopyValue(InputOutput* output, Datum value) {
    return output->transfer(output->userargs, FLOAT4OID, DatumGetPointer(value), 4);
}

static int
DirectDoubleCopyValue(InputOutput* output, double value) {
    return output->transfer(output->userargs, FLOAT8OID, &value, 8);
}

static bool
IsIntegerOid(Oid type)
{
    return type == INT2OID || type == INT4OID || type == INT8OID;
}

static int64
ReadIntegerColumn(Form_pg_attribute desc, Datum value)
{
    switch (desc->atttypid) {
        case INT2OID:
            return (int64) DatumGetInt16(value);
        case INT4OID:
            return (int64) DatumGetInt32(value);
        case INT8OID:
            if (desc->attbyval)
                return (int64) DatumGetInt64(value);
            return *(int64 *) DatumGetPointer(value);
        default:
            return 0;
    }
}

/*
 * Move an int2/int4/int8 column into the registered link width.
 * Narrowing that does not fit raises a clear error instead of returning
 * the generic "conversion not implemented" failure.
 */
static int
TransferInteger(InputOutput *output, Form_pg_attribute desc, Datum value)
{
    int64 wide = ReadIntegerColumn(desc, value);

    if (output->varType == INT2OID) {
        if (wide < SHRT_MIN || wide > SHRT_MAX)
            coded_elog(ERROR, 107, "integer value %ld is out of range for int2", (long) wide);
        return DirectShortCopyValue(output, Int32GetDatum((int32) (int16) wide));
    }
    if (output->varType == INT4OID) {
        if (wide < (int64) INT_MIN || wide > (int64) INT_MAX)
            coded_elog(ERROR, 107, "integer value %ld is out of range for int4", (long) wide);
        return DirectIntCopyValue(output, Int32GetDatum((int32) wide));
    }
    return DirectLongCopyValue(output, (long) wide);
}

bool
TransferColumnName(InputOutput* output, Form_pg_attribute desc) {
    output->transfer(output->userargs, NAMEOID,NameStr(desc->attname),strlen(NameStr(desc->attname)));
    return true;
}

bool
TransferToRegistered(InputOutput* output, Form_pg_attribute desc, Datum value, bool isnull) {
    int result = 0;
    if (isnull) {
        output->transfer(output->userargs,desc->atttypid,NULL,NULL_VALUE);
    } else if (output->varType == 0 || desc->atttypid == output->varType) {
        switch (desc->atttypid) {
            case BOOLOID:
                result = DirectBoolCopyValue(output,value);
                break;
            case CHAROID:
                result = DirectCharCopyValue(output,value);
                break;
            case INT2OID:
                result = DirectShortCopyValue(output,value);
                break;
            case INT4OID:
                result = DirectIntCopyValue(output,value);
                break;
            case FLOAT4OID:
                result = desc->attbyval ? DirectFloatCopyValue(output,value) : IndirectFloatCopyValue(output,value);
                break;
            case FLOAT8OID:
                result = desc->attbyval ? DirectDoubleCopyValue(output,value) : IndirectDoubleCopyValue(output,value);
                break;
            case TIMESTAMPOID:
                result = desc->attbyval ? DirectDateCopyValue(output,value) : IndirectDateCopyValue(output,value);
                break;
            case INT8OID:
            case XIDOID:
            case OIDOID:
            case REGPROCOID:
                result = desc->attbyval ? DirectLongCopyValue(output,value) : IndirectLongCopyValue(output,value);
                break;
            case BLOBOID:
            case TEXTOID:
            case VARCHAROID:
            case BPCHAROID:
            case BYTEAOID:
            case JAVAOID:
                result = BinaryCopyOutValue(output,desc,value);
                break;
            case STREAMINGOID: 
                result = StreamOutValue(output,value,desc->atttypid);
                break;
            case NAMEOID:
                result = ConvertValueToText(output, desc->atttypid, desc->atttypmod, value);
                break;
            default:
                return false;
        }
    } else {
        switch (output->varType) {
            case STREAMINGOID:
                result = StreamOutValue(output,value,desc->atttypid);
                break;
            case CHAROID:
            case VARCHAROID:
                result = ConvertValueToText(output,desc->atttypid,desc->atttypmod,value);
                break;
            case TEXTOID:
            case BPCHAROID:
            case BYTEAOID:
            case BLOBOID:
                result = BinaryCopyOutValue(output,desc,value);
                break;
            case INT2OID:
                if (!IsIntegerOid(desc->atttypid))
                    return false;
                result = TransferInteger(output, desc, value);
                break;
            case INT4OID:
                if (desc->atttypid == CONNECTOROID ) result = DirectIntCopyValue(output,value);
                else if (desc->atttypid == BOOLOID) result = DirectIntCopyValue(output,Int32GetDatum((value) ? 1 : 0));
                else if (IsIntegerOid(desc->atttypid))
                    result = TransferInteger(output, desc, value);
                else return false;
                break;
            case BOOLOID:
                if (desc->atttypid == INT4OID) result = DirectBoolCopyValue(output,CharGetDatum((value == 0) ? FALSE : TRUE));
                else return false;
                break;
            case INT8OID:
                if (IsIntegerOid(desc->atttypid))
                    result = TransferInteger(output, desc, value);
                else
                    result = desc->attbyval ? DirectLongCopyValue(output,value) : IndirectLongCopyValue(output,value);
                break;
            case TIMESTAMPOID:
                if (desc->atttypid == DATEOID) {
                    Timestamp *ts = date_timestamp(DatumGetInt32(value));
                    result = IndirectDateCopyValue(output, PointerGetDatum(ts));
                    pfree(ts);
                } else
                    return false;
                break;
            case DATEOID:
                if (desc->atttypid == TIMESTAMPOID) {
                    DateADT date;
                    int32 raw;
                    if (desc->attbyval) {
                        union { Datum d; Timestamp ts; } bits;
                        bits.d = value;
                        date = timestamp_date(&bits.ts);
                    } else
                        date = timestamp_date((Timestamp *) DatumGetPointer(value));
                    raw = date;
                    result = output->transfer(output->userargs, DATEOID, &raw, sizeof(raw));
                } else
                    return false;
                break;
            case FLOAT8OID:
                if (desc->atttypid == FLOAT4OID) {
                    float32 pass = DatumGetFloat32(value);
                    result = IndirectDoubleCopyValue(output,(double)*(float*)pass);
                } else if (desc->atttypid == FLOAT8OID) {
                    result = desc->attbyval ? DirectDoubleCopyValue(output,(double)value) : IndirectDoubleCopyValue(output,value);
                }
                else return false;
                break;
            default:
                return false;
        }
    }
    return result >= 0;
}

