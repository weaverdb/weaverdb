/*-------------------------------------------------------------------------
 *
 * genam.c
 *	  general index access method routines
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  $Header: /cvs/weaver/mtpgsql/src/backend/access/index/genam.c,v 1.1.1.1 2006/08/12 00:20:00 synmscott Exp $
 *
 * NOTES
 *	  many of the old access method routines have been turned into
 *	  macros and moved to genam.h -cim 4/30/91
 *
 *-------------------------------------------------------------------------
 */
/*
 * OLD COMMENTS
 * Scans are implemented as follows:
 *
 * `0' represents an invalid item pointer.
 * `-' represents an unknown item pointer.
 * `X' represents a known item pointers.
 * `+' represents known or invalid item pointers.
 * `*' represents any item pointers.
 *
 * State is represented by a triple of these symbols in the order of
 * previous, current, next.  Note that the case of reverse scans works
 * identically.
 *
 *		State	Result
 * (1)	+ + -	+ 0 0			(if the next item pointer is invalid)
 * (2)			+ X -			(otherwise)
 * (3)	* 0 0	* 0 0			(no change)
 * (4)	+ X 0	X 0 0			(shift)
 * (5)	* + X	+ X -			(shift, add unknown)
 *
 * All other states cannot occur.
 *
 * Note:
 *		It would be possible to cache the status of the previous and
 *		next item pointer using the flags.
 * ----------------------------------------------------------------
 */

#include "postgres.h"
#include "env/env.h"
#include "access/genam.h"
#include "storage/smgr.h"
#include "nodes/pg_list.h"
#include "utils/relcache.h"

/* ----------------------------------------------------------------
 *		general access method routines
 *
 *		All indexed access methods use an identical scan structure.
 *		We don't know how the various AMs do locking, however, so we don't
 *		do anything about that here.
 *
 *		The intent is that an AM implementor will define a beginscan routine
 *		that calls RelationGetIndexScan, to fill in the scan, and then does
 *		whatever kind of locking he wants.
 *
 *		At the end of a scan, the AM's endscan routine undoes the locking,
 *		but does *not* call IndexScanEnd --- the higher-level index_endscan
 *		routine does that.	(We can't do it in the AM because index_endscan
 *		still needs to touch the IndexScanDesc after calling the AM.)
 *
 *		Because of this, the AM does not have a choice whether to call
 *		RelationGetIndexScan or not; its beginscan routine must return an
 *		object made by RelationGetIndexScan.  This is kinda ugly but not
 *		worth cleaning up now.
 * ----------------------------------------------------------------
 */


bool            DelegatedIndexBuild = true;
bool		FastIndexBuild = true;	/* use SORT instead of insertion build */


static SectionId idx_id = SECTIONID("INDX");

#ifdef TLS
TLS IndexGlobals* index_globals = NULL;
#else
#define index_globals GetEnv()->index_globals
#endif

/* ----------------
 *	RelationGetIndexScan -- Create and fill an IndexScanDesc.
 *
 *		This routine creates an index scan structure and sets its contents
 *		up correctly. This routine calls AMrescan to set up the scan with
 *		the passed key.
 *
 *		Parameters:
 *				relation -- index relation for scan.
 *				scanFromEnd -- if true, begin scan at one of the index's
 *							   endpoints.
 *				numberOfKeys -- count of scan keys.
 *				key -- the ScanKey for the starting position of the scan.
 *
 *		Returns:
 *				An initialized IndexScanDesc.
 * ----------------
 */
IndexScanDesc
RelationGetIndexScan(Relation relation,
					 bool scanFromEnd,
					 uint16 numberOfKeys,
					 ScanKey key)
{
	IndexScanDesc scan;

	if (!RelationIsValid(relation))
		elog(ERROR, "RelationGetIndexScan: relation invalid");

	scan = (IndexScanDesc) palloc(sizeof(IndexScanDescData));
        memset(scan,0x00,sizeof(IndexScanDescData));

	scan->relation = relation;
	scan->opaque = NULL;
	scan->numberOfKeys = numberOfKeys;

	ItemPointerSetInvalid(&scan->currentItemData);
	ItemPointerSetInvalid(&scan->currentMarkData);

	/*
	 * mark cached function lookup data invalid; it will be set on first
	 * use
	 */
	scan->fn_getnext.fn_oid = InvalidOid;

	if (numberOfKeys > 0)
		scan->keyData = (ScanKey) palloc(sizeof(ScanKeyData) * numberOfKeys);
	else
		scan->keyData = NULL;
	index_rescan(scan, scanFromEnd, key);
	return scan;
}

/* ----------------
 *	IndexScanEnd -- End an index scan.
 *
 *		This routine just releases the storage acquired by
 *		RelationGetIndexScan().  Any AM-level resources are
 *		assumed to already have been released by the AM's
 *		endscan routine.
 *
 *	Returns:
 *		None.
 * ----------------
 */
void
IndexScanEnd(IndexScanDesc scan)
{
	if (!IndexScanIsValid(scan))
		elog(ERROR, "IndexScanEnd: invalid scan");

	if (scan->keyData != NULL)
		pfree(scan->keyData);

	pfree(scan);
}



IndexGlobals*
GetIndexGlobals(void)
{
        IndexGlobals* info = index_globals;
        if ( info == NULL ) {
            info = AllocateEnvSpace(idx_id,sizeof(IndexGlobals));
            memset(info,0x00,sizeof(IndexGlobals));
            info->FastIndexBuild = FastIndexBuild;
            info->DelegatedIndexBuild = DelegatedIndexBuild;
            
            index_globals = info;
        }

        return info;
}

void
index_recoverpages(List* pages) {
    List* item;
    
    if ( !pages ) return;
    
    foreach(item,pages) {
        RecoveredPage* page = lfirst(item);
        Oid relid = page->relid;
        Relation rel = RelationIdGetRelation(relid,DEFAULTDBOID);
        if ( rel->rd_rel->relkind == RELKIND_INDEX ) {
            RegProcedure procedure = rel->rd_am->amfreetuple;   /*  this one is deprecated and we are using the slot for recover page */
            if ( RegProcedureIsValid(procedure) ) {
                fmgr(procedure, rel, page->block);
            }
        }
        RelationClose(rel);
    }
}

BlockNumber
index_recoverpage(Relation rel,BlockNumber page) {
    if ( rel->rd_rel->relkind == RELKIND_INDEX ) {
        RegProcedure procedure = rel->rd_am->amfreetuple;   /*  this one is deprecated and we are using the slot for recover page */
        if ( RegProcedureIsValid(procedure) ) {
            return DatumGetLong(fmgr(procedure, rel, page));
        }
    }
    return InvalidBlockNumber;
}

