/*-------------------------------------------------------------------------
 *
 * vacuum.h
 *	  header file for postgres vacuum cleaner
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * $Id: vacuum.h,v 1.1.1.1 2006/08/12 00:22:17 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */
#ifndef VACUUM_H
#define VACUUM_H

#include "fmgr.h"
#include "access/funcindex.h"
#include "catalog/pg_index.h"
#include "catalog/pg_attribute.h"
#include "nodes/pg_list.h"
#include "storage/itemptr.h"


typedef struct VAttListData
{
	int			val_dummy;
	struct VAttListData *val_next;
} VAttListData;

typedef VAttListData *VAttList;

typedef struct VPageDescrData
{
	BlockNumber vpd_blkno;		/* BlockNumber of this Page */
	Size		vpd_free;		/* FreeSpace on this Page */
	uint16		vpd_offsets_used;		/* Number of OffNums used by
										 * vacuum */
	uint16		vpd_offsets_free;		/* Number of OffNums free or to be
										 * free */
	OffsetNumber vpd_offsets[1];/* Array of its OffNums */
} VPageDescrData;

typedef VPageDescrData *VPageDescr;

typedef struct VPageListData
{
	long			vpl_empty_end_pages;	/* Number of "empty" end-pages */
	long			vpl_num_pages;	/* Number of pages in vpl_pagedesc */
	long			vpl_num_allocated_pages;		/* Number of allocated
												 * pages in vpl_pagedesc */
	VPageDescr *vpl_pagedesc;	/* Descriptions of pages */
} VPageListData;

typedef VPageListData *VPageList;

typedef struct
{
	FuncIndexInfo finfo;
	FuncIndexInfo *finfoP;
	Form_pg_index tform;
	int			natts;
} IndDesc;

typedef struct
{
	Form_pg_attribute attr;
	Datum		best,
				guess1,
				guess2,
				max,
				min;
	int			best_len,
				guess1_len,
				guess2_len,
				max_len,
				min_len;
	long		best_cnt,
				guess1_cnt,
				guess1_hits,
				guess2_hits,
				null_cnt,
				nonnull_cnt,
				max_cnt,
				min_cnt;
	FmgrInfo	f_cmpeq,
				f_cmplt,
				f_cmpgt;
	Oid			op_cmplt;
	regproc		outfunc;
	Oid			typelem;
	bool		initialized;
} VacAttrStats;

typedef struct VRelListData
{
	Oid			vrl_relid;
	struct VRelListData *vrl_next;
} VRelListData;

typedef VRelListData *VRelList;

typedef struct VTupleLinkData
{
	ItemPointerData new_tid;
	ItemPointerData this_tid;
} VTupleLinkData;

typedef VTupleLinkData *VTupleLink;

typedef struct VTupleMoveData
{
	ItemPointerData tid;		/* tuple ID */
	VPageDescr	vpd;			/* where to move */
	bool		cleanVpd;		/* clean vpd before using */
} VTupleMoveData;

typedef VTupleMoveData *VTupleMove;

typedef struct VRelStats
{
	Oid			relid;
	TupleCount			num_tuples;
	TupleCount			num_pages;
	Size			min_tlen;
	Size			max_tlen;
	bool			hasindex;
	int			va_natts;		/* number of attrs being analyzed */
	VacAttrStats *		vacattrstats;
	int			num_vtlinks;
	VTupleLink		vtlinks;
        TransactionId		xmax_recent;
        bool                       fixflags;
        int                       MESSAGE_LEVEL;
} VRelStats;

typedef enum fragmode {
    NORMAL,
    RELINKING,
    BLOB_MOVE
}   FragMode;
/*
extern  bool VacuumRunning;
*/
#ifdef __cplusplus
extern "C" {
#endif

PG_EXTERN void vc_abort(void);
PG_EXTERN void vacuum(char *vacrel, bool verbose, bool analyze,bool exclusive,bool fix, List *va_spec);
PG_EXTERN void lazy_open_vacuum_rel(Oid vacrel, bool force_trim, bool scanonly);
PG_EXTERN void lazy_fragmentation_scan_rel(Oid vacrel,bool force, FragMode mode, int max);
PG_EXTERN void lazy_respan_blobs_rel(Oid vacrel, bool force, bool exclude_self);
PG_EXTERN void lazy_freespace_scan_rel(Oid vacrel);
PG_EXTERN void analyze_rel(Oid vacrel);
PG_EXTERN void lazy_vacuum_database(bool verbose);

#ifdef __cplusplus
}
#endif

#define ATTNVALS_SCALE	1000000000		/* XXX so it can act as a float4 */

#endif	 /* VACUUM_H */
