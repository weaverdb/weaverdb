/*-------------------------------------------------------------------------
 *
 * genam.h
 *	  POSTGRES general access method definitions.
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * $Id: genam.h,v 1.1.1.1 2006/08/12 00:22:08 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */
#ifndef GENAM_H
#define GENAM_H

#include "access/funcindex.h"
#include "access/itup.h"
#include "access/relscan.h"
#include "access/sdir.h"

typedef struct index_globals {
/*   from hash.c    */
	bool			BuildingHash;
/*    from nbtree.c      */
	bool			BuildingBtree;		 
	bool			reindexing;
/*    from hashscan.c   	 */
	void*	 		HashScans;    
/*    from rtscan.c     */
	void*			RTScans;	
        bool                    FastIndexBuild;
        bool                    DelegatedIndexBuild;
} IndexGlobals;

IndexGlobals* GetIndexGlobals( void );

/* ----------------
 *		generalized index_ interface routines
 * ----------------
 */
void index_recoverpages(List* pages);

/* extern */  Relation index_open(Oid relationId);
/* extern */  Relation index_openr(char *relationName);
/* extern */  void index_close(Relation relation);
/* extern */  InsertIndexResult index_insert(Relation relation,
			 Datum *datum, char *nulls,
			 ItemPointer heap_t_ctid,
			 Relation heapRel);
/* extern */  void index_delete(Relation relation, ItemPointer indexItem);
/* extern */  TupleCount index_bulkdelete(Relation relation,int delcount, ItemPointerData* del_heappointers);
/* extern */  IndexScanDesc index_beginscan(Relation relation, bool scanFromEnd,
				uint16 numberOfKeys, ScanKey key);
/* extern */  void index_rescan(IndexScanDesc scan, bool scanFromEnd, ScanKey key);
/* extern */  void index_endscan(IndexScanDesc scan);
/* extern */  void index_markpos(IndexScanDesc scan);
/* extern */  void index_restrpos(IndexScanDesc scan);
/* extern */  bool index_getnext(IndexScanDesc scan, ScanDirection dir);
/* extern */  RegProcedure index_cost_estimator(Relation relation);
/* extern */  RegProcedure index_getprocid(Relation irel, AttrNumber attnum,
				uint16 procnum);
/* extern */  Datum GetIndexValue(HeapTuple tuple, TupleDesc hTupDesc,
			  int attOff, AttrNumber *attrNums, FuncIndexInfo *fInfo,
			  bool *attNull);

/* in genam.c */
/* extern */  IndexScanDesc RelationGetIndexScan(Relation relation, bool scanFromEnd,
					 uint16 numberOfKeys, ScanKey key);
/* extern */  void IndexScanEnd(IndexScanDesc scan);

#endif	 /* GENAM_H */
