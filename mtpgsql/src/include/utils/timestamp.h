/*-------------------------------------------------------------------------
 *
 * timestamp.h
 *	  Definitions for the SQL92 "timestamp" and "interval" types.
 *
 * Portions Copyright (c) 2000-2024, Myron Scott  <myron@weaverdb.org>
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 *
 *-------------------------------------------------------------------------
 */
#ifndef TIMESTAMP_H
#define TIMESTAMP_H

#include <time.h>
/* #include <math.h> */
#include <limits.h>

/*
 * Timestamp represents absolute time.
 * Interval represents delta time. Keep track of months (and years)
 *	separately since the elapsed time spanned is unknown until instantiated
 *	relative to an absolute time.
 *
 * Note that Postgres uses "time interval" to mean a bounded interval,
 *	consisting of a beginning and ending time, not a time span - thomas 97/03/20
 */

typedef double Timestamp;

typedef struct
{
	double		time;			/* all time units other than months and
								 * years */
	int4		month;			/* months and years, after time for
								 * alignment */
} Interval;


#ifdef NAN
#define DT_INVALID		(NAN)
#else
#define DT_INVALID		(DBL_MIN+DBL_MIN)
#endif
#ifdef HUGE_VAL
#define DT_NOBEGIN		(-HUGE_VAL)
#define DT_NOEND		(HUGE_VAL)
#else
#define DT_NOBEGIN		(-DBL_MAX)
#define DT_NOEND		(DBL_MAX)
#endif
#define DT_CURRENT		(DBL_MIN)
#define DT_EPOCH		(-DBL_MIN)

#define TIMESTAMP_INVALID(j)		do {j = DT_INVALID;} while (0)
#ifdef NAN
#define TIMESTAMP_IS_INVALID(j) (isnan(j))
#else
#define TIMESTAMP_IS_INVALID(j) (j == DT_INVALID)
#endif

#define TIMESTAMP_NOBEGIN(j)		do {j = DT_NOBEGIN;} while (0)
#define TIMESTAMP_IS_NOBEGIN(j) (j == DT_NOBEGIN)

#define TIMESTAMP_NOEND(j)		do {j = DT_NOEND;} while (0)
#define TIMESTAMP_IS_NOEND(j)	(j == DT_NOEND)

#define TIMESTAMP_CURRENT(j)		do {j = DT_CURRENT;} while (0)
#if defined(linux) && defined(__powerpc__)
PG_EXTERN int	timestamp_is_current(double j);

#define TIMESTAMP_IS_CURRENT(j) timestamp_is_current(j)
#else
#define TIMESTAMP_IS_CURRENT(j) (j == DT_CURRENT)
#endif

#define TIMESTAMP_EPOCH(j)		do {j = DT_EPOCH;} while (0)
#if defined(linux) && defined(__powerpc__)
PG_EXTERN int	timestamp_is_epoch(double j);

#define TIMESTAMP_IS_EPOCH(j)	timestamp_is_epoch(j)
#else
#define TIMESTAMP_IS_EPOCH(j)	(j == DT_EPOCH)
#endif

#define TIMESTAMP_IS_RELATIVE(j) (TIMESTAMP_IS_CURRENT(j) || TIMESTAMP_IS_EPOCH(j))
#define TIMESTAMP_NOT_FINITE(j) (TIMESTAMP_IS_INVALID(j) \
								|| TIMESTAMP_IS_NOBEGIN(j) || TIMESTAMP_IS_NOEND(j))
#define TIMESTAMP_IS_RESERVED(j) (TIMESTAMP_IS_RELATIVE(j) || TIMESTAMP_NOT_FINITE(j))

#define INTERVAL_INVALID(j)		do {(j).time = DT_INVALID;} while (0)
#ifdef NAN
#define INTERVAL_IS_INVALID(j)	(isnan((j).time))
#else
#define INTERVAL_IS_INVALID(j)	((j).time == DT_INVALID)
#endif
#define INTERVAL_NOT_FINITE(j)	INTERVAL_IS_INVALID(j)

#define TIME_PREC_INV 1000000.0
#define JROUND(j) (rint(((double) (j))*TIME_PREC_INV)/TIME_PREC_INV)


/*
 * timestamp.c prototypes
 */

PG_EXTERN Timestamp *timestamp_in(char *str);
PG_EXTERN char *timestamp_out(Timestamp *dt);
PG_EXTERN bool timestamp_eq(Timestamp *dt1, Timestamp *dt2);
PG_EXTERN bool timestamp_ne(Timestamp *dt1, Timestamp *dt2);
PG_EXTERN bool timestamp_lt(Timestamp *dt1, Timestamp *dt2);
PG_EXTERN bool timestamp_le(Timestamp *dt1, Timestamp *dt2);
PG_EXTERN bool timestamp_ge(Timestamp *dt1, Timestamp *dt2);
PG_EXTERN bool timestamp_gt(Timestamp *dt1, Timestamp *dt2);
PG_EXTERN bool timestamp_finite(Timestamp *timestamp);
PG_EXTERN int	timestamp_cmp(Timestamp *dt1, Timestamp *dt2);
PG_EXTERN Timestamp *timestamp_smaller(Timestamp *dt1, Timestamp *dt2);
PG_EXTERN Timestamp *timestamp_larger(Timestamp *dt1, Timestamp *dt2);

PG_EXTERN Interval *interval_in(char *str);
PG_EXTERN char *interval_out(Interval *span);
PG_EXTERN bool interval_eq(Interval *span1, Interval *span2);
PG_EXTERN bool interval_ne(Interval *span1, Interval *span2);
PG_EXTERN bool interval_lt(Interval *span1, Interval *span2);
PG_EXTERN bool interval_le(Interval *span1, Interval *span2);
PG_EXTERN bool interval_ge(Interval *span1, Interval *span2);
PG_EXTERN bool interval_gt(Interval *span1, Interval *span2);
PG_EXTERN bool interval_finite(Interval *span);
PG_EXTERN int	interval_cmp(Interval *span1, Interval *span2);
PG_EXTERN Interval *interval_smaller(Interval *span1, Interval *span2);
PG_EXTERN Interval *interval_larger(Interval *span1, Interval *span2);

PG_EXTERN text *timestamp_text(Timestamp *timestamp);
PG_EXTERN Timestamp *text_timestamp(text *str);
PG_EXTERN text *interval_text(Interval *interval);
PG_EXTERN Interval *text_interval(text *str);
PG_EXTERN Timestamp *timestamp_trunc(text *units, Timestamp *timestamp);
PG_EXTERN Interval *interval_trunc(text *units, Interval *interval);
PG_EXTERN float64 timestamp_part(text *units, Timestamp *timestamp);
PG_EXTERN float64 interval_part(text *units, Interval *interval);
PG_EXTERN text *timestamp_zone(text *zone, Timestamp *timestamp);

PG_EXTERN Interval *interval_um(Interval *span);
PG_EXTERN Interval *interval_pl(Interval *span1, Interval *span2);
PG_EXTERN Interval *interval_mi(Interval *span1, Interval *span2);
PG_EXTERN Interval *interval_mul(Interval *span1, float8 *factor);
PG_EXTERN Interval *mul_d_interval(float8 *factor, Interval *span1);
PG_EXTERN Interval *interval_div(Interval *span1, float8 *factor);

PG_EXTERN Interval *timestamp_mi(Timestamp *dt1, Timestamp *dt2);
PG_EXTERN Timestamp *timestamp_pl_span(Timestamp *dt, Interval *span);
PG_EXTERN Timestamp *timestamp_mi_span(Timestamp *dt, Interval *span);
PG_EXTERN Interval *timestamp_age(Timestamp *dt1, Timestamp *dt2);
PG_EXTERN bool overlaps_timestamp(Timestamp *dt1, Timestamp *dt2, Timestamp *dt3, Timestamp *dt4);

PG_EXTERN int	tm2timestamp(struct tm * tm, double fsec, int *tzp, Timestamp *dt);
PG_EXTERN int	timestamp2tm(Timestamp dt, int *tzp, struct tm * tm, double *fsec, char **tzn);

PG_EXTERN Timestamp SetTimestamp(Timestamp timestamp);
PG_EXTERN Timestamp dt2local(Timestamp dt, int timezone);
PG_EXTERN void dt2time(Timestamp dt, int *hour, int *min, double *sec);
PG_EXTERN int	EncodeSpecialTimestamp(Timestamp dt, char *str);
PG_EXTERN int	interval2tm(Interval span, struct tm * tm, float8 *fsec);
PG_EXTERN int	tm2interval(struct tm * tm, double fsec, Interval *span);
PG_EXTERN Timestamp *now(void);

#endif	 /* TIMESTAMP_H */
