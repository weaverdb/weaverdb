/*-------------------------------------------------------------------------
 *
 * async.h
 *	  Asynchronous notification: NOTIFY, LISTEN, UNLISTEN
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * $Id: async.h,v 1.1.1.1 2006/08/12 00:22:17 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */
#ifndef ASYNC_H
#define ASYNC_H

/* notify-related SQL statements */
PG_EXTERN void Async_Notify(char *relname);
PG_EXTERN void Async_Listen(char *relname, int pid);
PG_EXTERN void Async_Unlisten(char *relname, int pid);

/* perform (or cancel) outbound notify processing at transaction commit */
PG_EXTERN void AtCommit_Notify(void);
PG_EXTERN void AtAbort_Notify(void);

/* signal handler for inbound notifies (SIGUSR2) */
PG_EXTERN void Async_NotifyHandler(SIGNAL_ARGS);

/*
 * enable/disable processing of inbound notifies directly from signal handler.
 * The enable routine first performs processing of any inbound notifies that
 * have occurred since the last disable.  These are meant to be called ONLY
 * from the appropriate places in PostgresMain().
 */
PG_EXTERN void EnableNotifyInterrupt(void);
PG_EXTERN void DisableNotifyInterrupt(void);

#endif	 /* ASYNC_H */
