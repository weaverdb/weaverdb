/*-------------------------------------------------------------------------
 *
 * pg_index.h
 *	  definition of the system "index" relation (pg_index)
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
#ifndef PG_INDEX_H
#define PG_INDEX_H

/* ----------------
 *		postgres.h contains the system type definintions and the
 *		CATALOG(), BOOTSTRAP and DATA() sugar words so this file
 *		can be read by both genbki.sh and the C compiler.
 * ----------------
 */

/* ----------------
 *		pg_index definition.  cpp turns this into
 *		typedef struct FormData_pg_index.  The oid of the index relation
 *		is stored in indexrelid; the oid of the indexed relation is stored
 *		in indrelid.
 * ----------------
 */

/*
 * it seems that all variable length fields should go at the _end_,
 * because the system cache routines only copy the fields up to the
 * first variable length field.  so I moved indislossy, indhaskeytype,
 * and indisunique before indpred.	--djm 8/20/96
 */
CATALOG(pg_index)
{
	Oid			indexrelid;
	Oid			indrelid;
	Oid			indproc;		/* registered procedure for functional
								 * index */
	int2vector              indkey;
	oidvector               indclass;
	bool                    indisclustered;
	char                    indattributes;		/* do we fetch false tuples (lossy
								 * compression)? */
	bool                    indhaskeytype;	/* does key type != attribute type? */
	bool                    indisunique;	/* is this a unique index? */
	bool                    indisprimary;	/* is this index for primary key */
	Oid			indreference;	/* oid of index of referenced relation (ie
								 * - this index for foreign key */
	text		indpred;		/* query plan for partial index predicate */
} FormData_pg_index;

/* ----------------
 *		Form_pg_index corresponds to a pointer to a tuple with
 *		the format of pg_index relation.
 * ----------------
 */
typedef FormData_pg_index *Form_pg_index;

typedef char              IndexProp;

#define INDEX_LOSSY 1
#define INDEX_DEFERRED 2
#define INDEX_UNIQUE   4
#define INDEX_PRIMARY   8
#define IndexIsLossy(value) (value->indattributes & 1)
#define IndexIsDeferred(value) (value->indattributes & 2)
#define IndexPropIsUnique(value) (value & 4)
#define IndexPropIsPrimary(value) (value & 8)
#define IndexPropIsDeferred(value) (value & 2)

/* ----------------
 *		compiler constants for pg_index
 * ----------------
 */
#define Natts_pg_index					12
#define Anum_pg_index_indexrelid		1
#define Anum_pg_index_indrelid			2
#define Anum_pg_index_indproc			3
#define Anum_pg_index_indkey			4
#define Anum_pg_index_indclass			5
#define Anum_pg_index_indisclustered	6
#define Anum_pg_index_indislossy		7
#define Anum_pg_index_indhaskeytype		8
#define Anum_pg_index_indisunique		9
#define Anum_pg_index_indisprimary		10
#define Anum_pg_index_indreference		11
#define Anum_pg_index_indpred			12

#endif	 /* PG_INDEX_H */
