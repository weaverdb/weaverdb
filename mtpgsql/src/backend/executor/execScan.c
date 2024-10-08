/*-------------------------------------------------------------------------
 *
 * execScan.c
 *	  This code provides support for generalized relation scans. ExecScan
 *	  is passed a node and a pointer to a function to "do the right thing"
 *	  and return a tuple from the relation. ExecScan then does the tedious
 *	  stuff - checking the qualification and projecting the tuple
 *	  appropriately.
 *
 * Portions Copyright (c) 2000-2024, Myron Scott  <myron@weaverdb.org>
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *
 *
 *-------------------------------------------------------------------------
 */

#include <sys/file.h>
#include "postgres.h"
#include "env/env.h"

#include "executor/executor.h"
#include "access/blobstorage.h"

/* ----------------------------------------------------------------
 *		ExecScan
 *
 *		Scans the relation using the 'access method' indicated and
 *		returns the next qualifying tuple in the direction specified
 *		in the global variable ExecDirection.
 *		The access method returns the next tuple and execScan() is
 *		responisble for checking the tuple returned against the qual-clause.
 *
 *		Conditions:
 *		  -- the "cursor" maintained by the AMI is positioned at the tuple
 *			 returned previously.
 *
 *		Initial States:
 *		  -- the relation indicated is opened for scanning so that the
 *			 "cursor" is positioned before the first qualifying tuple.
 *
 *		May need to put startmmgr  and endmmgr in here.
 * ----------------------------------------------------------------
 */
TupleTableSlot *
ExecScan(Scan *node,
		 TupleTableSlot *(*accessMtd) (Scan*))/* function returning a * tuple */
{
	CommonScanState *scanstate;
//	EState	   *estate;
	List	   *qual;
	bool		isDone;

	TupleTableSlot *slot;
	TupleTableSlot *resultSlot;
//	HeapTuple	newTuple;

	ExprContext *econtext;
	ProjectionInfo *projInfo;


	/* ----------------
	 *	initialize misc variables
	 * ----------------
	 */
//	newTuple = NULL;
	slot = NULL;

//	estate = node->plan.state;
	scanstate = node->scanstate;

	/* ----------------
	 *	get the expression context
	 * ----------------
	 */
	econtext = scanstate->cstate.cs_ExprContext;

	/* ----------------
	 *	initialize fields in ExprContext which don't change
	 *	in the course of the scan..
	 * ----------------
	 */
	qual = node->plan.qual;
	econtext->ecxt_relation = scanstate->css_currentRelation;
	econtext->ecxt_relid = node->scanrelid;

	if (scanstate->cstate.cs_TupFromTlist)
	{
		projInfo = scanstate->cstate.cs_ProjInfo;
		resultSlot = ExecProject(projInfo, &isDone);
		if (!isDone)
			return resultSlot;
	}

	/*
	 * get a tuple from the access method loop until we obtain a tuple
	 * which passes the qualification.
	 */
	for (;;)
	{
		slot = (TupleTableSlot *) (*accessMtd) (node);

		/* ----------------
		 *	if the slot returned by the accessMtd contains
		 *	NULL, then it means there is nothing more to scan
		 *	so we just return the empty slot...
		 *
		 *	... with invalid TupleDesc (not the same as in
		 *	projInfo->pi_slot) and break upper MergeJoin node.
		 *	New code below do what ExecProject() does.	- vadim 02/26/98
		 * ----------------
		 */
		if (TupIsNull(slot))
		{
			scanstate->cstate.cs_TupFromTlist = false;
			resultSlot = scanstate->cstate.cs_ProjInfo->pi_slot;
			ExecClearTuple(resultSlot);
			return ExecStoreTuple(NULL,resultSlot,false);
		}

		/* ----------------
		 *	 place the current tuple into the expr context
		 * ----------------
		 */
		econtext->ecxt_scantuple = slot;

		/* ----------------
		 *	check that the current tuple satisfies the qual-clause
		 *	if our qualification succeeds then we
		 *	leave the loop.
		 * ----------------
		 */

		/*
		 * add a check for non-nil qual here to avoid a function call to
		 * ExecQual() when the qual is nil ... saves only a few cycles,
		 * but they add up ...
		 */
		if (!qual || ExecQual(qual, econtext, false))
			break;
	}

	/* ----------------
	 *	form a projection tuple, store it in the result tuple
	 *	slot and return it.
	 * ----------------
	 */
	projInfo = scanstate->cstate.cs_ProjInfo;

	resultSlot = ExecProject(projInfo, &isDone);
	scanstate->cstate.cs_TupFromTlist = !isDone;

	return resultSlot;
}
