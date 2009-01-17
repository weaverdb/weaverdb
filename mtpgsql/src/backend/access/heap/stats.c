/*-------------------------------------------------------------------------
 *
 * stats.c
 *	  heap access method debugging statistic collection routines
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  $Header: /cvs/weaver/mtpgsql/src/backend/access/heap/stats.c,v 1.1.1.1 2006/08/12 00:20:00 synmscott Exp $
 *
 * NOTES
 *	  initam should be moved someplace else.
 *
 *-------------------------------------------------------------------------
 */

#include <time.h>

#include "postgres.h"

#include "env/env.h"

#include "access/heapam.h"


static void InitHeapAccessStatistics(void);

/* ----------------
 *		InitHeapAccessStatistics
 * ----------------
 */
/*
*
*	Moved to env MKS   7/30/2000
*
*
*
*HeapAccessStatistics heap_access_stats = (HeapAccessStatistics) NULL;
*
*/
static void HeapStatsInitEnv(void);
static void HeapStatsDestroyEnv(void);
static HeapAccessStatistics HeapStatsGetEnv(void);

static SectionId  stats_id = SECTIONID("HSTA");

#ifdef TLS
TLS HeapAccessStatistics stats_global = NULL;
#else
#define stats_global GetEnv()->stats_global
#endif

void HeapStatsInitEnv(void)
{
    HeapAccessStatistics stats = AllocateEnvSpace(stats_id,sizeof(HeapAccessStatisticsData));
	memset(stats,0x00,sizeof(HeapAccessStatisticsData));
	stats_global = stats;
}

void HeapStatsDestroyEnv(void)
{
    ReleaseEnvSpace(stats_id);
    stats_global = NULL;
}

static HeapAccessStatistics HeapStatsGetEnv(void)
{
	return stats_global;
}

static void
InitHeapAccessStatistics()
{
    HeapAccessStatistics    stats;

	/* ----------------
	 *	make sure we don't initialize things twice
	 * ----------------
	 */
	if (HeapStatsGetEnv() != NULL)
		return;

    HeapStatsInitEnv();
    stats = HeapStatsGetEnv();

	/* ----------------
	 *	initialize fields to default values
	 * ----------------
	 */
	stats->global_open = 0;
	stats->global_openr = 0;
	stats->global_close = 0;
	stats->global_beginscan = 0;
	stats->global_rescan = 0;
	stats->global_endscan = 0;
	stats->global_getnext = 0;
	stats->global_fetch = 0;
	stats->global_insert = 0;
	stats->global_delete = 0;
	stats->global_replace = 0;
	stats->global_mark4update = 0;
	stats->global_markpos = 0;
	stats->global_restrpos = 0;
	stats->global_BufferGetRelation = 0;
	stats->global_RelationIdGetRelation = 0;
	stats->global_RelationIdGetRelation_Buf = 0;
	stats->global_getreldesc = 0;
	stats->global_heapgettup = 0;
	stats->global_RelationPutHeapTuple = 0;
	stats->global_RelationPutLongHeapTuple = 0;

	stats->local_open = 0;
	stats->local_openr = 0;
	stats->local_close = 0;
	stats->local_beginscan = 0;
	stats->local_rescan = 0;
	stats->local_endscan = 0;
	stats->local_getnext = 0;
	stats->local_fetch = 0;
	stats->local_insert = 0;
	stats->local_delete = 0;
	stats->local_replace = 0;
	stats->local_mark4update = 0;
	stats->local_markpos = 0;
	stats->local_restrpos = 0;
	stats->local_BufferGetRelation = 0;
	stats->local_RelationIdGetRelation = 0;
	stats->local_RelationIdGetRelation_Buf = 0;
	stats->local_getreldesc = 0;
	stats->local_heapgettup = 0;
	stats->local_RelationPutHeapTuple = 0;
	stats->local_RelationPutLongHeapTuple = 0;
	stats->local_RelationNameGetRelation = 0;
	stats->global_RelationNameGetRelation = 0;

	/* ----------------
	 *	record init times
	 * ----------------
	 */
	time(&stats->init_global_timestamp);
	time(&stats->local_reset_timestamp);
	time(&stats->last_request_timestamp);

	/* ----------------
	 *	return to old memory context
	 * ----------------
	 */
}

/* ----------------------------------------------------------------
 *					access method initialization
 * ----------------------------------------------------------------
 */
/* ----------------
 *		initam should someday be moved someplace else.
 * ----------------
 */
void
initam(void)
{
	/* ----------------
	 *	initialize heap statistics.
	 * ----------------
	 */
	InitHeapAccessStatistics();
}

