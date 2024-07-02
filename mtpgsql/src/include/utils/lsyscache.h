/*-------------------------------------------------------------------------
 *
 * lsyscache.h
 *	  Convenience routines for common queries in the system catalog cache.
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 *
 *-------------------------------------------------------------------------
 */
#ifndef LSYSCACHE_H
#define LSYSCACHE_H

#include "access/htup.h"

PG_EXTERN bool op_class(Oid opid, Oid opclass, Oid amopid);
PG_EXTERN char *get_attname(Oid relid, AttrNumber attnum);
PG_EXTERN AttrNumber get_attnum(Oid relid, char *attname);
PG_EXTERN Oid	get_atttype(Oid relid, AttrNumber attnum);
PG_EXTERN bool get_attisset(Oid relid, char *attname);
PG_EXTERN int32 get_atttypmod(Oid relid, AttrNumber attnum);
PG_EXTERN double get_attdisbursion(Oid relid, AttrNumber attnum,
				  double min_estimate);
PG_EXTERN RegProcedure get_opcode(Oid opid);
PG_EXTERN char *get_opname(Oid opid);
PG_EXTERN bool op_mergejoinable(Oid opid, Oid ltype, Oid rtype,
				 Oid *leftOp, Oid *rightOp);
PG_EXTERN Oid	op_hashjoinable(Oid opid, Oid ltype, Oid rtype);
PG_EXTERN Oid	get_commutator(Oid opid);
PG_EXTERN HeapTuple get_operator_tuple(Oid opno);
PG_EXTERN Oid	get_negator(Oid opid);
PG_EXTERN RegProcedure get_oprrest(Oid opid);
PG_EXTERN RegProcedure get_oprjoin(Oid opid);
PG_EXTERN Oid	get_func_rettype(Oid funcid);
PG_EXTERN int	get_relnatts(Oid relid);
PG_EXTERN char *get_rel_name(Oid relid);
PG_EXTERN struct varlena *get_relstub(Oid relid, int no, bool *islast);
PG_EXTERN Oid	get_ruleid(char *rulename);
PG_EXTERN Oid	get_eventrelid(Oid ruleid);
PG_EXTERN int16 get_typlen(Oid typid);
PG_EXTERN bool get_typbyval(Oid typid);
PG_EXTERN Datum get_typdefault(Oid typid);

#endif	 /* LSYSCACHE_H */
