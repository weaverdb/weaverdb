/*-------------------------------------------------------------------------
 *
 * trace.h
 *
 *	  Conditional trace definitions.
 *
 * Portions Copyright (c) 2000-2024, Myron Scott  <myron@weaverdb.org>
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 *
 *-------------------------------------------------------------------------
 */

#ifndef TRACE_H
#define TRACE_H

#ifdef ELOG_TIMESTAMPS
char	   *tprintf_timestamp(void);

#define TIMESTAMP_SIZE 32
#else
#define TIMESTAMP_SIZE 0
#endif

PG_EXTERN int	tprintf(int flag, const char *fmt,...);
PG_EXTERN int	eprintf(const char *fmt,...);
PG_EXTERN void write_syslog(int level, char *line);
PG_EXTERN void show_options(void);
PG_EXTERN void parse_options(char *str, bool secure);
PG_EXTERN void read_pg_options(SIGNAL_ARGS);

/*
 * Trace options, used as index into pg_options.
 * Must match the constants in pg_options[].
 */
enum pg_option_enum
{
	TRACE_ALL,					/* 0=trace some, 1=trace all, -1=trace
								 * none */
	TRACE_VERBOSE,
	TRACE_QUERY,
	TRACE_PLAN,
	TRACE_PARSE,
	TRACE_REWRITTEN,
	TRACE_PRETTY_PLAN,			/* indented multiline versions of trees */
	TRACE_PRETTY_PARSE,
	TRACE_PRETTY_REWRITTEN,
	TRACE_PARSERSTATS,
	TRACE_PLANNERSTATS,
	TRACE_EXECUTORSTATS,
	TRACE_SHORTLOCKS,			/* currently unused but needed, see lock.c */
	TRACE_LOCKS,
	TRACE_USERLOCKS,
	TRACE_SPINLOCKS,
	TRACE_NOTIFY,
	TRACE_MALLOC,
	TRACE_PALLOC,
	TRACE_LOCKOIDMIN,
	TRACE_LOCKRELATION,
	OPT_LOCKREADPRIORITY,		/* lock priority, see lock.c */
	OPT_DEADLOCKTIMEOUT,		/* deadlock timeout, see proc.c */
	OPT_NOFSYNC,				/* turn fsync off */
	OPT_SYSLOG,					/* use syslog for error messages */
	OPT_HOSTLOOKUP,				/* enable hostname lookup in ps_status */
	OPT_SHOWPORTNUMBER,			/* show port number in ps_status */

	NUM_PG_OPTIONS				/* must be the last item of enum */
};

extern int	pg_options[NUM_PG_OPTIONS];

#ifdef __GNUC__
#define PRINTF(args...)			tprintf1(args)
#define EPRINTF(args...)		eprintf(args)
#define TPRINTF(flag, args...)	tprintf(flag, args)
#else
#define PRINTF	tprintf1
#define EPRINTF eprintf
#define TPRINTF tprintf
#endif

#endif	 /* TRACE_H */

/*
 * Local variables:
 *	tab-width: 4
 *	c-indent-level: 4
 *	c-basic-offset: 4
 * End:
 */
