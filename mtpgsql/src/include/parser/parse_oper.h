/*-------------------------------------------------------------------------
 *
 * catalog_utils.h
 *
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * $Id: parse_oper.h,v 1.1.1.1 2006/08/12 00:22:22 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */
#ifndef PARSE_OPER_H
#define PARSE_OPER_H

#include "access/htup.h"

typedef HeapTuple Operator;

PG_EXTERN Oid	any_ordering_op(Oid restype);
PG_EXTERN Oid	oprid(Operator op);
PG_EXTERN Operator oper(char *op, Oid arg1, Oid arg2, bool noWarnings);
PG_EXTERN Operator right_oper(char *op, Oid arg);
PG_EXTERN Operator left_oper(char *op, Oid arg);

#endif	 /* PARSE_OPER_H */
