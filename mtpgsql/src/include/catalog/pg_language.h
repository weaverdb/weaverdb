/*-------------------------------------------------------------------------
 *
 * pg_language.h
 *	  definition of the system "language" relation (pg_language)
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
#ifndef PG_LANGUAGE_H
#define PG_LANGUAGE_H

/* ----------------
 *		postgres.h contains the system type definintions and the
 *		CATALOG(), BOOTSTRAP and DATA() sugar words so this file
 *		can be read by both genbki.sh and the C compiler.
 * ----------------
 */

/* ----------------
 *		pg_language definition.  cpp turns this into
 *		typedef struct FormData_pg_language
 * ----------------
 */
CATALOG(pg_language)
{
	NameData	lanname;
	bool		lanispl;		/* Is a procedural language */
	bool		lanpltrusted;	/* PL is trusted */
	Oid			lanplcallfoid;	/* Call handler for PL */
	text		lancompiler;	/* VARIABLE LENGTH FIELD */
} FormData_pg_language;

/* ----------------
 *		Form_pg_language corresponds to a pointer to a tuple with
 *		the format of pg_language relation.
 * ----------------
 */
typedef FormData_pg_language *Form_pg_language;

/* ----------------
 *		compiler constants for pg_language
 * ----------------
 */
#define Natts_pg_language				5
#define Anum_pg_language_lanname		1
#define Anum_pg_language_lanispl		2
#define Anum_pg_language_lanpltrusted		3
#define Anum_pg_language_lanplcallfoid		4
#define Anum_pg_language_lancompiler		5

/* ----------------
 *		initial contents of pg_language
 * ----------------
 */

DATA(insert OID = 11 ( internal f 0 0 "n/a" ));
DESCR("");
#define INTERNALlanguageId 11
DATA(insert OID = 13 ( "C" f 0 0 "/bin/cc" ));
DESCR("");
#define ClanguageId 13
DATA(insert OID = 14 ( "sql" f 0 0 "postgres"));
DESCR("");
#define SQLlanguageId 14
DATA(insert OID = 10 ( "java" f 0 0 "java"));
DESCR("");
#define JAVAlanguageId 10

#endif	 /* PG_LANGUAGE_H */
