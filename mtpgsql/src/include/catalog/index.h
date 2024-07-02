/*-------------------------------------------------------------------------
 *
 * index.h
 *	  prototypes for index.c.
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 *
 *-------------------------------------------------------------------------
 */
#ifndef INDEX_H
#define INDEX_H

#include "access/itup.h"
#include "nodes/execnodes.h"


/* Typedef for callback function for IndexBuildHeapScan */
typedef void (*IndexBuildCallback) (Relation index,
												HeapTuple htup,
												Datum *attdata,
												char *nulls,
												bool tupleIsAlive,
												void *state);
                                                                                                
PG_EXTERN Form_pg_am AccessMethodObjectIdGetForm(Oid accessMethodObjectId);

PG_EXTERN void UpdateIndexPredicate(Oid indexoid, Node *oldPred, Node *predicate);

PG_EXTERN void InitIndexStrategy(int numatts,
				  Relation indexRelation,
				  Oid accessMethodObjectId);

PG_EXTERN void index_create(char *heapRelationName,
			 char *indexRelationName,
			 FuncIndexInfo *funcInfo,
			 List *attributeList,
			 Oid accessMethodObjectId,
			 int numatts,
			 AttrNumber *attNums,
			 Oid *classObjectId,
			 uint16 parameterCount,
			 Datum *parameter,
			 Node *predicate,
			 bool isdeferred,
                         bool islossy,
			 bool unique,
			 bool primary);

PG_EXTERN void index_drop(Oid indexId);

PG_EXTERN void FormIndexDatum(int numberOfAttributes,
			   AttrNumber *attributeNumber, HeapTuple heapTuple,
			   TupleDesc heapDescriptor, Datum *datum,
			   char *nullv, FuncIndexInfoPtr fInfo);

PG_EXTERN void UpdateStats(Oid relid, long reltuples);
PG_EXTERN bool IndexesAreActive(Oid relid, bool comfirmCommitted);
PG_EXTERN void setRelhasindexInplace(Oid relid, bool hasindex, bool immediate);
PG_EXTERN bool SetReindexProcessing(bool processing);
PG_EXTERN void ResetReindexProcessing(void);
PG_EXTERN bool IsReindexProcessing(void);

PG_EXTERN void FillDummyExprContext(ExprContext *econtext, TupleTableSlot *slot,
					 TupleDesc tupdesc);

PG_EXTERN void index_build(Relation heapRelation, Relation indexRelation,
			int numberOfAttributes, AttrNumber *attributeNumber,
		uint16 parameterCount, Datum *parameter, FuncIndexInfo *funcInfo,
			PredInfo *predInfo);

PG_EXTERN void build_indices(void);

PG_EXTERN Oid IndexGetRelation(Oid indexId);
PG_EXTERN IndexProp IndexProperties(Oid indexId);
PG_EXTERN bool IndexIsUniqueNoCache(Oid indexId);

PG_EXTERN bool activate_index(Oid indexId, bool activate);
PG_EXTERN bool reindex_index(Oid indexId, bool force);
PG_EXTERN bool activate_indexes_of_a_table(Oid relid, bool activate);
PG_EXTERN bool reindex_relation(Oid relid, bool force);

#endif	 /* INDEX_H */
