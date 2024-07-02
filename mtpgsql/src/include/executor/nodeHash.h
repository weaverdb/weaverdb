/*-------------------------------------------------------------------------
 *
 * nodeHash.h
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
#ifndef NODEHASH_H
#define NODEHASH_H

#include "nodes/plannodes.h"

/* NTUP_PER_BUCKET is exported because planner wants to see it */
#define NTUP_PER_BUCKET			10

PG_EXTERN TupleTableSlot *ExecHash(Hash *node);
PG_EXTERN bool ExecInitHash(Hash *node, EState *estate);
PG_EXTERN int	ExecCountSlotsHash(Hash *node);
PG_EXTERN void ExecEndHash(Hash *node);
PG_EXTERN HashJoinTable ExecHashTableCreate(Hash *node);
PG_EXTERN void ExecHashTableDestroy(HashJoinTable hashtable);
PG_EXTERN void ExecHashTableInsert(HashJoinTable hashtable, ExprContext *econtext,
					Var *hashkey);
PG_EXTERN int ExecHashGetBucket(HashJoinTable hashtable, ExprContext *econtext,
				  Var *hashkey);
PG_EXTERN HeapTuple ExecScanHashBucket(HashJoinState *hjstate, List *hjclauses,
				   ExprContext *econtext);
PG_EXTERN void ExecHashTableReset(HashJoinTable hashtable, long ntuples);
PG_EXTERN void ExecReScanHash(Hash *node, ExprContext *exprCtxt);

#endif	 /* NODEHASH_H */
