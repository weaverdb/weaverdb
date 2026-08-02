/*-------------------------------------------------------------------------
 *
 * pg_amproc.h
 *	  definition of the system "amproc" relation (pg_amproce)
 *	  along with the relation's initial contents.  The amproc
 *	  catalog is used to store procedures used by indexed access
 *	  methods that aren't associated with operators.
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
#ifndef PG_AMPROC_H
#define PG_AMPROC_H

/* ----------------
 *		postgres.h contains the system type definitions and the
 *		CATALOG(), BOOTSTRAP and DATA() sugar words so this file
 *		can be read by both genbki.sh and the C compiler.
 * ----------------
 */

/* ----------------
 *		pg_amproc definition.  cpp turns this into
 *		typedef struct FormData_pg_amproc
 * ----------------
 */
CATALOG(pg_amproc)
{
	Oid			amid;
	Oid			amopclaid;
	Oid			amproc;
	int2		amprocnum;
} FormData_pg_amproc;

/* ----------------
 *		Form_pg_amproc corresponds to a pointer to a tuple with
 *		the format of pg_amproc relation.
 * ----------------
 */
typedef FormData_pg_amproc *Form_pg_amproc;

/* ----------------
 *		compiler constants for pg_amproc
 * ----------------
 */
#define Natts_pg_amproc					4
#define Anum_pg_amproc_amid				1
#define Anum_pg_amproc_amopclaid		2
#define Anum_pg_amproc_amproc			3
#define Anum_pg_amproc_amprocnum		4

/* ----------------
 *		initial contents of pg_amproc
 * ----------------
 */

/* rtree */
#ifdef NOTUSED
DATA(insert OID = 0 (402  422  193 1));
DATA(insert OID = 0 (402  422  194 2));
DATA(insert OID = 0 (402  422  195 3));
DATA(insert OID = 0 (402  433  193 1));
DATA(insert OID = 0 (402  433  194 2));
DATA(insert OID = 0 (402  433  196 3));
DATA(insert OID = 0 (402  434  197 1));
DATA(insert OID = 0 (402  434  198 2));
DATA(insert OID = 0 (402  434  199 3));
#endif

/* btree */
DATA(insert OID = 0 (403  421  350 1));
DATA(insert OID = 0 (403  423  355 1));
DATA(insert OID = 0 (403  424  353 1));
DATA(insert OID = 0 (403  425  352 1));
DATA(insert OID = 0 (403  426  351 1));
DATA(insert OID = 0 (403  427  356 1));
DATA(insert OID = 0 (403  428  354 1));
DATA(insert OID = 0 (403  429  358 1));
DATA(insert OID = 0 (403  431  360 1));
DATA(insert OID = 0 (403  432  357 1));
DATA(insert OID = 0 (403  435  404 1));
DATA(insert OID = 0 (403  754  842 1));
DATA(insert OID = 0 (403 1076 1078 1));
DATA(insert OID = 0 (403 957 1512 1));
DATA(insert OID = 0 (403 1077 1079 1));
DATA(insert OID = 0 (403 1114 1092 1));
DATA(insert OID = 0 (403 1115 1107 1));
DATA(insert OID = 0 (403 1181  359 1));
DATA(insert OID = 0 (403 1312 1314 1));
DATA(insert OID = 0 (403 1313 1315 1));
DATA(insert OID = 0 (403  810  836 1));
DATA(insert OID = 0 (403  935  926 1));
DATA(insert OID = 0 (403  652  926 1));
DATA(insert OID = 0 (403 1768 1769 1));
DATA(insert OID = 0 (403 1690 1693 1));
DATA(insert OID = 0 (403 1663 1636 1));
DATA(insert OID = 0 (403 1399 1358 1));
DATA(insert OID = 0 (403 1419 1137 1));
DATA(insert OID = 0 (403 1989 2010 1));
DATA(insert OID = 0 (403 1990 2242 1));
DATA(insert OID = 0 (403 1994 2258 1));
DATA(insert OID = 0 (403 2023 2336 1));

/* ivfflat vector_l2_ops */
DATA(insert OID = 0 (406 1991 2241 1));
DATA(insert OID = 0 (406 1991 2240 3));

/* ivfflat vector_ip_ops */
DATA(insert OID = 0 (406 1992 2243 1));
DATA(insert OID = 0 (406 1992 2245 3));
DATA(insert OID = 0 (406 1992 2244 4));

/* ivfflat vector_cosine_ops */
DATA(insert OID = 0 (406 1993 2243 1));
DATA(insert OID = 0 (406 1993 2244 2));
DATA(insert OID = 0 (406 1993 2245 3));
DATA(insert OID = 0 (406 1993 2244 4));

/* hnsw vector_l2_ops */
DATA(insert OID = 0 (407 1991 2241 1));

/* hnsw vector_ip_ops */
DATA(insert OID = 0 (407 1992 2243 1));

/* hnsw vector_cosine_ops */
DATA(insert OID = 0 (407 1993 2243 1));
DATA(insert OID = 0 (407 1993 2244 2));

/* ivfflat halfvec_l2_ops */
DATA(insert OID = 0 (406 1995 2252 1));
DATA(insert OID = 0 (406 1995 2251 3));
DATA(insert OID = 0 (406 1995 2265 5));

/* ivfflat halfvec_ip_ops */
DATA(insert OID = 0 (406 1996 2253 1));
DATA(insert OID = 0 (406 1996 2254 3));
DATA(insert OID = 0 (406 1996 2255 4));
DATA(insert OID = 0 (406 1996 2265 5));

/* ivfflat halfvec_cosine_ops */
DATA(insert OID = 0 (406 1997 2253 1));
DATA(insert OID = 0 (406 1997 2255 2));
DATA(insert OID = 0 (406 1997 2254 3));
DATA(insert OID = 0 (406 1997 2255 4));
DATA(insert OID = 0 (406 1997 2265 5));

/* hnsw halfvec_l2_ops */
DATA(insert OID = 0 (407 1995 2252 1));
DATA(insert OID = 0 (407 1995 2266 3));

/* hnsw halfvec_ip_ops */
DATA(insert OID = 0 (407 1996 2253 1));
DATA(insert OID = 0 (407 1996 2266 3));

/* hnsw halfvec_cosine_ops */
DATA(insert OID = 0 (407 1997 2253 1));
DATA(insert OID = 0 (407 1997 2255 2));
DATA(insert OID = 0 (407 1997 2266 3));

/* hnsw sparsevec_l2_ops */
DATA(insert OID = 0 (407 1998 2274 1));
DATA(insert OID = 0 (407 1998 2280 3));

/* hnsw sparsevec_ip_ops */
DATA(insert OID = 0 (407 1999 2275 1));
DATA(insert OID = 0 (407 1999 2280 3));

/* hnsw sparsevec_cosine_ops */
DATA(insert OID = 0 (407 2020 2275 1));
DATA(insert OID = 0 (407 2020 2277 2));
DATA(insert OID = 0 (407 2020 2280 3));

/* ivfflat bit_hamming_ops */
DATA(insert OID = 0 (406 2021 2281 1));
DATA(insert OID = 0 (406 2021 2281 3));
DATA(insert OID = 0 (406 2021 2283 5));

/* hnsw bit_hamming_ops */
DATA(insert OID = 0 (407 2021 2281 1));
DATA(insert OID = 0 (407 2021 2284 3));

/* hnsw bit_jaccard_ops */
DATA(insert OID = 0 (407 2022 2282 1));
DATA(insert OID = 0 (407 2022 2284 3));

/* hnsw vector_l1_ops */
DATA(insert OID = 0 (407 2024 2361 1));

/* hnsw halfvec_l1_ops */
DATA(insert OID = 0 (407 2025 2362 1));
DATA(insert OID = 0 (407 2025 2266 3));

/* hnsw sparsevec_l1_ops */
DATA(insert OID = 0 (407 2026 2363 1));
DATA(insert OID = 0 (407 2026 2280 3));


/* hash */
DATA(insert OID = 0 (405  421  449 1));
DATA(insert OID = 0 (405  423  452 1));
DATA(insert OID = 0 (405  426  450 1));
DATA(insert OID = 0 (405  427  453 1));
DATA(insert OID = 0 (405  428  451 1));
DATA(insert OID = 0 (405  429  454 1));
DATA(insert OID = 0 (405  431  456 1));
DATA(insert OID = 0 (405  435  457 1));
DATA(insert OID = 0 (405 1076 1080 1));
DATA(insert OID = 0 (405 1077 1081 1));
DATA(insert OID = 0 (405 1114  450 1));
DATA(insert OID = 0 (405 1115  452 1));
DATA(insert OID = 0 (405 1181  455 1));
DATA(insert OID = 0 (405 1312  452 1));
DATA(insert OID = 0 (405 1313  452 1));
DATA(insert OID = 0 (405 1399  452 1));

#endif	 /* PG_AMPROC_H */
