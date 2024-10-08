/*-------------------------------------------------------------------------
 *
 * pg_am.h
 *	  definition of the system "am" relation (pg_am)
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
 *		the genbki.sh script reads this file and generates .bki
 *		information from the DATA() statements.
 *
 *		XXX do NOT break up DATA() statements into multiple lines!
 *			the scripts are not as smart as you might think...
 *
 *-------------------------------------------------------------------------
 */
#ifndef PG_AM_H
#define PG_AM_H

/* ----------------
 *		postgres.h contains the system type definintions and the
 *		CATALOG(), BOOTSTRAP and DATA() sugar words so this file
 *		can be read by both genbki.sh and the C compiler.
 * ----------------
 */

/* ----------------
 *		pg_am definition.  cpp turns this into
 *		typedef struct FormData_pg_am
 * ----------------
 */
CATALOG(pg_am)
{
	NameData	amname;			/* access method name */
	int4		amowner;		/* usesysid of creator */
	int2		amstrategies;	/* total NUMBER of strategies by which we
								 * can traverse/search this AM */
	int2		amsupport;		/* total NUMBER of support functions that
								 * this AM uses */
	int2		amorderstrategy;/* if this AM has a sort order, the
								 * strategy number of the sort operator.
								 * Zero if AM is not ordered. */
	regproc		amgettuple;		/* "next valid tuple" function */
	regproc		aminsert;		/* "insert this tuple" function */
	regproc		amdelete;		/* "delete this tuple" function */
	regproc		ambulkdelete;		
	regproc		amsetlock;		/* - deprecated */
	regproc		amsettid;		/* - deprecated */
	regproc		amfreetuple;	/* - deprecated...use for recoverpage of indexes */
	regproc		ambeginscan;	/* "start new scan" function */
	regproc		amrescan;		/* "restart this scan" function */
	regproc		amendscan;		/* "end this scan" function */
	regproc		ammarkpos;		/* "mark current scan position" function */
	regproc		amrestrpos;		/* "restore marked scan position" function */
	regproc		amopen;			/* - deprecated */
	regproc		amclose;		/* - deprecated */
	regproc		ambuild;		/* "build new index" function */
	regproc		amcreate;		/* - deprecated */
	regproc		amdestroy;		/* - deprecated */
	regproc		amcostestimate; /* estimate cost of an indexscan */
} FormData_pg_am;

/* ----------------
 *		Form_pg_am corresponds to a pointer to a tuple with
 *		the format of pg_am relation.
 * ----------------
 */
typedef FormData_pg_am *Form_pg_am;

/* ----------------
 *		compiler constants for pg_am
 * ----------------
 */
#define Natts_pg_am						23
#define Anum_pg_am_amname				1
#define Anum_pg_am_amowner				2
#define Anum_pg_am_amstrategies			3
#define Anum_pg_am_amsupport			4
#define Anum_pg_am_amorderstrategy		5
#define Anum_pg_am_amgettuple			6
#define Anum_pg_am_aminsert				7
#define Anum_pg_am_amdelete				8
#define Anum_pg_am_ambulkdelete			9
#define Anum_pg_am_amsetlock			10
#define Anum_pg_am_amsettid				11
#define Anum_pg_am_amfreetuple			12
#define Anum_pg_am_ambeginscan			13
#define Anum_pg_am_amrescan				14
#define Anum_pg_am_amendscan			15
#define Anum_pg_am_ammarkpos			16
#define Anum_pg_am_amrestrpos			17
#define Anum_pg_am_amopen				18
#define Anum_pg_am_amclose				19
#define Anum_pg_am_ambuild				20
#define Anum_pg_am_amcreate				21
#define Anum_pg_am_amdestroy			22
#define Anum_pg_am_amcostestimate		23

/* ----------------
 *		initial contents of pg_am
 * ----------------
 */

//DATA(insert OID = 402 (  rtree PGUID 8 3 0 rtgettuple rtinsert rtdelete - - - - rtbeginscan rtrescan rtendscan rtmarkpos rtrestrpos - - rtbuild - - rtcostestimate ));
//DESCR("");
DATA(insert OID = 403 (  btree PGUID 5 1 1 btgettuple btinsert btdelete btbulkdelete - - btrecoverpage btbeginscan btrescan btendscan btmarkpos btrestrpos - - btbuild - - btcostestimate ));
DESCR("");
#define BTREE_AM_OID 403
DATA(insert OID = 405 (  hash PGUID 1 1 0 hashgettuple hashinsert hashdelete - - - - hashbeginscan hashrescan hashendscan hashmarkpos hashrestrpos - - hashbuild - - hashcostestimate ));
DESCR("");
#define HASH_AM_OID 405
//DATA(insert OID = 783 (  gist PGUID 100 7 0 gistgettuple gistinsert gistdelete - - - - gistbeginscan gistrescan gistendscan gistmarkpos gistrestrpos - - gistbuild - - gistcostestimate ));
//DESCR("");

#endif	 /* PG_AM_H */
