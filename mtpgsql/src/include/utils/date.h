/*-------------------------------------------------------------------------
 *
 * date.h
 *	  Definitions for the SQL92 "date" and "time" types.
 *
 *
 * Portions Copyright (c) 2000-2024, Myron Scott  <myron@weaverdb.org>
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 *
 *-------------------------------------------------------------------------
 */
#ifndef DATE_H
#define DATE_H

typedef int32 DateADT;

typedef float8 TimeADT;

typedef struct
{
	double		time;			/* all time units other than months and
								 * years */
	int4		zone;			/* numeric time zone, in seconds */
} TimeTzADT;

/* date.c */
PG_EXTERN DateADT date_in(char *datestr);
PG_EXTERN char *date_out(DateADT dateVal);
PG_EXTERN bool date_eq(DateADT dateVal1, DateADT dateVal2);
PG_EXTERN bool date_ne(DateADT dateVal1, DateADT dateVal2);
PG_EXTERN bool date_lt(DateADT dateVal1, DateADT dateVal2);
PG_EXTERN bool date_le(DateADT dateVal1, DateADT dateVal2);
PG_EXTERN bool date_gt(DateADT dateVal1, DateADT dateVal2);
PG_EXTERN bool date_ge(DateADT dateVal1, DateADT dateVal2);
PG_EXTERN int	date_cmp(DateADT dateVal1, DateADT dateVal2);
PG_EXTERN DateADT date_larger(DateADT dateVal1, DateADT dateVal2);
PG_EXTERN DateADT date_smaller(DateADT dateVal1, DateADT dateVal2);
PG_EXTERN int32 date_mi(DateADT dateVal1, DateADT dateVal2);
PG_EXTERN DateADT date_pli(DateADT dateVal, int32 days);
PG_EXTERN DateADT date_mii(DateADT dateVal, int32 days);
PG_EXTERN Timestamp *date_timestamp(DateADT date);
PG_EXTERN DateADT timestamp_date(Timestamp *timestamp);
PG_EXTERN Timestamp *datetime_timestamp(DateADT date, TimeADT *time);
PG_EXTERN DateADT abstime_date(AbsoluteTime abstime);

PG_EXTERN TimeADT *time_in(char *timestr);
PG_EXTERN char *time_out(TimeADT *time);
PG_EXTERN bool time_eq(TimeADT *time1, TimeADT *time2);
PG_EXTERN bool time_ne(TimeADT *time1, TimeADT *time2);
PG_EXTERN bool time_lt(TimeADT *time1, TimeADT *time2);
PG_EXTERN bool time_le(TimeADT *time1, TimeADT *time2);
PG_EXTERN bool time_gt(TimeADT *time1, TimeADT *time2);
PG_EXTERN bool time_ge(TimeADT *time1, TimeADT *time2);
PG_EXTERN int	time_cmp(TimeADT *time1, TimeADT *time2);
PG_EXTERN bool overlaps_time(TimeADT *time1, TimeADT *time2,
			  TimeADT *time3, TimeADT *time4);
PG_EXTERN TimeADT *time_larger(TimeADT *time1, TimeADT *time2);
PG_EXTERN TimeADT *time_smaller(TimeADT *time1, TimeADT *time2);
PG_EXTERN TimeADT *timestamp_time(Timestamp *timestamp);
PG_EXTERN Interval *time_interval(TimeADT *time);

PG_EXTERN TimeTzADT *timetz_in(char *timestr);
PG_EXTERN char *timetz_out(TimeTzADT *time);
PG_EXTERN bool timetz_eq(TimeTzADT *time1, TimeTzADT *time2);
PG_EXTERN bool timetz_ne(TimeTzADT *time1, TimeTzADT *time2);
PG_EXTERN bool timetz_lt(TimeTzADT *time1, TimeTzADT *time2);
PG_EXTERN bool timetz_le(TimeTzADT *time1, TimeTzADT *time2);
PG_EXTERN bool timetz_gt(TimeTzADT *time1, TimeTzADT *time2);
PG_EXTERN bool timetz_ge(TimeTzADT *time1, TimeTzADT *time2);
PG_EXTERN int	timetz_cmp(TimeTzADT *time1, TimeTzADT *time2);
PG_EXTERN bool overlaps_timetz(TimeTzADT *time1, TimeTzADT *time2,
				TimeTzADT *time3, TimeTzADT *time4);
PG_EXTERN TimeTzADT *timetz_larger(TimeTzADT *time1, TimeTzADT *time2);
PG_EXTERN TimeTzADT *timetz_smaller(TimeTzADT *time1, TimeTzADT *time2);
PG_EXTERN TimeTzADT *timestamp_timetz(Timestamp *timestamp);
PG_EXTERN Timestamp *datetimetz_timestamp(DateADT date, TimeTzADT *time);

#endif	 /* DATE_H */
