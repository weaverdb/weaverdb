/*-------------------------------------------------------------------------
 *
 * pg_extstore.h
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * $Id: pg_extstore.h,v 1.1.1.1 2006/08/12 00:22:12 synmscott Exp $
 *
 *
 *-------------------------------------------------------------------------
 */
#ifndef PG_EXTSTORE_H
#define PG_EXTSTORE_H

/* ----------------
 *		postgres.h contains the system type definintions and the
 *		CATALOG(), BOOTSTRAP and DATA() sugar words so this file
 *		can be read by both genbki.sh and the C compiler.
 * ----------------
 */

/* ----------------
 *		pg_inherits definition.  cpp turns this into
 *		typedef struct FormData_pg_inherits
 * ----------------
 */
CATALOG(pg_extstore)
{
	Oid			extrelid;
	Oid			extstore;
	int2                    extattnum;
} FormData_pg_extstore;

/* ----------------
 *		Form_pg_extstore corresponds to a pointer to a tuple with
 *		the format of pg_extstore relation.
 * ----------------
 */
typedef FormData_pg_extstore *Form_pg_extstore;

/* ----------------
 *		compiler constants for pg_extstore
 * ----------------
 */
#define Natts_pg_extstore				3
#define Anum_pg_extstore_extrelid		1
#define Anum_pg_extstore_extstore		2
#define Anum_pg_extstore_extattnum		3


#endif	 /* PG_EXTSTORE_H */
