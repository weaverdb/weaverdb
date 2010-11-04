/*-------------------------------------------------------------------------
 *
 * analyze.c
 *	  the postgres statistics generator
 *
 * Portions Copyright (c) 1996-2002, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  $Header: /cvs/weaver/mtpgsql/src/backend/env/analyze.c,v 1.1.1.1 2006/08/12 00:19:50 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */
#include <math.h>

#include "postgres.h"
#include "env/env.h"

#include "env/freespace.h"
#include "access/heapam.h"
#include "access/blobstorage.h"
#include "access/tuptoaster.h"
#include "catalog/catalog.h"
#include "catalog/catname.h"
#include "catalog/indexing.h"
#include "catalog/pg_operator.h"
#include "catalog/pg_statistic.h"
#include "catalog/pg_type.h"
#include "commands/vacuum.h"
#include "miscadmin.h"
#include "parser/parse_oper.h"
#include "parser/parse_relation.h"
#ifdef USEACL
#include "utils/acl.h"
#endif
#include "utils/builtins.h"
#include "utils/datum.h"
#include "utils/lsyscache.h"
#include "utils/syscache.h"
#include "utils/tuplesort.h"
#include "utils/wrapdatum.h"


/*
 * Analysis algorithms supported
 */
typedef enum {
	ALG_MINIMAL = 1,	/* Compute only most-common-values */
	ALG_SCALAR,		/* Compute MCV, histogram, sort correlation */
	ALG_NONE
}               AlgCode;

/*
 * To avoid consuming too much memory during analysis and/or too much space
 * in the resulting pg_statistic rows, we ignore varlena datums that are
 * wider than WIDTH_THRESHOLD (after detoasting!).  This is legitimate for
 * MCV and distinct-value calculations since a wide value is unlikely to be
 * duplicated at all, much less be a most-common value.  For the same reason,
 * ignoring wide values will not affect our estimates of histogram bin
 * boundaries very much.
 */
#define WIDTH_THRESHOLD  1024
#define STATISTIC_NUM_SLOTS  4

/*
 * We build one of these structs for each attribute (column) that is to be
 * analyzed.  The struct and subsidiary data are in anl_context, so they live
 * until the end of the ANALYZE operation.
 */
typedef struct {
	/* These fields are set up by examine_attribute */
	int             attnum;	/* attribute number */
	AlgCode         algcode;/* Which algorithm to use for this column */
	int             minrows;/* Minimum # of rows wanted for stats */
	Form_pg_attribute attr;	/* copy of pg_attribute row for column */
	Form_pg_type    attrtype;	/* copy of pg_type row for column */

	FmgrInfo        f_cmpeq;
	FmgrInfo        f_cmplt;
	FmgrInfo        f_cmpgt;

	Oid             op_cmplt;
	Oid             op_cmpgt;
	Oid             op_cmpeq;
	/*
	 * These fields are filled in by the actual statistics-gathering
	 * routine
	 */
	bool            stats_valid;
	float4          stanullfrac;	/* fraction of entries that are NULL */
	int4            stawidth;	/* average width */
	float4          stadistinct;	/* # distinct values */
	int2            stakind[STATISTIC_NUM_SLOTS];
	Oid             staop[STATISTIC_NUM_SLOTS];
	int             numnumbers[STATISTIC_NUM_SLOTS];
	float4         *stanumbers[STATISTIC_NUM_SLOTS];
	int             numvalues[STATISTIC_NUM_SLOTS];
	Datum          *stavalues[STATISTIC_NUM_SLOTS];
}               AnalyzeAttrStats;


typedef struct {
	Datum           value;	/* a data value */
	int             tupno;	/* position index for tuple it came from */
}               ScalarItem;

typedef struct {
	int             count;	/* # of duplicates */
	int             first;	/* values[] index of first occurrence */
}               ScalarMCVItem;

/*
typedef struct {
    BlockNumber         count;
    BlockNumber         limit;
    Relation    self;
} ReasonableCount;
*/

#define swapInt(a,b)	do {int _tmp; _tmp=a; a=b; b=_tmp;} while(0)
#define swapDatum(a,b)	do {Datum _tmp; _tmp=a; a=b; b=_tmp;} while(0)


/* Default statistics target (GUC parameter) */
int             default_statistics_target = 10;

static SectionId analyze_section_id = SECTIONID("ANLZ");

/* context information for compare_scalars() */
typedef struct {
	FmgrInfo       *datumCmpFn;
	SortFunctionKind datumCmpFnKind;
	int            *datumCmpTupnoLink;
}               AnalyzeGlobals;


#ifdef TLS
TLS AnalyzeGlobals* analyze_globals = NULL;
#else
#define analyze_globals GetEnv()->analyze_globals
#endif

static AnalyzeGlobals *AnalyzeInitEnv();
static void     AnalyzeDestroyEnv();
static AnalyzeGlobals *AnalyzeGetEnv();
static void  analyze_log(Relation rel, char* pattern, ...);

static AnalyzeAttrStats *examine_attribute(Relation onerel, int attnum);
static int 
acquire_sample_rows(Relation onerel, HeapTuple * rows,
		    int targrows, double *totalrows);
static double   random_fract(void);
static double   init_selection_state(int n);
static double   select_next_random_record(double t, int n, double *stateptr);
static int      compare_rows(const void *a, const void *b);
static int      compare_scalars(const void *a, const void *b);
static int      compare_mcvs(const void *a, const void *b);
static void 
compute_minimal_stats(AnalyzeAttrStats * stats,
		      Relation onerel, double totalrows,
		  HeapTuple * rows, int numrows, MemoryContext anl_context);
static void 
compute_scalar_stats(AnalyzeAttrStats * stats,
		     Relation onerel, double totalrows,
		  HeapTuple * rows, int numrows, MemoryContext anl_context);
static void     update_attstats(Oid relid, int natts, AnalyzeAttrStats ** vacattrstats);
static int     reasonable_read_check(void *args);
static HeapTuple find_tuple_on_page(Relation onerel, Buffer targbuf, Offset targoffset);

/*
 * analyze_rel() -- analyze one relation
 */
void
analyze_rel(Oid relid)
{
	Relation        onerel;
	int             attr_cnt, tcnt, i;
	AnalyzeAttrStats **vacattrstats;
	int             targrows, numrows;
	double          totalrows;
	HeapTuple      *rows;
	MemoryContext   anl_context = MemoryContextGetCurrentContext();
	struct timeval  stv;
	struct timeval  etv;
	struct timezone tz;
	double          runtime;

	gettimeofday(&stv, &tz);

	onerel = heap_open(relid, AccessShareLock);
        onerel->rd_nblocks = RelationGetNumberOfBlocks(onerel);
	/*
	 * Check that it's a plain table; we used to do this in getrels() but
	 * seems safer to check after we've locked the relation.
	 */
	if (onerel->rd_rel->relkind != RELKIND_RELATION) {
		/*
		 * No need for a WARNING if we already complained during
		 * VACUUM
		 */
		analyze_log(onerel,"Skipping --- can not process indexes, views or special system tables");
		heap_close(onerel, AccessShareLock);
		return;
	}
	/*
	 * We can ANALYZE any table except pg_statistic. See update_attstats
	 */
	if (
	    strcmp(RelationGetRelationName(onerel), StatisticRelationName) == 0) {
		heap_close(onerel, AccessShareLock);
		return;
	}
	attr_cnt = onerel->rd_att->natts;

	analyze_log(onerel,"Analyzing with %d attributes",
	     attr_cnt);

	vacattrstats = (AnalyzeAttrStats **) palloc(attr_cnt *
						sizeof(AnalyzeAttrStats *));
	tcnt = 0;
	for (i = 1; i <= attr_cnt; i++) {
		analyze_log(onerel,"examining %s", onerel->rd_att->attrs[i - 1]->attname);
		vacattrstats[tcnt] = examine_attribute(onerel, i);
		if (vacattrstats[tcnt] != NULL)
			tcnt++;
	}
	attr_cnt = tcnt;

	analyze_log(onerel,"done examining attributes");

	/*
	 * Quit if no analyzable columns
	 */
	if (attr_cnt <= 0) {
		analyze_log(onerel,"no analyzable columns");
		heap_close(onerel, AccessShareLock);
		return;
	}
	/*
	 * Determine how many rows we need to sample, using the worst case
	 * from all analyzable columns.  We use a lower bound of 100 rows to
	 * avoid possible overflow in Vitter's algorithm.
	 */
	targrows = 100;
	for (i = 0; i < attr_cnt; i++) {
		if (targrows < vacattrstats[i]->minrows)
			targrows = vacattrstats[i]->minrows;
	}

	/*
	 * Acquire the sample rows
	 */
	analyze_log(onerel,"acquiring %d sample rows", targrows);
	rows = (HeapTuple *) palloc(targrows * sizeof(HeapTuple));
	memset(rows, 0x00, targrows * sizeof(HeapTuple));
	numrows = acquire_sample_rows(onerel, rows, targrows, &totalrows);

	/*
	 * Compute the statistics.	Temporary results during the
	 * calculations for each column are stored in a child context.  The
	 * calc routines are responsible to make sure that whatever they
	 * store into the VacAttrStats structure is allocated in anl_context.
	 */

	if (numrows > 0) {
		MemoryContext   col_context, old_context;

		col_context = AllocSetContextCreate(anl_context,
						    "Analyze Column",
						    ALLOCSET_DEFAULT_MINSIZE,
						  ALLOCSET_DEFAULT_INITSIZE,
						  ALLOCSET_DEFAULT_MAXSIZE);
		old_context = MemoryContextSwitchTo(col_context);
		for (i = 0; i < attr_cnt; i++) {
			switch (vacattrstats[i]->algcode) {
			case ALG_MINIMAL:
				analyze_log(onerel,"computing minimal stats for %s attribute:%s",  NameStr(vacattrstats[i]->attr->attname));
				compute_minimal_stats(vacattrstats[i],
						      onerel, totalrows,
						rows, numrows, anl_context);
				if (vacattrstats[i]->stats_valid) {
					analyze_log(onerel,"minimal stats for attribute:%s are valid", NameStr(vacattrstats[i]->attr->attname));
				} else {
					analyze_log(onerel,"minimal stats for attribute:%s are not valid", NameStr(vacattrstats[i]->attr->attname));
				}
				break;
			case ALG_SCALAR:
				analyze_log(onerel,"computing scalar stats for attribute:%s", NameStr(vacattrstats[i]->attr->attname));
				compute_scalar_stats(vacattrstats[i],
						     onerel, totalrows,
						rows, numrows, anl_context);
				if (vacattrstats[i]->stats_valid) {
					analyze_log(onerel,"scalar stats for attribute:%s are valid",  NameStr(vacattrstats[i]->attr->attname));
				} else {
					analyze_log(onerel,"scalar stats for attribute:%s are not valid", NameStr(vacattrstats[i]->attr->attname));
				}
				break;
			case ALG_NONE:
				analyze_log(onerel,"no stats for attribute:%s",  NameStr(vacattrstats[i]->attr->attname));
				break;
			}
			MemoryContextResetAndDeleteChildren(col_context);
		}

		MemoryContextSwitchTo(old_context);
		MemoryContextDelete(col_context);

		/*
		 * Emit the completed stats rows into pg_statistic, replacing
		 * any previous statistics for the target columns.	(If
		 * there are stats in pg_statistic for columns we didn't
		 * process, we leave them alone.)
		 */
		update_attstats(relid, attr_cnt, vacattrstats);
	}
	/*
	 * Close source relation now, but keep lock so that no one deletes it
	 * before we commit.  (If someone did, they'd fail to clean up the
	 * entries we made in pg_statistic.)
	 */
	heap_close(onerel, NoLock);

	gettimeofday(&etv, &tz);

	runtime = etv.tv_sec;
	runtime += (etv.tv_usec * .000001);
	runtime -= stv.tv_sec;
	runtime -= (stv.tv_usec * .000001);
	analyze_log(onerel,"analyze time %.2f", runtime);
}

/*
 * examine_attribute -- pre-analysis of a single column
 * 
 * Determine whether the column is analyzable; if so, create and initialize a
 * VacAttrStats struct for it.  If not, return NULL.
 */
static AnalyzeAttrStats *
examine_attribute(Relation onerel, int attnum)
{
	Form_pg_attribute attr = onerel->rd_att->attrs[attnum - 1];
	Operator        func_operator;
	Oid             oprrest;
	HeapTuple       typtuple;
	Oid             eqopr = InvalidOid;
	Oid             eqfunc = InvalidOid;
	/* Oid			ltopr = InvalidOid; */
	AnalyzeAttrStats *stats;
	Form_pg_operator pgopform;
	bool            minimal = true;
	bool            ltopr = false;
	int             counter = 0;

	/*
	 * If we have "=" then we're at least able to do the minimal
	 * algorithm, so start filling in a VacAttrStats struct.
	 */
	stats = (AnalyzeAttrStats *) palloc(sizeof(AnalyzeAttrStats));
	for (counter = 0; counter < STATISTIC_NUM_SLOTS; counter++) {
		stats->stakind[counter] = -1;
	}

	stats->attnum = attnum;
	stats->attr = (Form_pg_attribute) palloc(ATTRIBUTE_TUPLE_SIZE);
	memcpy(stats->attr, attr, ATTRIBUTE_TUPLE_SIZE);
	typtuple = SearchSysCacheTuple(TYPEOID,
				       ObjectIdGetDatum(attr->atttypid),
				       0, 0, 0);
	if (!HeapTupleIsValid(typtuple))
		elog(ERROR, "cache lookup of type %u failed", attr->atttypid);
	stats->attrtype = (Form_pg_type) palloc(sizeof(FormData_pg_type));
	memcpy(stats->attrtype, GETSTRUCT(typtuple), sizeof(FormData_pg_type));

	func_operator = oper("=", stats->attr->atttypid, stats->attr->atttypid, true);
	if (func_operator != NULL) {
		pgopform = (Form_pg_operator) GETSTRUCT(func_operator);
		fmgr_info(pgopform->oprcode, &(stats->f_cmpeq));
		stats->op_cmpeq = oprid(func_operator);
	} else {
		minimal = false;
		stats->f_cmpeq.fn_addr = NULL;
	}

	func_operator = oper("<", stats->attr->atttypid, stats->attr->atttypid, true);
	if (func_operator != NULL) {
		ltopr = true;
		pgopform = (Form_pg_operator) GETSTRUCT(func_operator);
		fmgr_info(pgopform->oprcode, &(stats->f_cmplt));
		stats->op_cmplt = oprid(func_operator);
	} else {
		stats->f_cmplt.fn_addr = NULL;
		stats->op_cmplt = InvalidOid;
	}

	func_operator = oper(">", stats->attr->atttypid, stats->attr->atttypid, true);
	if (func_operator != NULL) {
		pgopform = (Form_pg_operator) GETSTRUCT(func_operator);
		fmgr_info(pgopform->oprcode, &(stats->f_cmpgt));
		stats->op_cmpgt = oprid(func_operator);
	} else {
		stats->f_cmpgt.fn_addr = NULL;
        }

	/*
	 * Determine the algorithm to use (this will get more complicated
	 * later)
	 */
	if (ltopr) {
		/* Seems to be a scalar datatype */
		stats->algcode = ALG_SCALAR;
		/*--------------------
		 * The following choice of minrows is based on the paper
		 * "Random sampling for histogram construction: how much is enough?"
		 * by Surajit Chaudhuri, Rajeev Motwani and Vivek Narasayya, in
		 * Proceedings of ACM SIGMOD International Conference on Management
		 * of Data, 1998, Pages 436-447.  Their Corollary 1 to Theorem 5
		 * says that for table size n, histogram size k, maximum relative
		 * error in bin size f, and error probability gamma, the minimum
		 * random sample size is
		 *		r = 4 * k * ln(2*n/gamma) / f^2
		 * Taking f = 0.5, gamma = 0.01, n = 1 million rows, we obtain
		 *		r = 305.82 * k
		 * Note that because of the log function, the dependence on n is
		 * quite weak; even at n = 1 billion, a 300*k sample gives <= 0.59
		 * bin size error with probability 0.99.  So there's no real need to
		 * scale for n, which is a good thing because we don't necessarily
		 * know it at this point.
		 *--------------------
		 */
		stats->minrows = 300 * default_statistics_target;
	} else if (minimal) {
		/* Can't do much but the minimal stuff */
		stats->algcode = ALG_MINIMAL;
		/* Might as well use the same minrows as above */
		stats->minrows = 300 * default_statistics_target;
	} else {
		stats->algcode = ALG_NONE;
		stats->minrows = 0;
	}

	return stats;
}

/*
static int
reasonable_read_check(void* args) {
    ReasonableCount*  check = (ReasonableCount*)args;

    if ( check->count++ > check->limit ) {
        RelationClearReadTrigger(check->self);
        elog(ERROR,"giving up on analyze, too many empty pages");
    }
    return 0;
}
*/
/*
 * acquire_sample_rows -- acquire a random sample of rows from the table
 * 
 * Up to targrows rows are collected (if there are fewer than that many rows in
 * the table, all rows are collected).	When the table is larger than
 * targrows, a truly random sample is collected: every row has an equal
 * chance of ending up in the final sample.
 * 
 * We also estimate the total number of rows in the table, and return that into
 * *totalrows.
 * 
 * The returned list of tuples is in order by physical position in the table.
 * (We will rely on this later to derive correlation estimates.)
 */
static int
acquire_sample_rows(Relation onerel, HeapTuple * rows, int targrows,
		    double *totalrows)
{
	int             numrows = 0;
	ItemPointer     lasttuple;
	BlockNumber     lastblock, estblock,targblock;
	OffsetNumber    lastoffset;
	int             numest;
	double          tuplesperpage;
	double          t;
	double          rstate;
        int misses  = 0;
/*
        BufferTrigger   reasonable;
        ReasonableCount rc;

        rc.limit = (BlockNumber)(onerel->rd_nblocks / 100);
        if ( rc.limit < 1000 ) rc.limit = 1000;
        rc.self = onerel;
        rc.count = 0;
        reasonable.call = reasonable_read_check;
        reasonable.args = &rc;
*/
        
	Assert(targrows > 0);
        if (IsShutdownProcessingMode()) {
                elog(ERROR, "shutting down");
        }
	/*
	 * Do a simple linear scan until we reach the target number of rows.
	 */
        targblock = 0;
        lastblock = onerel->rd_nblocks;
        while (numrows < targrows && targblock < lastblock) {
            HeapTuple  targtup;
            OffsetNumber targoff = FirstOffsetNumber;
            bool        found = false;

            if (IsShutdownProcessingMode()) {
                    elog(ERROR, "shutting down");
            }

            Buffer targbuf = ReadBuffer(onerel,targblock++);
            
            if (!BufferIsValid(targbuf)) {
                elog(ERROR, "acquire_sample_rows: ReadBuffer(%s,%u) failed",RelationGetRelationName(onerel), targblock);
            }
            LockBuffer((onerel), targbuf, BUFFER_LOCK_SHARE);
            targtup = find_tuple_on_page(onerel,targbuf,targoff);
            while ( numrows < targrows && HeapTupleIsValid(targtup) ) {
                found = true;
                rows[numrows++] = targtup;
                targoff = ItemPointerGetOffsetNumber(&targtup->t_self) + 1;
                targtup = find_tuple_on_page(onerel,targbuf,targoff);
            }
            LockBuffer((onerel), targbuf, BUFFER_LOCK_UNLOCK);
            ReleaseBuffer(onerel,targbuf);
            if ( !found ) {
                if ( misses++ > 300 ) {
                    elog(ERROR,"acquire_sample_rows: giving up on %s, too many empty pages",RelationGetRelationName(onerel));
                }
            } else {
                misses = 0;
            }
        }
	/*
	 * If we ran out of tuples then we're done, no matter how few we
	 * collected.  No sort is needed, since they're already in order.
	 */
	if (numrows != targrows) {
            *totalrows = (double) numrows;
            return numrows;
	}
	/*
	 * Otherwise, start replacing tuples in the sample until we reach the
	 * end of the relation.  This algorithm is from Jeff Vitter's paper
	 * (see full citation below).  It works by repeatedly computing the
	 * number of the next tuple we want to fetch, which will replace a
	 * randomly chosen element of the reservoir (current set of tuples).
	 * At all times the reservoir is a true random sample of the tuples
	 * we've passed over so far, so when we fall off the end of the
	 * relation we're done.
	 * 
	 * A slight difficulty is that since we don't want to fetch tuples or
	 * even pages that we skip over, it's not possible to fetch *exactly*
	 * the N'th tuple at each step --- we don't know how many valid
	 * tuples are on the skipped pages.  We handle this by assuming that
	 * the average number of valid tuples/page on the pages already
	 * scanned over holds good for the rest of the relation as well; this
	 * lets us estimate which page the next tuple should be on and its
	 * position in the page.  Then we fetch the first valid tuple at or
	 * after that position, being careful not to use the same tuple
	 * twice.  This approach should still give a good random sample,
	 * although it's not perfect.
	 */
	lasttuple = &(rows[numrows - 1]->t_self);
	lastblock = ItemPointerGetBlockNumber(lasttuple);
	lastoffset = ItemPointerGetOffsetNumber(lasttuple);

	/*
	 * If possible, estimate tuples/page using only completely-scanned
	 * pages.
	 */
	for (numest = numrows; numest > 0; numest--) {
            if (ItemPointerGetBlockNumber(&(rows[numest - 1]->t_self)) != lastblock)
                    break;
	}
	if (numest == 0) {
		numest = numrows;	/* don't have a full page? */
		estblock = lastblock + 1;
	} else {
		estblock = lastblock;
        }
	tuplesperpage = (double) numest / (double) estblock;

	t = (double) numrows;	/* t is the # of records processed so far */
	rstate = init_selection_state(targrows);
        targblock = 0;
	while (targblock < onerel->rd_nblocks) {
            double          targpos;
            Buffer          targbuf;
            OffsetNumber    targoffset;
            bool            replaced = FALSE;

            if (IsShutdownProcessingMode()) {
                    elog(ERROR, "shutting down");
            }
            t = select_next_random_record(t, targrows, &rstate);
            /* Try to read the t'th record in the table */
            targpos = t / tuplesperpage;
            targblock = (BlockNumber) targpos;
            targoffset = ((int) ((targpos - targblock) * tuplesperpage)) +
                    FirstOffsetNumber;
            /* Make sure we are past the last selected record */
            if (targblock <= lastblock) {
                    targblock = lastblock;
                    if (targoffset <= lastoffset)
                            targoffset = lastoffset + 1;
            }
            /* Loop to find first valid record at or after given position */
            while (!replaced && targblock < onerel->rd_nblocks) {
                HeapTuple   targtup;
                
                if (IsShutdownProcessingMode()) {
                        elog(ERROR, "shutting down");
                }

                targbuf = ReadBuffer(onerel, targblock++);
                if (!BufferIsValid(targbuf)) {
                    elog(ERROR, "acquire_sample_rows: ReadBuffer(%s,%u) failed",RelationGetRelationName(onerel), targblock-1);
                }
                LockBuffer((onerel), targbuf, BUFFER_LOCK_SHARE);
                targtup = find_tuple_on_page(onerel,targbuf,targoffset);
                if ( HeapTupleIsValid(targtup) ) {
                    int k = (int)(targrows * random_fract());
                    heap_freetuple(rows[k]);
                    rows[k] = targtup;
                    lastblock = ItemPointerGetBlockNumber(&targtup->t_self);
                    lastoffset = ItemPointerGetOffsetNumber(&targtup->t_self);
                    replaced = TRUE;
                } else {
                    targoffset = FirstOffsetNumber;
                }
                LockBuffer((onerel), targbuf, BUFFER_LOCK_UNLOCK);
                ReleaseBuffer(onerel,targbuf);
            }
	}

	/*
	 * Now we need to sort the collected tuples by position
	 * (itempointer).
	 */
	qsort((void *) rows, numrows, sizeof(HeapTuple), compare_rows);

	/*
	 * Estimate total number of valid rows in relation.
	 */
	*totalrows = floor((double) onerel->rd_nblocks * tuplesperpage + 0.5);

	return numrows;
}

static HeapTuple
find_tuple_on_page(Relation onerel, Buffer targbuf, Offset targoffset) {
    HeapTupleData   targtuple;
    Offset  maxoffset;
    BlockNumber   targblock = BufferGetBlockNumber(targbuf);
    Page    targpage = BufferGetPage(targbuf);

    targtuple.t_len = 0;
    targtuple.t_datamcxt = NULL;
    targtuple.t_datasrc = NULL;

    maxoffset = PageGetMaxOffsetNumber(targpage);

    for (;targoffset <= maxoffset;targoffset = OffsetNumberNext(targoffset))
    {
        ItemId itemid = PageGetItemId(targpage,targoffset);
        if ( ItemIdIsUsed(itemid) ) {
            targtuple.t_data = (HeapTupleHeader) PageGetItem(targpage, itemid);
            targtuple.t_len = ItemIdGetLength(itemid);
            ItemPointerSet(&targtuple.t_self,targblock,targoffset);
            if ( !(targtuple.t_data->t_infomask & HEAP_BLOB_SEGMENT) ) {
                if  ( HeapTupleSatisfies(onerel, targbuf, &targtuple,  SnapshotNow, 0, NULL) ) {
                    return heap_copytuple(&targtuple);
                }
            }
        }
    }
    
    return NULL;
}


/* Select a random value R uniformly distributed in 0 < R < 1 */
static double
random_fract(void)
{
	long            z;

	/* random() can produce endpoint values, try again if so */
	do {
		z = prandom();
	} while (!(z > 0 && z < MAX_RANDOM_VALUE));
	return (double) z / (double) MAX_RANDOM_VALUE;
}

/*
 * These two routines embody Algorithm Z from "Random sampling with a
 * reservoir" by Jeffrey S. Vitter, in ACM Trans. Math. Softw. 11, 1 (Mar.
 * 1985), Pages 37-57.  While Vitter describes his algorithm in terms of the
 * count S of records to skip before processing another record, it is
 * convenient to work primarily with t, the index (counting from 1) of the
 * last record processed and next record to process.  The only extra state
 * needed between calls is W, a random state variable.
 * 
 * Note: the original algorithm defines t, S, numer, and denom as integers. Here
 * we express them as doubles to avoid overflow if the number of rows in the
 * table exceeds INT_MAX.  The algorithm should work as long as the row count
 * does not become so large that it is not represented accurately in a double
 * (on IEEE-math machines this would be around 2^52 rows).
 * 
 * init_selection_state computes the initial W value.
 * 
 * Given that we've already processed t records (t >= n),
 * select_next_random_record determines the number of the next record to
 * process.
 */
static double
init_selection_state(int n)
{
	/* Initial value of W (for use when Algorithm Z is first applied) */
	return exp(-log(random_fract()) / n);
}

static double
select_next_random_record(double t, int n, double *stateptr)
{
	/* The magic constant here is T from Vitter's paper */
	if (t <= (22.0 * n)) {
		/* Process records using Algorithm X until t is large enough */
		double          V, quot;

		V = random_fract();	/* Generate V */
		t += 1;
		quot = (t - (double) n) / t;
		/* Find min S satisfying (4.1) */
		while (quot > V) {
			t += 1;
			quot *= (t - (double) n) / t;
		}
	} else {
		/* Now apply Algorithm Z */
		double          W = *stateptr;
		double          term = t - (double) n + 1;
		double          S;

		for (;;) {
			double          numer, numer_lim, denom;
			double          U, X, lhs, rhs, y, tmp;

			/* Generate U and X */
			U = random_fract();
			X = t * (W - 1.0);
			S = floor(X);	/* S is tentatively set to floor(X) */
			/* Test if U <= h(S)/cg(X) in the manner of (6.3) */
			tmp = (t + 1) / term;
			lhs = exp(log(((U * tmp * tmp) * (term + S)) / (t + X)) / n);
			rhs = (((t + X) / (term + S)) * term) / t;
			if (lhs <= rhs) {
				W = rhs / lhs;
				break;
			}
			/* Test if U <= f(S)/cg(X) */
			y = (((U * (t + 1)) / term) * (t + S + 1)) / (t + X);
			if ((double) n < S) {
				denom = t;
				numer_lim = term + S;
			} else {
				denom = t - (double) n + S;
				numer_lim = t + 1;
			}
			for (numer = t + S; numer >= numer_lim; numer -= 1) {
				y *= numer / denom;
				denom -= 1;
			}
			W = exp(-log(random_fract()) / n);	/* Generate W in advance */
			if (exp(log(y) / n) <= (t + X) / t)
				break;
		}
		t += S + 1;
		*stateptr = W;
	}
	return t;
}

/*
 * qsort comparator for sorting rows[] array
 */
static int
compare_rows(const void *a, const void *b)
{
	HeapTuple       ha = *(HeapTuple *) a;
	HeapTuple       hb = *(HeapTuple *) b;
	BlockNumber     ba = ItemPointerGetBlockNumber(&ha->t_self);
	OffsetNumber    oa = ItemPointerGetOffsetNumber(&ha->t_self);
	BlockNumber     bb = ItemPointerGetBlockNumber(&hb->t_self);
	OffsetNumber    ob = ItemPointerGetOffsetNumber(&hb->t_self);

	if (ba < bb)
		return -1;
	if (ba > bb)
		return 1;
	if (oa < ob)
		return -1;
	if (oa > ob)
		return 1;
	return 0;
}


/*
 * compute_minimal_stats() -- compute minimal column statistics
 * 
 * We use this when we can find only an "=" operator for the datatype.
 * 
 * We determine the fraction of non-null rows, the average width, the most
 * common values, and the (estimated) number of distinct values.
 * 
 * The most common values are determined by brute force: we keep a list of
 * previously seen values, ordered by number of times seen, as we scan the
 * samples.  A newly seen value is inserted just after the last multiply-seen
 * value, causing the bottommost (oldest) singly-seen value to drop off the
 * list.  The accuracy of this method, and also its cost, depend mainly on
 * the length of the list we are willing to keep.
 */
static void
compute_minimal_stats(AnalyzeAttrStats * stats,
		      Relation onerel, double totalrows,
		   HeapTuple * rows, int numrows, MemoryContext anl_context)
{
	int             i;
	int             null_cnt = 0;
	int             nonnull_cnt = 0;
	int             toowide_cnt = 0;
	double          total_width = 0;
	bool            is_varlena = (!stats->attr->attbyval &&
				      stats->attr->attlen == -1);
	bool            is_varwidth = (!stats->attr->attbyval &&
				       stats->attr->attlen < 0);
	TupleDesc       tupDesc = onerel->rd_att;

	typedef struct {
		Datum           value;
		int             count;
	}               TrackItem;
	TrackItem      *track;
	int             track_cnt, track_max;
	int             num_mcv = default_statistics_target;

	/*
	 * We track up to 2*n values for an n-element MCV list; but at least
	 * 10
	 */
	track_max = 2 * num_mcv;
	if (track_max < 10)
		track_max = 10;
	track = (TrackItem *) palloc(track_max * sizeof(TrackItem));
	track_cnt = 0;

	for (i = 0; i < numrows; i++) {
		HeapTuple       tuple = rows[i];
		Datum           value;
		bool            isnull;
		bool            match;
		int             firstcount1, j;
		bool            free_val = false;

		value = heap_getattr(tuple, stats->attnum, tupDesc, &isnull);
		/* Check for null/nonnull */
		if (isnull) {
			null_cnt++;
			continue;
		} else {
			nonnull_cnt++;
		}


		if (HeapTupleHasBlob(tuple) &&
		    (stats->attr->attstorage == 'e') &&
                    !isnull &&
		    ISINDIRECT(value)) {
			value = PointerGetDatum(rebuild_indirect_blob(value));
			free_val = true;
		}
		/*
		 * If it's a variable-width field, add up widths for average
		 * width calculation.  Note that if the value is toasted, we
		 * use the toasted width.  We don't bother with this
		 * calculation if it's a fixed-width type.
		 */
		if (is_varlena) {
			total_width += VARSIZE(DatumGetPointer(value));
		} else if (is_varwidth) {
			/* must be cstring */
			total_width += strlen((char *) value) + 1;
		}
		/*
		 * See if the value matches anything we're already tracking.
		 */
		match = false;
		firstcount1 = track_cnt;
		for (j = 0; j < track_cnt; j++) {
			FmgrValues      args;
			bool            isNull;
			args.data[0] = (char *) value;
			args.data[1] = (char *) track[j].value;

			if (DatumGetChar(fmgr_c(&stats->f_cmpeq, &args, &isNull))) {
				match = true;
				break;
			}
			if (j < firstcount1 && track[j].count == 1)
				firstcount1 = j;
		}

		if (match) {
			/* Found a match */
			track[j].count++;
			/*
			 * This value may now need to "bubble up" in the
			 * track list
			 */
			while (j > 0 && track[j].count > track[j - 1].count) {
				swapDatum(track[j].value, track[j - 1].value);
				swapInt(track[j].count, track[j - 1].count);
				j--;
			}
		} else {
			/* No match.  Insert at head of count-1 list */
			if (track_cnt < track_max)
				track_cnt++;
			for (j = track_cnt - 1; j > firstcount1; j--) {
				track[j].value = track[j - 1].value;
				track[j].count = track[j - 1].count;
			}
			if (firstcount1 < track_cnt) {
				track[firstcount1].value = value;
				track[firstcount1].count = 1;
			}
		}
		if (free_val)
			pfree(DatumGetPointer(value));
	}

	/* We can only compute valid stats if we found some non-null values. */
	if (nonnull_cnt > 0) {
		int             nmultiple, summultiple;

		stats->stats_valid = true;
		/* Do the simple null-frac and width stats */
		stats->stanullfrac = (double) null_cnt / (double) numrows;
		if (is_varwidth)
			stats->stawidth = total_width / (double) nonnull_cnt;
		else
			stats->stawidth = stats->attrtype->typlen;

		/* Count the number of values we found multiple times */
		summultiple = 0;
		for (nmultiple = 0; nmultiple < track_cnt; nmultiple++) {
			if (track[nmultiple].count == 1)
				break;
			summultiple += track[nmultiple].count;
		}

		if (nmultiple == 0) {
			/*
			 * If we found no repeated values, assume it's a
			 * unique column
			 */
			stats->stadistinct = -1.0;
		} else if (track_cnt < track_max && toowide_cnt == 0 &&
			   nmultiple == track_cnt) {
			/*
			 * Our track list includes every value in the sample,
			 * and every value appeared more than once.  Assume
			 * the column has just these values.
			 */
			stats->stadistinct = track_cnt;
		} else {
			/*----------
			 * Estimate the number of distinct values using the estimator
			 * proposed by Haas and Stokes in IBM Research Report RJ 10025:
			 *		n*d / (n - f1 + f1*n/N)
			 * where f1 is the number of distinct values that occurred
			 * exactly once in our sample of n rows (from a total of N),
			 * and d is the total number of distinct values in the sample.
			 * This is their Duj1 estimator; the other estimators they
			 * recommend are considerably more complex, and are numerically
			 * very unstable when n is much smaller than N.
			 *
			 * We assume (not very reliably!) that all the multiply-occurring
			 * values are reflected in the final track[] list, and the other
			 * nonnull values all appeared but once.  (XXX this usually
			 * results in a drastic overestimate of ndistinct.	Can we do
			 * any better?)
			 *----------
			 */
			int             f1 = nonnull_cnt - summultiple;
			int             d = f1 + nmultiple;
			double          numer, denom, stadistinct;

			numer = (double) numrows *(double) d;

			denom = (double) (numrows - f1) +
				(double) f1    *(double) numrows / totalrows;

			stadistinct = numer / denom;
			/* Clamp to sane range in case of roundoff error */
			if (stadistinct < (double) d)
				stadistinct = (double) d;
			if (stadistinct > totalrows)
				stadistinct = totalrows;
			stats->stadistinct = floor(stadistinct + 0.5);
		}

		/*
		 * If we estimated the number of distinct values at more than
		 * 10% of the total row count (a very arbitrary limit), then
		 * assume that stadistinct should scale with the row count
		 * rather than be a fixed value.
		 */
		if (stats->stadistinct > 0.1 * totalrows)
			stats->stadistinct = -(stats->stadistinct / totalrows);

		/*
		 * Decide how many values are worth storing as most-common
		 * values. If we are able to generate a complete MCV list
		 * (all the values in the sample will fit, and we think these
		 * are all the ones in the table), then do so.	Otherwise,
		 * store only those values that are significantly more common
		 * than the (estimated) average. We set the threshold rather
		 * arbitrarily at 25% more than average, with at least 2
		 * instances in the sample.
		 */
		if (track_cnt < track_max && toowide_cnt == 0 &&
		    stats->stadistinct > 0 &&
		    track_cnt <= num_mcv) {
			/*
			 * Track list includes all values seen, and all will
			 * fit
			 */
			num_mcv = track_cnt;
		} else {
			double          ndistinct = stats->stadistinct;
			double          avgcount, mincount;

			if (ndistinct < 0)
				ndistinct = -ndistinct * totalrows;
			/*
			 * estimate # of occurrences in sample of a typical
			 * value
			 */
			avgcount = (double) numrows / ndistinct;
			/* set minimum threshold count to store a value */
			mincount = avgcount * 1.25;
			if (mincount < 2)
				mincount = 2;
			if (num_mcv > track_cnt)
				num_mcv = track_cnt;
			for (i = 0; i < num_mcv; i++) {
				if (track[i].count < mincount) {
					num_mcv = i;
					break;
				}
			}
		}

		/* Generate MCV slot entry */
		if (num_mcv > 0) {
			MemoryContext   old_context;
			Datum          *mcv_values;
			float4         *mcv_freqs;

			/* Must copy the target values into anl_context */
			old_context = MemoryContextSwitchTo(anl_context);
			mcv_values = (Datum *) palloc(num_mcv * sizeof(Datum));
			mcv_freqs = (float4 *) palloc(num_mcv * sizeof(float4));
			memset(mcv_values, 0, num_mcv * sizeof(Datum));
			memset(mcv_freqs, 0, num_mcv * sizeof(float4));

			for (i = 0; i < num_mcv; i++) {
				mcv_values[i] = datumCopy(track[i].value,
						      stats->attr->atttypid,
						      stats->attr->attbyval,
						       stats->attr->attlen);
				mcv_freqs[i] = (double) track[i].count / (double) numrows;
			}
			MemoryContextSwitchTo(old_context);

			stats->stakind[0] = 0;
			stats->staop[0] = stats->op_cmpeq;
			stats->stanumbers[0] = mcv_freqs;
			stats->numnumbers[0] = num_mcv;
			stats->stavalues[0] = mcv_values;
			stats->numvalues[0] = num_mcv;
		}
	}
	/* We don't need to bother cleaning up any of our temporary palloc's */
}


/*
 * compute_scalar_stats() -- compute column statistics
 * 
 * We use this when we can find "=" and "<" operators for the datatype.
 * 
 * We determine the fraction of non-null rows, the average width, the most
 * common values, the (estimated) number of distinct values, the distribution
 * histogram, and the correlation of physical to logical order.
 * 
 * The desired stats can be determined fairly easily after sorting the data
 * values into order.
 */
static void
compute_scalar_stats(AnalyzeAttrStats * stats,
		     Relation onerel, double totalrows,
		   HeapTuple * rows, int numrows, MemoryContext anl_context)
{
	int             i;
	int             null_cnt = 0;
	int             nonnull_cnt = 0;
	int             toowide_cnt = 0;
	double          total_width = 0;
	bool            is_varlena = (!stats->attr->attbyval &&
				      stats->attr->attlen == -1);
	bool            is_varwidth = (!stats->attr->attbyval &&
				       stats->attr->attlen < 0);
	double          corr_xysum;

	RegProcedure    cmpFn;
	SortFunctionKind cmpFnKind;
	FmgrInfo        f_cmpfn;

	ScalarItem     *values;
	int             values_cnt = 0;
	int            *tupnoLink;
	ScalarMCVItem  *track;
	int             track_cnt = 0;
	int             num_mcv = default_statistics_target;
	int             num_bins = default_statistics_target;

	TupleDesc       tupDesc = onerel->rd_att;

	values = (ScalarItem *) palloc(numrows * sizeof(ScalarItem));
	tupnoLink = (int *) palloc(numrows * sizeof(int));
	track = (ScalarMCVItem *) palloc(num_mcv * sizeof(ScalarMCVItem));

	SelectSortFunction(stats->op_cmplt, &cmpFn, &cmpFnKind);

	if (cmpFn == stats->f_cmplt.fn_oid)
		f_cmpfn = stats->f_cmplt;
	else if (cmpFn == stats->f_cmpgt.fn_oid)
		f_cmpfn = stats->f_cmpgt;
	else {
		fmgr_info(cmpFn, &f_cmpfn);
	}

	/* Initial scan to find sortable values */
	for (i = 0; i < numrows; i++) {
		HeapTuple       tuple = rows[i];
		Datum           value;
		bool            isnull;
		bool            free_value = false;

		value = heap_getattr(tuple, stats->attnum, tupDesc, &isnull);

		/* Check for null/nonnull */
		if (isnull) {
			null_cnt++;
			continue;
		}
		nonnull_cnt++;


		if (HeapTupleHasBlob(tuple) &&
		    (stats->attr->attstorage == 'e') &&
                    !isnull &&
		    ISINDIRECT(value)) {
			value = PointerGetDatum(rebuild_indirect_blob(value));
			free_value = true;
		}
		/*
		 * If it's a variable-width field, add up widths for average
		 * width calculation.  Note that if the value is toasted, we
		 * use the toasted width.  We don't bother with this
		 * calculation if it's a fixed-width type.
		 */
		if (is_varlena) {
			total_width += VARSIZE(DatumGetPointer(value));
		} else if (is_varwidth) {
			/* must be cstring */
			total_width += strlen((char *) value) + 1;
		}
		/* Add it to the list to be sorted */
		values[values_cnt].value = value;
		values[values_cnt].tupno = values_cnt;
		tupnoLink[values_cnt] = values_cnt;
		values_cnt++;
	}

	/* We can only compute valid stats if we found some sortable values. */
	if (values_cnt > 0) {
		AnalyzeGlobals *ag = AnalyzeInitEnv();
		int             ndistinct,	/* # distinct values in
						 * sample */
		                nmultiple,	/* # that appear multiple
						 * times */
		                num_hist, dups_cnt;
		int             slot_idx = 0;

		ag->datumCmpFn = &f_cmpfn;
		ag->datumCmpFnKind = cmpFnKind;
		ag->datumCmpTupnoLink = tupnoLink;

		qsort((void *) values, values_cnt,
		      sizeof(ScalarItem), compare_scalars);

		/*
		 * Now scan the values in order, find the most common ones,
		 * and also accumulate ordering-correlation statistics.
		 * 
		 * To determine which are most common, we first have to count
		 * the number of duplicates of each value.	The
		 * duplicates are adjacent in the sorted list, so a
		 * brute-force approach is to compare successive datum values
		 * until we find two that are not equal. However, that
		 * requires N-1 invocations of the datum comparison routine,
		 * which are completely redundant with work that was done
		 * during the sort.  (The sort algorithm must at some point
		 * have compared each pair of items that are adjacent in the
		 * sorted order; otherwise it could not know that it's
		 * ordered the pair correctly.) We exploit this by having
		 * compare_scalars remember the highest tupno index that each
		 * ScalarItem has been found equal to.	At the end of the
		 * sort, a ScalarItem's tupnoLink will still point to itself
		 * if and only if it is the last item of its group of
		 * duplicates (since the group will be ordered by tupno).
		 */
		corr_xysum = 0;
		ndistinct = 0;
		nmultiple = 0;
		dups_cnt = 0;
		for (i = 0; i < values_cnt; i++) {
			int             tupno = values[i].tupno;

			corr_xysum += ((double) i) * ((double) tupno);
			dups_cnt++;
			if (tupnoLink[tupno] == tupno) {
				/* Reached end of duplicates of this value */
				ndistinct++;
				if (dups_cnt > 1) {
					nmultiple++;
					if (track_cnt < num_mcv ||
					    dups_cnt > track[track_cnt - 1].count) {
						/*
						 * Found a new item for the
						 * mcv list; find its
						 * position, bubbling down
						 * old items if needed. Loop
						 * invariant is that j points
						 * at an empty/ replaceable
						 * slot.
						 */
						int             j;

						if (track_cnt < num_mcv)
							track_cnt++;
						for (j = track_cnt - 1; j > 0; j--) {
							if (dups_cnt <= track[j - 1].count)
								break;
							track[j].count = track[j - 1].count;
							track[j].first = track[j - 1].first;
						}
						track[j].count = dups_cnt;
						track[j].first = i + 1 - dups_cnt;
					}
				}
				dups_cnt = 0;
			}
		}

		stats->stats_valid = true;
		/* Do the simple null-frac and width stats */
		stats->stanullfrac = (double) null_cnt / (double) numrows;
		if (is_varwidth)
			stats->stawidth = total_width / (double) nonnull_cnt;
		else
			stats->stawidth = stats->attrtype->typlen;

		if (nmultiple == 0) {
			/*
			 * If we found no repeated values, assume it's a
			 * unique column
			 */
			stats->stadistinct = -1.0;
		} else if (toowide_cnt == 0 && nmultiple == ndistinct) {
			/*
			 * Every value in the sample appeared more than once.
			 * Assume the column has just these values.
			 */
			stats->stadistinct = ndistinct;
		} else {
			/*----------
			 * Estimate the number of distinct values using the estimator
			 * proposed by Haas and Stokes in IBM Research Report RJ 10025:
			 *		n*d / (n - f1 + f1*n/N)
			 * where f1 is the number of distinct values that occurred
			 * exactly once in our sample of n rows (from a total of N),
			 * and d is the total number of distinct values in the sample.
			 * This is their Duj1 estimator; the other estimators they
			 * recommend are considerably more complex, and are numerically
			 * very unstable when n is much smaller than N.
			 *
			 * Overwidth values are assumed to have been distinct.
			 *----------
			 */
			int             f1 = ndistinct - nmultiple + toowide_cnt;
			int             d = f1 + nmultiple;
			double          numer, denom, stadistinct;

			numer = (double) numrows *(double) d;

			denom = (double) (numrows - f1) +
				(double) f1    *(double) numrows / totalrows;

			stadistinct = numer / denom;
			/* Clamp to sane range in case of roundoff error */
			if (stadistinct < (double) d)
				stadistinct = (double) d;
			if (stadistinct > totalrows)
				stadistinct = totalrows;
			stats->stadistinct = floor(stadistinct + 0.5);
		}

		/*
		 * If we estimated the number of distinct values at more than
		 * 10% of the total row count (a very arbitrary limit), then
		 * assume that stadistinct should scale with the row count
		 * rather than be a fixed value.
		 */
		if (stats->stadistinct > 0.1 * totalrows)
			stats->stadistinct = -(stats->stadistinct / totalrows);

		/*
		 * Decide how many values are worth storing as most-common
		 * values. If we are able to generate a complete MCV list
		 * (all the values in the sample will fit, and we think these
		 * are all the ones in the table), then do so.	Otherwise,
		 * store only those values that are significantly more common
		 * than the (estimated) average. We set the threshold rather
		 * arbitrarily at 25% more than average, with at least 2
		 * instances in the sample.  Also, we won't suppress values
		 * that have a frequency of at least 1/K where K is the
		 * intended number of histogram bins; such values might
		 * otherwise cause us to emit duplicate histogram bin
		 * boundaries.
		 */
		if (track_cnt == ndistinct && toowide_cnt == 0 &&
		    stats->stadistinct > 0 &&
		    track_cnt <= num_mcv) {
			/*
			 * Track list includes all values seen, and all will
			 * fit
			 */
			num_mcv = track_cnt;
		} else {
			double          ndistinct = stats->stadistinct;
			double          avgcount, mincount, maxmincount;

			if (ndistinct < 0)
				ndistinct = -ndistinct * totalrows;
			/*
			 * estimate # of occurrences in sample of a typical
			 * value
			 */
			avgcount = (double) numrows / ndistinct;
			/* set minimum threshold count to store a value */
			mincount = avgcount * 1.25;
			if (mincount < 2)
				mincount = 2;
			/* don't let threshold exceed 1/K, however */
			maxmincount = (double) numrows / (double) num_bins;
			if (mincount > maxmincount)
				mincount = maxmincount;
			if (num_mcv > track_cnt)
				num_mcv = track_cnt;
			for (i = 0; i < num_mcv; i++) {
				if (track[i].count < mincount) {
					num_mcv = i;
					break;
				}
			}
		}

		/* Generate MCV slot entry */
		if (num_mcv > 0) {
			MemoryContext   old_context;
			Datum          *mcv_values;
			float4         *mcv_freqs;

			/* Must copy the target values into anl_context */
			old_context = MemoryContextSwitchTo(anl_context);
			mcv_values = (Datum *) palloc(num_mcv * sizeof(Datum));
			mcv_freqs = (float4 *) palloc(num_mcv * sizeof(float4));
			for (i = 0; i < num_mcv; i++) {
				mcv_values[i] = datumCopy(values[track[i].first].value,
						      stats->attr->atttypid,
						      stats->attr->attbyval,
						       stats->attr->attlen);
				mcv_freqs[i] = (double) track[i].count / (double) numrows;
			}
			MemoryContextSwitchTo(old_context);

			stats->stakind[slot_idx] = 0;
			stats->staop[slot_idx] = stats->op_cmpeq;
			stats->stanumbers[slot_idx] = mcv_freqs;
			stats->numnumbers[slot_idx] = num_mcv;
			stats->stavalues[slot_idx] = mcv_values;
			stats->numvalues[slot_idx] = num_mcv;
			slot_idx++;
		}
		/*
		 * Generate a histogram slot entry if there are at least two
		 * distinct values not accounted for in the MCV list.  (This
		 * ensures the histogram won't collapse to empty or a
		 * singleton.)
		 */
		num_hist = ndistinct - num_mcv;
		if (num_hist > num_bins)
			num_hist = num_bins + 1;
		if (num_hist >= 2) {
			MemoryContext   old_context;
			Datum          *hist_values;
			int             nvals;

			/*
			 * Sort the MCV items into position order to speed
			 * next loop
			 */
			qsort((void *) track, num_mcv,
			      sizeof(ScalarMCVItem), compare_mcvs);

			/*
			 * Collapse out the MCV items from the values[]
			 * array.
			 * 
			 * Note we destroy the values[] array here... but we
			 * don't need it for anything more.  We do, however,
			 * still need values_cnt. nvals will be the number of
			 * remaining entries in values[].
			 */
			if (num_mcv > 0) {
				int             src, dest;
				int             j;

				src = dest = 0;
				j = 0;	/* index of next interesting MCV item */
				while (src < values_cnt) {
					int             ncopy;

					if (j < num_mcv) {
						int             first = track[j].first;

						if (src >= first) {
							/*
							 * advance past this
							 * MCV item
							 */
							src = first + track[j].count;
							j++;
							continue;
						}
						ncopy = first - src;
					} else {
						ncopy = values_cnt - src;
                                        }
					memmove(&values[dest], &values[src],ncopy * sizeof(ScalarItem));
					src += ncopy;
					dest += ncopy;
				}
				nvals = dest;
			} else {
				nvals = values_cnt;
                        }
			Assert(nvals >= num_hist);

			/* Must copy the target values into anl_context */
			old_context = MemoryContextSwitchTo(anl_context);
			hist_values = (Datum *) palloc(num_hist * sizeof(Datum));
			for (i = 0; i < num_hist; i++) {
				int             pos;

				pos = (i * (nvals - 1)) / (num_hist - 1);
				hist_values[i] = datumCopy(values[pos].value,
						      stats->attr->atttypid,
						      stats->attr->attbyval,
						       stats->attr->attlen);
			}
			MemoryContextSwitchTo(old_context);

			stats->stakind[slot_idx] = 1;
			stats->staop[slot_idx] = stats->op_cmplt;
			stats->stavalues[slot_idx] = hist_values;
			stats->numvalues[slot_idx] = num_hist;
			slot_idx++;
		}
		/* Generate a correlation entry if there are multiple values */
		if (values_cnt > 1) {
			MemoryContext   old_context;
			float4         *corrs;
			double          corr_xsum, corr_x2sum;

			/* Must copy the target values into anl_context */
			old_context = MemoryContextSwitchTo(anl_context);
			corrs = (float4 *) palloc(sizeof(float4));
			MemoryContextSwitchTo(old_context);

			/*----------
			 * Since we know the x and y value sets are both
			 *		0, 1, ..., values_cnt-1
			 * we have sum(x) = sum(y) =
			 *		(values_cnt-1)*values_cnt / 2
			 * and sum(x^2) = sum(y^2) =
			 *		(values_cnt-1)*values_cnt*(2*values_cnt-1) / 6.
			 *----------
			 */
			corr_xsum = ((double) (values_cnt - 1)) *
				((double) values_cnt) / 2.0;
			corr_x2sum = ((double) (values_cnt - 1)) *
				((double) values_cnt) * (double) (2 * values_cnt - 1) / 6.0;

			/* And the correlation coefficient reduces to */
			corrs[0] = (values_cnt * corr_xysum - corr_xsum * corr_xsum) /
				(values_cnt * corr_x2sum - corr_xsum * corr_xsum);

			stats->stakind[slot_idx] = 2;
			stats->staop[slot_idx] = stats->op_cmplt;
			stats->stanumbers[slot_idx] = corrs;
			stats->numnumbers[slot_idx] = 1;
			slot_idx++;
		}
	}
	/* We don't need to bother cleaning up any of our temporary palloc's */
}

/*
 * qsort comparator for sorting ScalarItems
 * 
 * Aside from sorting the items, we update the datumCmpTupnoLink[] array
 * whenever two ScalarItems are found to contain equal datums.	The array is
 * indexed by tupno; for each ScalarItem, it contains the highest tupno that
 * that item's datum has been found to be equal to.  This allows us to avoid
 * additional comparisons in compute_scalar_stats().
 */
static int
compare_scalars(const void *a, const void *b)
{
	Datum           da = ((ScalarItem *) a)->value;
	int             ta = ((ScalarItem *) a)->tupno;
	Datum           db = ((ScalarItem *) b)->value;
	int             tb = ((ScalarItem *) b)->tupno;
	int32           compare;
	AnalyzeGlobals *ag = AnalyzeGetEnv();

	compare = ApplySortFunction(ag->datumCmpFn, ag->datumCmpFnKind,
				    da, false, db, false);


	if (compare != 0)
		return compare;

	/*
	 * The two datums are equal, so update datumCmpTupnoLink[].
	 */
	if (ag->datumCmpTupnoLink[ta] < tb)
		ag->datumCmpTupnoLink[ta] = tb;
	if (ag->datumCmpTupnoLink[tb] < ta)
		ag->datumCmpTupnoLink[tb] = ta;

	/*
	 * For equal datums, sort by tupno
	 */
	return ta - tb;
}

/*
 * qsort comparator for sorting ScalarMCVItems by position
 */
static int
compare_mcvs(const void *a, const void *b)
{
	int             da = ((ScalarMCVItem *) a)->first;
	int             db = ((ScalarMCVItem *) b)->first;

	return da - db;
}


/*
 * update_attstats() -- update attribute statistics for one relation
 * 
 * Statistics are stored in several places: the pg_class row for the relation
 * has stats about the whole relation, and there is a pg_statistic row for
 * each (non-system) attribute that has ever been analyzed.	The pg_class
 * values are updated by VACUUM, not here.
 * 
 * pg_statistic rows are just added or updated normally.  This means that
 * pg_statistic will probably contain some deleted rows at the completion of
 * a vacuum cycle, unless it happens to get vacuumed last.
 * 
 * To keep things simple, we punt for pg_statistic, and don't try to compute or
 * store rows for pg_statistic itself in pg_statistic. This could possibly be
 * made to work, but it's not worth the trouble. Note analyze_rel() has seen
 * to it that we won't come here when vacuuming pg_statistic itself.
 * 
 * Note: if two backends concurrently try to analyze the same relation, the
 * second one is likely to fail here with a "tuple concurrently updated"
 * error.  This is slightly annoying, but no real harm is done. We could
 * prevent the problem by using a stronger lock on the relation for ANALYZE
 * (ie, ShareUpdateExclusiveLock instead of AccessShareLock); but that cure
 * seems worse than the disease, especially now that ANALYZE doesn't start a
 * new transaction for each relation.	The lock could be held for a long
 * time...
 */
static void
update_attstats(Oid relid, int natts, AnalyzeAttrStats ** vacattrstats)
{
	Relation        sd;
	int             attno;

	sd = heap_openr(StatisticRelationName, RowExclusiveLock);

	for (attno = 0; attno < natts; attno++) {
		AnalyzeAttrStats *stats = vacattrstats[attno];
		HeapTuple       stup, oldtup;
		int             i, k;
                wrapped_datum   commonval,hival,loval;

		Datum           values[Natts_pg_statistic];
		char            nulls[Natts_pg_statistic];
		char            replaces[Natts_pg_statistic];


		/* Ignore attr if we weren't able to collect stats */
		if (!stats->stats_valid || stats->algcode == ALG_NONE)
			continue;

                commonval.type = stats->attr->atttypid;
                hival.type = stats->attr->atttypid;
		loval.type = stats->attr->atttypid;
/*
		 * Construct a new pg_statistic tuple
		 */
		for (i = 0; i < Natts_pg_statistic; ++i) {
			nulls[i] = ' ';
			replaces[i] = ' ';
		}

		i = 0;
		replaces[i] = 'r';
		values[i++] = ObjectIdGetDatum(relid);	/* starelid */
		replaces[i] = 'r';
		values[i++] = Int16GetDatum(stats->attnum);	/* staattnum */
		replaces[i] = 'r';
		values[i++] = ObjectIdGetDatum(stats->op_cmplt);	/* staop */
		replaces[i] = 'r';
		values[i++] = (Datum) & stats->stanullfrac;	/* stanullfrac;	 */

		/*
		 * THE REST OF THESE ARE VARIABLE LENGTH FIELDS. They cannot
		 * be accessed as C struct entries; you have to use the full
		 * field access machinery (heap_getattr) for them.
		 * 
		 * All three of these are text representations of data values of
		 * the column's data type.  To re-create the actual Datum, do
		 * datatypein(textout(givenvalue)).
		 */
		nulls[i] = 'n';
		nulls[i + 1] = 'n';
		nulls[i + 2] = 'n';
		nulls[i + 3] = 'n';

		for (k = 0; k < STATISTIC_NUM_SLOTS; k++) {
			float4          cv = 0.0;
			FmgrValues      args;

			if (stats->stakind[k] == 0) {

				int             n;

				for (n = 0; n < stats->numnumbers[k]; n++) {
					if (stats->stanumbers[k][n] <= cv)
						continue;

					nulls[i] = ' ';
					replaces[i] = 'r';
					values[i] = (Datum) & stats->stanumbers[k][n];	/* the fraction that are
											 * the most common * val */

                                       commonval.value = stats->stavalues[k][n];

                                        nulls[i + 1] = ' ';
                                        replaces[i + 1] = 'r';
                                        values[i + 1] = (Datum) wrappedin(&commonval);	/* stacommonval */

					cv = stats->stanumbers[k][n];
				}
				if (stats->algcode == ALG_MINIMAL) {
					values[i - 2] = ObjectIdGetDatum(stats->staop[k]);	/* staop */
				}
			}
			if (stats->stakind[k] == 1) {

				values[i - 2] = ObjectIdGetDatum(stats->staop[k]);	/* staop */

                                loval.value = stats->stavalues[k][0];

                                nulls[i + 2] = ' ';
                                replaces[i + 2] = 'r';
                                values[i + 2] = (Datum) wrappedin(&loval);

				/* hival  */
				hival.value = stats->stavalues[k][stats->numvalues[k] - 1];

                                nulls[i + 3] = ' ';
                                replaces[i + 3] = 'r';
                                values[i + 3] = (Datum) wrappedin(&hival);
			}
		}

		/* Is there already a pg_statistic tuple for this attribute? */
		oldtup = SearchSysCacheTuple(STATRELID,
					     ObjectIdGetDatum(relid),
					     Int16GetDatum(stats->attnum),
					     0, 0);

		if (HeapTupleIsValid(oldtup)) {
			/* Yes, replace it */
			stup = heap_modifytuple(oldtup,
						sd,
						values,
						nulls,
						replaces);

			heap_update(sd, &oldtup->t_self, stup, &stup->t_self, NULL);
		} else {
			/* No, insert new tuple */
			stup = heap_formtuple(sd->rd_att, values, nulls);
			heap_insert(sd, stup);
		}

		{
			Relation        irelations[Num_pg_statistic_indices];
			CatalogOpenIndices(Num_pg_statistic_indices, Name_pg_statistic_indices, irelations);
			CatalogIndexInsert(irelations, Num_pg_statistic_indices, sd, stup);
			CatalogCloseIndices(Num_pg_statistic_indices, irelations);
		}

		heap_freetuple(stup);
	}

	heap_close(sd, RowExclusiveLock);
}


AnalyzeGlobals *
AnalyzeInitEnv(void)
{
        analyze_globals = (AnalyzeGlobals *) palloc(sizeof(AnalyzeGlobals));
        return analyze_globals;
}

void
AnalyzeDestroyEnv(void)
{
	pfree(analyze_globals);
        analyze_globals = NULL;
}

AnalyzeGlobals *
AnalyzeGetEnv(void)
{
	return (AnalyzeGlobals *) analyze_globals;
}

void  analyze_log(Relation rel, char* pattern, ...) {
    char            msg[256];
    va_list         args;

    va_start(args, pattern);
    vsprintf(msg,pattern,args);
#ifdef SUNOS
    DTRACE_PROBE3(mtpg,analyze__msg,msg,RelationGetRelid(rel),GetDatabaseId());  
#endif
#ifdef DEBUGLOGS
    elog(DEBUG,"analyze: %d/%d %s",RelationGetRelid(rel),GetDatabaseId(),msg);
#endif
    va_end(args);
}

