/*-------------------------------------------------------------------------
 *
 * wrapdatum.c
 *	  Functions for wrapping data in a variable length field
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *
 *-------------------------------------------------------------------------
 */
#include <ctype.h>

#include "postgres.h"

#include "utils/builtins.h"
#include "fmgr.h"
#include "utils/wrapdatum.h"
#include "utils/syscache.h"
#include "catalog/pg_type.h"


wrapped *
wrappedin(wrapped_datum * input)
{
	bytea	   *result;
	HeapTuple	typeTuple;
        int         varsize,datumsize;
        void *      data;
	char*		tp;

	typeTuple = SearchSysCacheTuple(TYPEOID,
                                            ObjectIdGetDatum(input->type),
                                            0, 0, 0);
	if (HeapTupleIsValid(typeTuple))
	{
		Form_pg_type pt = (Form_pg_type) GETSTRUCT(typeTuple);

		if ( pt->typbyval ) {
		    varsize = sizeof(Datum);
		    data = &input->value;	
		} else if ( pt->typlen >= 0) {
                    varsize = pt->typlen;
                    data = DatumGetPointer(input->value); 
                } else {
                    varsize = VARSIZE(DatumGetPointer(input->value)) - VARHDRSZ;
                    data = VARDATA(DatumGetPointer(input->value));
                }
	}

	datumsize = varsize + LONGALIGN(VARHDRSZ) + sizeof(Oid);

	tp = palloc(datumsize);

	result = (bytea*)tp;

	tp += LONGALIGN(VARHDRSZ);
	*((Oid*)tp) = input->type;
	tp += sizeof(Oid);
        memmove(tp,data,varsize);

	SETVARSIZE(result,datumsize);		/* varlena? */

	return result;
}

/*
 *		wrapout			- wraps a datum in a variable length field
 *
 */

wrapped_datum *
wrappedout(wrapped * input)
{

        HeapTuple           typeTuple;
        char*               data;
        wrapped_datum	   *result = NULL;
	Oid		   type;

        data = (char*)input;
	data += LONGALIGN(VARHDRSZ);
	type = *(Oid*)data;
	data += sizeof(Oid);

 	typeTuple = SearchSysCacheTuple(TYPEOID,
                                            ObjectIdGetDatum(type),
                                            0, 0, 0);
	if (HeapTupleIsValid(typeTuple))
	{
            Form_pg_type pt = (Form_pg_type) GETSTRUCT(typeTuple);

            if ( pt->typbyval ) {
                result = palloc(sizeof(wrapped_datum));
                
                result->type = type;
                result->value = *(Datum*)data;
	    } else if ( pt->typlen >= 0 ) {
                result = palloc(MAXALIGN(sizeof(wrapped_datum)) + pt->typlen);
                
                result->type = type;
		char* scratch = ((char*)result) + sizeof(wrapped_datum);
		memmove(scratch,data,pt->typlen);
                result->value = PointerGetDatum(scratch);
            } else {
                result = palloc(MAXALIGN(sizeof(wrapped_datum)) + VARSIZE(input) - sizeof (Oid) - LONGALIGN(VARHDRSZ) + VARHDRSZ);
                char* scratch = ((char*)result);
                scratch += MAXALIGN(sizeof(wrapped_datum));
                result->type = type;
                memmove(scratch + VARHDRSZ,data,VARSIZE(input) - sizeof(Oid) -
			 LONGALIGN(VARHDRSZ));
                SETVARSIZE((wrapped*)scratch,VARSIZE(input) - sizeof(Oid) - LONGALIGN(VARHDRSZ) + VARHDRSZ);
                result->value = PointerGetDatum(scratch);
            }
	} else {
                return NULL;
        }
       
	return result;
}

char*   
wrappedtotext(wrapped* input) {
 		
        HeapTuple           typeTuple;

        wrapped_datum* conv = wrappedout(input);
        
        if ( conv == NULL ) {
            char* out = palloc(1);
            *out = 0x00;
            return out;
        }

 	typeTuple = SearchSysCacheTuple(TYPEOID,
                                            ObjectIdGetDatum(conv->type),
                                            0, 0, 0);
	if (HeapTupleIsValid(typeTuple))
	{
            char *    converted;

            Form_pg_type pt = (Form_pg_type) GETSTRUCT(typeTuple);

            converted = (char *) fmgr(pt->typoutput, conv->value);
            
            pfree(conv);

            return converted;
	}
        pfree(conv);

        return NULL;
}

