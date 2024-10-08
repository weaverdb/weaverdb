/*-------------------------------------------------------------------------
 *
 * indexam.c
 *	  general index access method routines
 *
 * Portions Copyright (c) 2000-2024, Myron Scott  <myron@weaverdb.org>
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *
 *
 * INTERFACE ROUTINES
 *		index_open		- open an index relation by relationId
 *		index_openr		- open a index relation by name
 *		index_close		- close a index relation
 *		index_beginscan - start a scan of an index
 *		index_rescan	- restart a scan of an index
 *		index_endscan	- end a scan
 *		index_insert	- insert an index tuple into a relation
 *		index_delete	- delete an item from an index relation
 *		index_markpos	- mark a scan position
 *		index_restrpos	- restore a scan position
 *		index_getnext	- get the next tuple from a scan
 * **	index_fetch		- retrieve tuple with tid
 * **	index_replace	- replace a tuple
 * **	index_getattr	- get an attribute from an index tuple
 *		index_getprocid - get a support procedure id from the rel tuple
 *
 *		IndexScanIsValid - check index scan
 *
 * NOTES
 *		This file contains the index_ routines which used
 *		to be a scattered collection of stuff in access/genam.
 *
 *		The ** routines: index_fetch, index_replace, and index_getattr
 *		have not yet been implemented.	They may not be needed.
 *
 * old comments
 *		Scans are implemented as follows:
 *
 *		`0' represents an invalid item pointer.
 *		`-' represents an unknown item pointer.
 *		`X' represents a known item pointers.
 *		`+' represents known or invalid item pointers.
 *		`*' represents any item pointers.
 *
 *		State is represented by a triple of these symbols in the order of
 *		previous, current, next.  Note that the case of reverse scans works
 *		identically.
 *
 *				State	Result
 *		(1)		+ + -	+ 0 0			(if the next item pointer is invalid)
 *		(2)				+ X -			(otherwise)
 *		(3)		* 0 0	* 0 0			(no change)
 *		(4)		+ X 0	X 0 0			(shift)
 *		(5)		* + X	+ X -			(shift, add unknown)
 *
 *		All other states cannot occur.
 *
 *		Note: It would be possible to cache the status of the previous and
 *			  next item pointer using the flags.
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"
#include "env/env.h"
#include "access/genam.h"
#include "access/heapam.h"
#include "access/blobstorage.h"
#include "utils/relcache.h"
#include "miscadmin.h"

/* ----------------------------------------------------------------
 *					macros used in index_ routines
 * ----------------------------------------------------------------
 */
#define RELATION_CHECKS \
( \
	AssertMacro(RelationIsValid(relation)), \
	AssertMacro(PointerIsValid(relation->rd_am)) \
)

#define SCAN_CHECKS \
( \
	AssertMacro(IndexScanIsValid(scan)), \
	AssertMacro(RelationIsValid(scan->relation)), \
	AssertMacro(PointerIsValid(scan->relation->rd_am)) \
)

#define GET_REL_PROCEDURE(x,y) \
( \
	procedure = relation->rd_am->y, \
	(!RegProcedureIsValid(procedure)) ? \
		elog(ERROR, "index_%s: invalid %s regproc", \
			CppAsString(x), CppAsString(y)) \
	: (void)NULL \
)

#define GET_SCAN_PROCEDURE(x,y) \
( \
	procedure = scan->relation->rd_am->y, \
	(!RegProcedureIsValid(procedure)) ? \
		elog(ERROR, "index_%s: invalid %s regproc", \
			CppAsString(x), CppAsString(y)) \
	: (void)NULL \
)


/* ----------------------------------------------------------------
 *				   index_ interface functions
 * ----------------------------------------------------------------
 */
/* ----------------
 *		index_open - open an index relation by relationId
 *
 *		presently the relcache routines do all the work we need
 *		to open/close index relations.	However, callers of index_open
 *		expect it to succeed, so we need to check for a failure return.
 *
 *		Note: we acquire no lock on the index.	An AccessShareLock is
 *		acquired by index_beginscan (and released by index_endscan).
 * ----------------
 */
Relation
index_open(Oid relationId)
{
	Relation	r;

	r = RelationIdGetRelation(relationId,DEFAULTDBOID);

	if (!RelationIsValid(r))
		elog(ERROR, "Index %lu does not exist", relationId);

	if (r->rd_rel->relkind != RELKIND_INDEX)
		elog(ERROR, "%s is not an index relation", RelationGetRelationName(r));

	return r;
}

/* ----------------
 *		index_openr - open a index relation by name
 *
 *		As above, but lookup by name instead of OID.
 * ----------------
 */
Relation
index_openr(char *relationName)
{
	Relation	r;

	r = RelationNameGetRelation(relationName,GetDatabaseId());

	if (!RelationIsValid(r))
		elog(ERROR, "Index '%s' does not exist", relationName);

	if (r->rd_rel->relkind != RELKIND_INDEX)
		elog(ERROR, "%s is not an index relation", RelationGetRelationName(r));

	return r;
}

/* ----------------
 *		index_close - close a index relation
 *
 *		presently the relcache routines do all the work we need
 *		to open/close index relations.
 * ----------------
 */
void
index_close(Relation relation)
{
	RelationClose(relation);
}

/* ----------------
 *		index_insert - insert an index tuple into a relation
 * ----------------
 */
InsertIndexResult
index_insert(Relation relation,
			 Datum *datum,
			 char *nulls,
			 ItemPointer heap_t_ctid,
			 Relation heapRel,bool is_put)
{
	RegProcedure procedure;
	InsertIndexResult specificResult;

	RELATION_CHECKS;
	GET_REL_PROCEDURE(insert, aminsert);

	/* ----------------
	 *	have the am's insert proc do all the work.
	 * ----------------
	 */
        

	specificResult = (InsertIndexResult)
		DatumGetPointer(fmgr(procedure, relation, datum, nulls, heap_t_ctid, heapRel, is_put));

	/* must be pfree'ed */
	return specificResult;
}

/* ----------------
 *		index_delete - delete an item from an index relation
 * ----------------
 */
void
index_delete(Relation relation, ItemPointer indexItem)
{
	RegProcedure procedure;

	RELATION_CHECKS;
	GET_REL_PROCEDURE(delete, amdelete);

	fmgr(procedure, relation, indexItem);
}

TupleCount index_bulkdelete(Relation relation,int delcount, ItemPointerData* del_heappointers)
{
	RegProcedure procedure;

	RELATION_CHECKS;
	GET_REL_PROCEDURE(bulkdelete, ambulkdelete);

	return (TupleCount)DatumGetLong(fmgr(procedure, relation, delcount, del_heappointers));
}

/* ----------------
 *		index_beginscan - start a scan of an index
 * ----------------
 */
IndexScanDesc
index_beginscan(Relation relation,
				bool scanFromEnd,
				uint16 numberOfKeys,
				ScanKey key)
{
	IndexScanDesc scandesc;
	RegProcedure procedure;

	RELATION_CHECKS;
	GET_REL_PROCEDURE(beginscan, ambeginscan);

	RelationIncrementReferenceCount(relation);

	/* ----------------
	 *	Acquire AccessShareLock for the duration of the scan
	 *
	 *	Note: we could get an SI inval message here and consequently have
	 *	to rebuild the relcache entry.	The refcount increment above
	 *	ensures that we will rebuild it and not just flush it...
	 * ----------------
	 */
	LockRelation(relation, AccessShareLock);

	scandesc = (IndexScanDesc)
		fmgr(procedure, relation, scanFromEnd, numberOfKeys, key);

	return scandesc;
}

/* ----------------
 *		index_rescan  - restart a scan of an index
 * ----------------
 */
void
index_rescan(IndexScanDesc scan, bool scanFromEnd, ScanKey key)
{
	RegProcedure procedure;

	SCAN_CHECKS;
	GET_SCAN_PROCEDURE(rescan, amrescan);

	fmgr(procedure, scan, scanFromEnd, key);
}

/* ----------------
 *		index_endscan - end a scan
 * ----------------
 */
void
index_endscan(IndexScanDesc scan)
{
	RegProcedure procedure;

	SCAN_CHECKS;
	GET_SCAN_PROCEDURE(endscan, amendscan);

	fmgr(procedure, scan);

	/* Release lock and refcount acquired by index_beginscan */

	UnlockRelation(scan->relation, AccessShareLock);

	RelationDecrementReferenceCount(scan->relation);

	/* Release the scan data structure itself */
	IndexScanEnd(scan);
}

/* ----------------
 *		index_markpos  - mark a scan position
 * ----------------
 */
void
index_markpos(IndexScanDesc scan)
{
	RegProcedure procedure;

	SCAN_CHECKS;
	GET_SCAN_PROCEDURE(markpos, ammarkpos);

	fmgr(procedure, scan);
}

/* ----------------
 *		index_restrpos	- restore a scan position
 * ----------------
 */
void
index_restrpos(IndexScanDesc scan)
{
	RegProcedure procedure;

	SCAN_CHECKS;
	GET_SCAN_PROCEDURE(restrpos, amrestrpos);

	fmgr(procedure, scan);
}

/* ----------------
 *		index_getnext - get the next tuple from a scan
 *
 *		A RetrieveIndexResult is a index tuple/heap tuple pair
 * ----------------
 */
bool
index_getnext(IndexScanDesc scan,
			  ScanDirection direction)
{
	bool result;

	SCAN_CHECKS;

	/* ----------------
	 *	Look up the access procedure only once per scan.
	 * ----------------
	 */
	if (scan->fn_getnext.fn_oid == InvalidOid)
	{
		RegProcedure procedure;

		GET_SCAN_PROCEDURE(getnext, amgettuple);
		fmgr_info(procedure, &scan->fn_getnext);
	}

	/* ----------------
	 *	have the am's gettuple proc do all the work.
	 * ----------------
	 */
	result = DatumGetChar((*fmgr_faddr_2(&scan->fn_getnext))(PointerGetDatum(scan), direction));

	return result;
}

/* ----------------
 *		index_cost_estimator
 *
 *		Fetch the amcostestimate procedure OID for an index.
 *
 *		We could combine fetching and calling the procedure,
 *		as index_insert does for example; but that would require
 *		importing a bunch of planner/optimizer stuff into this file.
 * ----------------
 */
RegProcedure
index_cost_estimator(Relation relation)
{
	RegProcedure procedure;

	RELATION_CHECKS;
	GET_REL_PROCEDURE(cost_estimator, amcostestimate);

	return procedure;
}

/* ----------------
 *		index_getprocid
 *
 *		Some indexed access methods may require support routines that are
 *		not in the operator class/operator model imposed by pg_am.	These
 *		access methods may store the OIDs of registered procedures they
 *		need in pg_amproc.	These registered procedure OIDs are ordered in
 *		a way that makes sense to the access method, and used only by the
 *		access method.	The general index code doesn't know anything about
 *		the routines involved; it just builds an ordered list of them for
 *		each attribute on which an index is defined.
 *
 *		This routine returns the requested procedure OID for a particular
 *		indexed attribute.
 * ----------------
 */
RegProcedure
index_getprocid(Relation irel,
				AttrNumber attnum,
				uint16 procnum)
{
	RegProcedure *loc;
	int			natts;

	natts = irel->rd_rel->relnatts;

	loc = irel->rd_support;

	Assert(loc != NULL);

	return loc[(natts * (procnum - 1)) + (attnum - 1)];
}

Datum
GetIndexValue(HeapTuple tuple,
			  TupleDesc hTupDesc,
			  int attOff,
			  AttrNumber *attrNums,
			  FuncIndexInfo *fInfo,
			  bool *attNull)
{
	Datum		returnVal;
	bool		isNull = FALSE;

	if (PointerIsValid(fInfo) && FIgetProcOid(fInfo) != InvalidOid)
	{
		int			i;
		Datum	   *attData = (Datum *) palloc(FIgetnArgs(fInfo) * sizeof(Datum));
		bool		mustfree[FIgetnArgs(fInfo)];

		for (i = 0; i < FIgetnArgs(fInfo); i++)
		{
					attData[i] = HeapGetAttr(tuple,
									  attrNums[i],
									  hTupDesc,
									  attNull);
									  
				mustfree[i] = false;

			if (hTupDesc->attrs[attrNums[i] - 1]->attstorage == 'e' && !(*attNull) && ISINDIRECT(attData[i])) {
				attData[i] = PointerGetDatum(rebuild_indirect_blob(attData[i]));
				mustfree[i] = true;
			}
			
			if (*attNull)
				isNull = TRUE;
		}
		returnVal = (Datum) fmgr_array_args(FIgetProcOid(fInfo),
											FIgetnArgs(fInfo),
											(char **) attData,
											&isNull);
		pfree(attData);
		for (i = 0; i < FIgetnArgs(fInfo); i++)
		{
			if ( mustfree[i] ) pfree(DatumGetPointer(attData[i]));
		}
		*attNull = isNull;
	}
	else {
		returnVal = HeapGetAttr(tuple, attrNums[attOff], hTupDesc, attNull);
		if ( HeapTupleHasBlob(tuple) ) {
			if ( ( hTupDesc->attrs[attrNums[attOff]-1]->attstorage == 'e' ) && ISINDIRECT(returnVal)  ) {
				elog(ERROR,"index key is too large");
			}
		}	
	}


	return returnVal;
}


