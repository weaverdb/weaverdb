#ifndef PGVECTOR_EXECUTOR_PORT_H
#define PGVECTOR_EXECUTOR_PORT_H

#include "executor/tuptable.h"
#include "pgvector_index.h"

typedef struct Tuplesortstate Tuplesortstate;

#define Int4LessOperator		97
#define Float8LessOperator	672

typedef struct SortCoordinateData SortCoordinateData;
typedef struct SortCoordinateData *SortCoordinate;

typedef struct PgvectorSlot
{
	TupleDesc	tupdesc;
	Datum		values[8];
	char		isnull[8];
	int			natts;
} PgvectorSlot;

typedef void (*PgvectorIndexBuildCallback) (Relation index, ItemPointer tid,
											Datum *values, bool *isnull,
											bool tupleIsAlive, void *state);

TupleTableSlot *pgvector_make_slot(TupleDesc tupdesc);
void pgvector_clear_slot(TupleTableSlot *slot);
void pgvector_store_virtual_tuple(TupleTableSlot *slot);
Datum pgvector_slot_getattr(TupleTableSlot *slot, int attnum, bool *isnull);
void pgvector_slot_set_attr(TupleTableSlot *slot, int attnum, Datum value, bool isnull);

#define MakeSingleTupleTableSlot(desc, ops) pgvector_make_slot(desc)
#define ExecClearTuple(slot) pgvector_clear_slot(slot)
#define ExecStoreVirtualTuple(slot) pgvector_store_virtual_tuple(slot)
#define slot_getattr(slot, attno, isnull) pgvector_slot_getattr((slot), (attno), (isnull))

Tuplesortstate *pgvector_tuplesort_begin_heap(TupleDesc tupdesc, int nkeys,
											  AttrNumber *attNums, Oid *sortOperators,
											  Oid *sortCollations, bool *nullsFirstFlags,
											  int workMem, SortCoordinate coordinate,
											  bool randomAccess);
void pgvector_tuplesort_puttupleslot(Tuplesortstate *state, TupleTableSlot *slot);
bool pgvector_tuplesort_gettupleslot(Tuplesortstate *state, bool forward, bool copy,
									 TupleTableSlot *slot, Datum *abbrev);
void pgvector_tuplesort_reset(Tuplesortstate *state);

#define tuplesort_begin_heap pgvector_tuplesort_begin_heap
#define tuplesort_puttupleslot pgvector_tuplesort_puttupleslot
#define tuplesort_gettupleslot pgvector_tuplesort_gettupleslot
#define tuplesort_reset pgvector_tuplesort_reset

double pgvector_table_index_build_scan(Relation heap, Relation index,
									   PgvectorIndexInfo *indexInfo,
									   bool allow_sync, bool anyvisible,
									   PgvectorIndexBuildCallback callback,
									   void *callback_state, void *scan);
double pgvector_table_index_build_range_scan(Relation heap, Relation index,
											 PgvectorIndexInfo *indexInfo,
											 bool allow_sync, bool anyvisible,
											 bool progress,
											 BlockNumber start_block,
											 BlockNumber num_blocks,
											 PgvectorIndexBuildCallback callback,
											 void *callback_state, void *scan);

#define table_index_build_scan pgvector_table_index_build_scan
#define table_index_build_range_scan pgvector_table_index_build_range_scan

#endif /* PGVECTOR_EXECUTOR_PORT_H */
