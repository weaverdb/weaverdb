/*-------------------------------------------------------------------------
 *
 * datetime.h
 *	  Definitions for the date/time and other date/time support code.
 *	  The support code is shared with other date data types,
 *	   including abstime, reltime, date, and time.
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
#ifndef DATETIME_H
#define DATETIME_H

#include <time.h>
/* #include <math.h> */
#include <limits.h>
#include "utils/timestamp.h"


/* ----------------------------------------------------------------
 *				time types + support macros
 *
 * String definitions for standard time quantities.
 *
 * These strings are the defaults used to form output time strings.
 * Other alternate forms are hardcoded into token tables in datetime.c.
 * ----------------------------------------------------------------
 */

#define DAGO			"ago"
#define DCURRENT		"current"
#define EPOCH			"epoch"
#define INVALID			"invalid"
#define EARLY			"-infinity"
#define LATE			"infinity"
#define NOW				"now"
#define TODAY			"today"
#define TOMORROW		"tomorrow"
#define YESTERDAY		"yesterday"
#define ZULU			"zulu"

#define DMICROSEC		"usecond"
#define DMILLISEC		"msecond"
#define DSECOND			"second"
#define DMINUTE			"minute"
#define DHOUR			"hour"
#define DDAY			"day"
#define DWEEK			"week"
#define DMONTH			"month"
#define DQUARTER		"quarter"
#define DYEAR			"year"
#define DDECADE			"decade"
#define DCENTURY		"century"
#define DMILLENNIUM		"millennium"
#define DA_D			"ad"
#define DB_C			"bc"
#define DTIMEZONE		"timezone"

/*
 * Fundamental time field definitions for parsing.
 *
 *	Meridian:  am, pm, or 24-hour style.
 *	Millennium: ad, bc
 */

#define AM		0
#define PM		1
#define HR24	2

#define AD		0
#define BC		1

/*
 * Fields for time decoding.
 * Can't have more of these than there are bits in an unsigned int
 *	since these are turned into bit masks during parsing and decoding.
 */

#define RESERV	0
#define MONTH	1
#define YEAR	2
#define DAY		3
#define TIMES	4				/* not used - thomas 1997-07-14 */
#define TZ		5
#define DTZ		6
#define DTZMOD	7
#define IGNORE	8
#define AMPM	9
#define HOUR	10
#define MINUTE	11
#define SECOND	12
#define DOY		13
#define DOW		14
#define UNITS	15
#define ADBC	16
/* these are only for relative dates */
#define AGO		17
#define ABS_BEFORE		18
#define ABS_AFTER		19

/*
 * Token field definitions for time parsing and decoding.
 * These need to fit into the datetkn table type.
 * At the moment, that means keep them within [-127,127].
 * These are also used for bit masks in DecodeDateDelta()
 *	so actually restrict them to within [0,31] for now.
 * - thomas 97/06/19
 * Not all of these fields are used for masks in DecodeDateDelta
 *	so allow some larger than 31. - thomas 1997-11-17
 */

#define DTK_NUMBER		0
#define DTK_STRING		1

#define DTK_DATE		2
#define DTK_TIME		3
#define DTK_TZ			4
#define DTK_AGO			5

#define DTK_SPECIAL		6
#define DTK_INVALID		7
#define DTK_CURRENT		8
#define DTK_EARLY		9
#define DTK_LATE		10
#define DTK_EPOCH		11
#define DTK_NOW			12
#define DTK_YESTERDAY	13
#define DTK_TODAY		14
#define DTK_TOMORROW	15
#define DTK_ZULU		16

#define DTK_DELTA		17
#define DTK_SECOND		18
#define DTK_MINUTE		19
#define DTK_HOUR		20
#define DTK_DAY			21
#define DTK_WEEK		22
#define DTK_MONTH		23
#define DTK_QUARTER		24
#define DTK_YEAR		25
#define DTK_DECADE		26
#define DTK_CENTURY		27
#define DTK_MILLENNIUM	28
#define DTK_MILLISEC	29
#define DTK_MICROSEC	30

#define DTK_DOW			32
#define DTK_DOY			33
#define DTK_TZ_HOUR		34
#define DTK_TZ_MINUTE	35

/*
 * Bit mask definitions for time parsing.
 */

#define DTK_M(t)		(0x01 << (t))

#define DTK_DATE_M		(DTK_M(YEAR) | DTK_M(MONTH) | DTK_M(DAY))
#define DTK_TIME_M		(DTK_M(HOUR) | DTK_M(MINUTE) | DTK_M(SECOND))

#define MAXDATELEN		51		/* maximum possible length of an input
								 * date string (not counting tr. null) */
#define MAXDATEFIELDS	25		/* maximum possible number of fields in a
								 * date string */
#define TOKMAXLEN		10		/* only this many chars are stored in
								 * datetktbl */

/* keep this struct small; it gets used a lot */
typedef struct
{
#if defined(_AIX)
	char	   *token;
#else
	char		token[TOKMAXLEN];
#endif	 /* _AIX */
	char		type;
	char		value;			/* this may be unsigned, alas */
} datetkn;


/* TMODULO()
 * Macro to replace modf(), which is broken on some platforms.
 */
#define TMODULO(t,q,u) \
do { \
	q = ((t < 0)? ceil(t / u): floor(t / u)); \
	if (q != 0) \
		t -= rint(q * u); \
} while(0)


/*
 * Date/time validation
 * Include check for leap year.
 */

extern int	day_tab[2][13];

#define isleap(y) (((y) % 4) == 0 && (((y) % 100) != 0 || ((y) % 400) == 0))

/* Julian date support for date2j() and j2date()
 * Set the minimum year to one greater than the year of the first valid day
 *	to avoid having to check year and day both. - tgl 97/05/08
 */

#define JULIAN_MINYEAR (-4713)
#define JULIAN_MINMONTH (11)
#define JULIAN_MINDAY (23)

#define IS_VALID_JULIAN(y,m,d) ((y > JULIAN_MINYEAR) \
 || ((y == JULIAN_MINYEAR) && ((m > JULIAN_MINMONTH) \
  || ((m == JULIAN_MINMONTH) && (d >= JULIAN_MINDAY)))))

#define UTIME_MINYEAR (1901)
#define UTIME_MINMONTH (12)
#define UTIME_MINDAY (14)
#define UTIME_MAXYEAR (2038)
#define UTIME_MAXMONTH (01)
#define UTIME_MAXDAY (18)

#define IS_VALID_UTIME(y,m,d) (((y > UTIME_MINYEAR) \
 || ((y == UTIME_MINYEAR) && ((m > UTIME_MINMONTH) \
  || ((m == UTIME_MINMONTH) && (d >= UTIME_MINDAY))))) \
 && ((y < UTIME_MAXYEAR) \
 || ((y == UTIME_MAXYEAR) && ((m < UTIME_MAXMONTH) \
  || ((m == UTIME_MAXMONTH) && (d <= UTIME_MAXDAY))))))


PG_EXTERN void GetCurrentTime(struct tm * tm);
PG_EXTERN void j2date(int jd, int *year, int *month, int *day);
PG_EXTERN int	date2j(int year, int month, int day);

PG_EXTERN int ParseDateTime(char *timestr, char *lowstr,
			  char **field, int *ftype,
			  int maxfields, int *numfields);
PG_EXTERN int DecodeDateTime(char **field, int *ftype,
			   int nf, int *dtype,
			   struct tm * tm, double *fsec, int *tzp);

PG_EXTERN int DecodeTimeOnly(char **field, int *ftype,
			   int nf, int *dtype,
			   struct tm * tm, double *fsec, int *tzp);

PG_EXTERN int DecodeDateDelta(char **field, int *ftype,
				int nf, int *dtype,
				struct tm * tm, double *fsec);

PG_EXTERN int	EncodeDateOnly(struct tm * tm, int style, char *str);
PG_EXTERN int	EncodeTimeOnly(struct tm * tm, double fsec, int *tzp, int style, char *str);
PG_EXTERN int	EncodeDateTime(struct tm * tm, double fsec, int *tzp, char **tzn, int style, char *str);
PG_EXTERN int	EncodeTimeSpan(struct tm * tm, double fsec, int style, char *str);

PG_EXTERN int	DecodeDate(char *str, int fmask, int *tmask, struct tm * tm);
PG_EXTERN int DecodeNumber(int flen, char *field,
			 int fmask, int *tmask,
			 struct tm * tm, double *fsec, int *is2digits);
PG_EXTERN int DecodeNumberField(int len, char *str,
				  int fmask, int *tmask,
				  struct tm * tm, double *fsec, int *is2digits);
PG_EXTERN int	DecodeSpecial(int field, char *lowtoken, int *val);
PG_EXTERN int DecodeTime(char *str, int fmask, int *tmask,
		   struct tm * tm, double *fsec);
PG_EXTERN int	DecodeTimezone(char *str, int *tzp);
PG_EXTERN int	DecodeUnits(int field, char *lowtoken, int *val);
PG_EXTERN datetkn *datebsearch(char *key, datetkn *base, unsigned int nel);

PG_EXTERN int	j2day(int jd);

#endif	 /* DATETIME_H */
