
/*-------------------------------------------------------------------------
 *
 * pg_schema.h
 *	  definition of the system "schema" relation (pg_schema)
 *	  along with the relation's initial contents.
 *
 *
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
#ifndef PG_SCHEMA_H
#define PG_SCHEMA_H

/* ----------------
 *		postgres.h contains the system type definintions and the
 *		CATALOG(), BOOTSTRAP and DATA() sugar words so this file
 *		can be read by both genbki.sh and the C compiler.
 * ----------------
 */

/* ----------------
 *		pg_schema definition.  cpp turns this into
 *		typedef struct FormData_pg_schema
 * ----------------
 */
CATALOG(pg_schema) BOOTSTRAP
{
	NameData	schemaname;
	int4		owner;
	int4		encoding;
    Oid			database;
	text		schemapath;		/* VARIABLE LENGTH FIELD */
} FormData_pg_schema;

/* ----------------
 *		Form_pg_database corresponds to a pointer to a tuple with
 *		the format of pg_database relation.
 * ----------------
 */
typedef FormData_pg_schema *Form_pg_schema;

/* ----------------
 *		compiler constants for pg_schema
 * ----------------
 */
#define Natts_pg_schema				5
#define Anum_pg_schema_schemaname		1
#define Anum_pg_schema_owner		2
#define Anum_pg_schema_encoding		3
#define Anum_pg_schema_database		4
#define Anum_pg_schema_datpath		5
#endif	 /* PG_SCHEMA_H */


