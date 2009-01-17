/*-------------------------------------------------------------------------
 *
 * rtscan.h
 *	  routines defined in access/rtree/rtscan.c
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * $Id: rtscan.h,v 1.1.1.1 2006/08/12 00:22:10 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */
#ifndef RTSCAN_H
#define RTSCAN_H

#include "storage/block.h"
#include "storage/off.h"
#include "utils/rel.h"

typedef struct RTScanListData
{
	IndexScanDesc rtsl_scan;
	struct RTScanListData *rtsl_next;
} RTScanListData;

typedef RTScanListData *RTScanList;

void		rtadjscans(Relation r, int op, BlockNumber blkno, OffsetNumber offnum);

#endif	 /* RTSCAN_H */
