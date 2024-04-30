/*-------------------------------------------------------------------------
 *
 * wrapdatum.h
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 *-------------------------------------------------------------------------
 */
#ifndef WRAPDATUM_H
#define WRAPDATUM_H

#include "c.h"

#include "fmgr.h"

typedef struct wrapped {
    Oid type;
    Datum value;
} wrapped_datum;

PG_EXTERN wrapped*   wrappedin(wrapped_datum* obj);
PG_EXTERN wrapped_datum*   wrappedout(wrapped* datum);
PG_EXTERN char*   wrappedtotext(wrapped* datum);

#endif
