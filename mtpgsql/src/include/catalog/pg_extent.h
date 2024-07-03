/*
 * Copyright (c) 2024 Myron Scott <myron@weaverdb.org> All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 */


/*-------------------------------------------------------------------------
 *
 * pg_extent.h
 *
 *
 * NOTES
 *	  the genbki.sh script reads this file and generates .bki
 *	  information from the DATA() statements.
 *
 *-------------------------------------------------------------------------
 */
#ifndef PG_EXTENT_H
#define PG_EXTENT_H

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
CATALOG(pg_extent) BOOTSTRAP
{
	Oid         relid;
	int2        allocation;
        bool        percentage;
} FormData_pg_extent;

/* ----------------
 *		Form_pg_database corresponds to a pointer to a tuple with
 *		the format of pg_database relation.
 * ----------------
 */
typedef FormData_pg_extent *Form_pg_extent;

/* ----------------
 *		compiler constants for pg_schema
 * ----------------
 */
#define Natts_pg_extent				3
#define Anum_pg_extent_relid		1
#define Anum_pg_extent_allocation		2
#define Anum_pg_extent_percentage		3
#endif	 /* PG_EXTENT_H */


