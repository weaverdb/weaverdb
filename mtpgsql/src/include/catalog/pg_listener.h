/*-------------------------------------------------------------------------
 *
 * pg_listener.h
 *	  Asynchronous notification
 *
 *
 * Portions Copyright (c) 2000-2024, Myron Scott  <myron@weaverdb.org>
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 *
 * NOTES
 *	  the genbki.sh script reads this file and generates .bki
 *	  information from the DATA() statements.
 *
 *-------------------------------------------------------------------------
 */
#ifndef PG_LISTENER_H
#define PG_LISTENER_H

/* ----------------
 *		postgres.h contains the system type definintions and the
 *		CATALOG(), BOOTSTRAP and DATA() sugar words so this file
 *		can be read by both genbki.sh and the C compiler.
 * ----------------
 */

/* ----------------------------------------------------------------
 *		pg_listener definition.
 *
 *		cpp turns this into typedef struct FormData_pg_listener
 * ----------------------------------------------------------------
 */

CATALOG(pg_listener)
{
	NameData	relname;
	int4		listenerpid;
	int4		notification;
} FormData_pg_listener;

/* ----------------
 *		compiler constants for pg_listener
 * ----------------
 */
#define Natts_pg_listener						3
#define Anum_pg_listener_relname				1
#define Anum_pg_listener_pid					2
#define Anum_pg_listener_notify					3

/* ----------------
 *		initial contents of pg_listener are NOTHING.
 * ----------------
 */


#endif	 /* PG_LISTENER_H */
