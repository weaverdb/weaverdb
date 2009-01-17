/*-------------------------------------------------------------------------
 *
 * nabstime.h
 *	  Definitions for the "new" abstime code.
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * $Id: nabstime.h,v 1.1.1.1 2006/08/12 00:22:27 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */
#ifndef NABSTIME_H
#define NABSTIME_H

#include <time.h>
#include "utils/timestamp.h"
#include "utils/datetime.h"


/* ----------------------------------------------------------------
 *				time types + support macros
 *
 *
 * ----------------------------------------------------------------
 */
/*
 * Although time_t generally is a long int on 64 bit systems, these two
 * types must be 4 bytes, because that's what the system assumes. They
 * should be yanked (long) before 2038 and be replaced by timestamp and
 * interval.
 */
typedef int32 AbsoluteTime;
typedef int32 RelativeTime;

typedef struct
{
	int32		status;
	AbsoluteTime data[2];
} TimeIntervalData;
typedef TimeIntervalData *TimeInterval;

/*
 * Reserved values
 * Epoch is Unix system time zero, but needs to be kept as a reserved
 *	value rather than converting to time since timezone calculations
 *	might move it away from 1970-01-01 00:00:00Z - tgl 97/02/20
 *
 * Pre-v6.1 code had large decimal numbers for reserved values.
 * These were chosen as special 32-bit bit patterns,
 *	so redefine them explicitly using these bit patterns. - tgl 97/02/24
 */
#define EPOCH_ABSTIME	((AbsoluteTime) 0)
#define INVALID_ABSTIME ((AbsoluteTime) 0x7FFFFFFE)		/* 2147483647 (2^31 - 1) */
#define CURRENT_ABSTIME ((AbsoluteTime) 0x7FFFFFFD)		/* 2147483646 (2^31 - 2) */
#define NOEND_ABSTIME	((AbsoluteTime) 0x7FFFFFFC)		/* 2147483645 (2^31 - 3) */
#define BIG_ABSTIME		((AbsoluteTime) 0x7FFFFFFB)		/* 2147483644 (2^31 - 4) */

#if defined(_AIX)
/*
 * AIX considers 2147483648 == -2147483648 (since they have the same bit
 * representation) but uses a different sign sense in a comparison to
 * these integer constants depending on whether the constant is signed
 * or not!
 */
#define NOSTART_ABSTIME		 ((AbsoluteTime) INT_MIN)
#else
#define NOSTART_ABSTIME ((AbsoluteTime) 0x80000001)		/* -2147483647 (- 2^31) */
#endif	 /* _AIX */

#define INVALID_RELTIME ((RelativeTime) 0x7FFFFFFE)		/* 2147483647 (2^31 - 1) */

#define AbsoluteTimeIsValid(time) \
	((bool) ((time) != INVALID_ABSTIME))

#define AbsoluteTimeIsReal(time) \
	((bool) (((AbsoluteTime) time) < NOEND_ABSTIME && \
			 ((AbsoluteTime) time) > NOSTART_ABSTIME))

/* have to include this because EPOCH_ABSTIME used to be invalid - yuk */
#define AbsoluteTimeIsBackwardCompatiblyValid(time) \
	((bool) (((AbsoluteTime) time) != INVALID_ABSTIME && \
			 ((AbsoluteTime) time) > EPOCH_ABSTIME))

#define AbsoluteTimeIsBackwardCompatiblyReal(time) \
	((bool) (((AbsoluteTime) time) < NOEND_ABSTIME && \
			 ((AbsoluteTime) time) > NOSTART_ABSTIME && \
			 ((AbsoluteTime) time) > EPOCH_ABSTIME))

#define RelativeTimeIsValid(time) \
	((bool) (((RelativeTime) time) != INVALID_RELTIME))

PG_EXTERN AbsoluteTime GetCurrentAbsoluteTime(void);

/*
 * getSystemTime
 *		Returns system time.
 */
#define getSystemTime() \
	((time_t) (time(0l)))


/*
 * nabstime.c prototypes
 */
PG_EXTERN AbsoluteTime nabstimein(char *timestr);
PG_EXTERN char *nabstimeout(AbsoluteTime time);

PG_EXTERN bool abstimeeq(AbsoluteTime t1, AbsoluteTime t2);
PG_EXTERN bool abstimene(AbsoluteTime t1, AbsoluteTime t2);
PG_EXTERN bool abstimelt(AbsoluteTime t1, AbsoluteTime t2);
PG_EXTERN bool abstimegt(AbsoluteTime t1, AbsoluteTime t2);
PG_EXTERN bool abstimele(AbsoluteTime t1, AbsoluteTime t2);
PG_EXTERN bool abstimege(AbsoluteTime t1, AbsoluteTime t2);
PG_EXTERN bool abstime_finite(AbsoluteTime time);

PG_EXTERN AbsoluteTime timestamp_abstime(Timestamp *timestamp);
PG_EXTERN Timestamp *abstime_timestamp(AbsoluteTime abstime);

PG_EXTERN bool AbsoluteTimeIsBefore(AbsoluteTime time1, AbsoluteTime time2);

PG_EXTERN void abstime2tm(AbsoluteTime time, int *tzp, struct tm * tm, char *tzn);

PG_EXTERN RelativeTime reltimein(char *timestring);
PG_EXTERN char *reltimeout(RelativeTime timevalue);
PG_EXTERN TimeInterval tintervalin(char *intervalstr);
PG_EXTERN char *tintervalout(TimeInterval interval);
PG_EXTERN RelativeTime interval_reltime(Interval *interval);
PG_EXTERN Interval *reltime_interval(RelativeTime reltime);
PG_EXTERN TimeInterval mktinterval(AbsoluteTime t1, AbsoluteTime t2);
PG_EXTERN AbsoluteTime timepl(AbsoluteTime t1, RelativeTime t2);
PG_EXTERN AbsoluteTime timemi(AbsoluteTime t1, RelativeTime t2);

/* PG_EXTERN RelativeTime abstimemi(AbsoluteTime t1, AbsoluteTime t2);  static*/
PG_EXTERN int	intinterval(AbsoluteTime t, TimeInterval interval);
PG_EXTERN RelativeTime tintervalrel(TimeInterval interval);
PG_EXTERN AbsoluteTime timenow(void);
PG_EXTERN bool reltimeeq(RelativeTime t1, RelativeTime t2);
PG_EXTERN bool reltimene(RelativeTime t1, RelativeTime t2);
PG_EXTERN bool reltimelt(RelativeTime t1, RelativeTime t2);
PG_EXTERN bool reltimegt(RelativeTime t1, RelativeTime t2);
PG_EXTERN bool reltimele(RelativeTime t1, RelativeTime t2);
PG_EXTERN bool reltimege(RelativeTime t1, RelativeTime t2);
PG_EXTERN bool tintervalsame(TimeInterval i1, TimeInterval i2);
PG_EXTERN bool tintervaleq(TimeInterval i1, TimeInterval i2);
PG_EXTERN bool tintervalne(TimeInterval i1, TimeInterval i2);
PG_EXTERN bool tintervallt(TimeInterval i1, TimeInterval i2);
PG_EXTERN bool tintervalgt(TimeInterval i1, TimeInterval i2);
PG_EXTERN bool tintervalle(TimeInterval i1, TimeInterval i2);
PG_EXTERN bool tintervalge(TimeInterval i1, TimeInterval i2);
PG_EXTERN bool tintervalleneq(TimeInterval i, RelativeTime t);
PG_EXTERN bool tintervallenne(TimeInterval i, RelativeTime t);
PG_EXTERN bool tintervallenlt(TimeInterval i, RelativeTime t);
PG_EXTERN bool tintervallengt(TimeInterval i, RelativeTime t);
PG_EXTERN bool tintervallenle(TimeInterval i, RelativeTime t);
PG_EXTERN bool tintervallenge(TimeInterval i, RelativeTime t);
PG_EXTERN bool tintervalct(TimeInterval i1, TimeInterval i2);
PG_EXTERN bool tintervalov(TimeInterval i1, TimeInterval i2);
PG_EXTERN AbsoluteTime tintervalstart(TimeInterval i);
PG_EXTERN AbsoluteTime tintervalend(TimeInterval i);
PG_EXTERN int32 int4reltime(int32 timevalue);
PG_EXTERN text *timeofday(void);

#endif	 /* NABSTIME_H */
