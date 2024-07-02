/*-------------------------------------------------------------------------
 *
 * parse_type.h
 *
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 *
 *-------------------------------------------------------------------------
 */
#ifndef PARSE_TYPE_H
#define PARSE_TYPE_H

#include "access/htup.h"

typedef HeapTuple Type;

PG_EXTERN bool typeidIsValid(Oid id);
PG_EXTERN Type typeidType(Oid id);
PG_EXTERN Type typenameType(char *s);
PG_EXTERN char *typeidTypeName(Oid id);
PG_EXTERN Oid	typeTypeId(Type tp);
PG_EXTERN int16 typeLen(Type t);
PG_EXTERN bool typeByVal(Type t);
PG_EXTERN char *typeTypeName(Type t);
PG_EXTERN char typeTypeFlag(Type t);
PG_EXTERN Datum stringTypeDatum(Type tp, char *string, int32 atttypmod);
PG_EXTERN Oid	typeidTypeRelid(Oid type_id);
PG_EXTERN Oid	typeTypeRelid(Type typ);
PG_EXTERN Oid	typeTypElem(Type typ);
PG_EXTERN Oid	GetArrayElementType(Oid typearray);
PG_EXTERN Oid	typeInfunc(Type typ);
PG_EXTERN Oid	typeOutfunc(Type typ);

#define ISCOMPLEX(typeid) (typeidTypeRelid(typeid) != InvalidOid)

#endif	 /* PARSE_TYPE_H */
