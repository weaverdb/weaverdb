/*-------------------------------------------------------------------------
 *
 * pg_inherits.h
 *	  definition of the system "inherits" relation (pg_inherits)
 *	  along with the relation's initial contents.
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
#ifndef PG_INHERITS_H
#define PG_INHERITS_H

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
CATALOG(pg_inherits)
{
	Oid			inhrelid;
	Oid			inhparent;
	int4		inhseqno;
} FormData_pg_inherits;

/* ----------------
 *		Form_pg_inherits corresponds to a pointer to a tuple with
 *		the format of pg_inherits relation.
 * ----------------
 */
typedef FormData_pg_inherits *Form_pg_inherits;

/* ----------------
 *		compiler constants for pg_inherits
 * ----------------
 */
#define Natts_pg_inherits				3
#define Anum_pg_inherits_inhrelid		1
#define Anum_pg_inherits_inhparent		2
#define Anum_pg_inherits_inhseqno		3


#endif	 /* PG_INHERITS_H */
