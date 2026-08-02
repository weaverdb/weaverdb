/*-------------------------------------------------------------------------
 *
 * pg_opclass.h
 *	  definition of the system "opclass" relation (pg_opclass)
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
#ifndef PG_OPCLASS_H
#define PG_OPCLASS_H

/* ----------------
 *		postgres.h contains the system type definintions and the
 *		CATALOG(), BOOTSTRAP and DATA() sugar words so this file
 *		can be read by both genbki.sh and the C compiler.
 * ----------------
 */

/* ----------------
 *		pg_opclass definition.	cpp turns this into
 *		typedef struct FormData_pg_opclass
 * ----------------
 */

CATALOG(pg_opclass)
{
	NameData	opcname;
	Oid			opcdeftype;
} FormData_pg_opclass;

/* ----------------
 *		Form_pg_opclass corresponds to a pointer to a tuple with
 *		the format of pg_opclass relation.
 * ----------------
 */
typedef FormData_pg_opclass *Form_pg_opclass;

/* ----------------
 *		compiler constants for pg_opclass
 * ----------------
 */
#define Natts_pg_opclass				2
#define Anum_pg_opclass_opcname			1
#define Anum_pg_opclass_opcdeftype		2

/* ----------------
 *		initial contents of pg_opclass
 * ----------------
 */

/*
 * putting _null_'s in the (fixed-length) type field is bad
 * (see the README in this directory), so just put zeros
 * in, which are invalid OID's anyway.  --djm
 */
DATA(insert OID =  421 (	int2_ops		 21   ));
DESCR("");
DATA(insert OID =  422 (	rect_ops			603   ));
DESCR("");
DATA(insert OID =  423 (	float8_ops		701   ));
DESCR("");
DATA(insert OID =  424 (	int24_ops		  0   ));
DESCR("");
DATA(insert OID =  425 (	int42_ops		  0   ));
DESCR("");
DATA(insert OID =  426 (	int4_ops		 23   ));
DESCR("");
#define INT4_OPS_OID 426
DATA(insert OID =  1419 (	connector_ops		 1136   ));
DESCR("");
#define CONNECTOR_OPS_OID 1419
DATA(insert OID =  427 (	oid_ops			 26   ));
DESCR("");
DATA(insert OID =  428 (	float4_ops		700   ));
DESCR("");
DATA(insert OID =  429 (	char_ops		 18   ));
DESCR("");
DATA(insert OID =  431 (	text_ops		 25   ));
DESCR("");
DATA(insert OID =  432 (	abstime_ops		702   ));
DESCR("");
DATA(insert OID =  433 (	bigbox_ops		603   ));
DESCR("");
DATA(insert OID =  434 (	poly_ops		604   ));
DESCR("");
DATA(insert OID =  435 (	oidvector_ops	 30   ));
DESCR("");
DATA(insert OID =  714 (	circle_ops		718   ));
DESCR("");
DATA(insert OID =  754 (	int8_ops		 20   ));
DESCR("");
DATA(insert OID = 957 (         bytea_ops               17   ));
DESCR("");
DATA(insert OID = 1076 (	bpchar_ops	   1042   ));
DESCR("");
DATA(insert OID = 1077 (	varchar_ops    1043   ));
DESCR("");
DATA(insert OID = 1114 (	date_ops	   1082   ));
DESCR("");
DATA(insert OID = 1115 (	time_ops	   1083   ));
DESCR("");
DATA(insert OID = 1181 (	name_ops		 19   ));
DESCR("");
DATA(insert OID = 1312 (	timestamp_ops  1184   ));
DESCR("");
DATA(insert OID = 1313 (	interval_ops   1186   ));
DESCR("");
DATA(insert OID = 810  (	macaddr_ops		829   ));
DESCR("");
DATA(insert OID = 935  (	inet_ops		869   ));
DESCR("");
DATA(insert OID = 652  (	cidr_ops		650   ));
DESCR("");
DATA(insert OID = 1768 (	numeric_ops    1700   ));
DESCR("");
DATA(insert OID = 1663 (	lztext_ops	   1625   ));
DESCR("");
DATA(insert OID = 1690 (	bool_ops		 16   ));
DESCR("");
DATA(insert OID = 1399 (	timetz_ops	   1266   ));
DESCR("");
DATA(insert OID = 1989 ( java_ops	   1830   ));
DESCR("");
DATA(insert OID = 1990 (	vector_ops	   1842   ));
DESCR("vector btree operators");
DATA(insert OID = 1991 (	vector_l2_ops    1842   ));
DESCR("vector L2 distance for ivfflat/hnsw");
#define VECTOR_L2_OPS_OID 1991
DATA(insert OID = 1992 (	vector_ip_ops    1842   ));
DESCR("vector inner product for ivfflat/hnsw");
#define VECTOR_IP_OPS_OID 1992
DATA(insert OID = 1993 (	vector_cosine_ops 1842   ));
DESCR("vector cosine distance for ivfflat/hnsw");
#define VECTOR_COSINE_OPS_OID 1993
DATA(insert OID = 1994 (	halfvec_ops	   1844   ));
DESCR("halfvec btree operators");
DATA(insert OID = 1995 (	halfvec_l2_ops    1844   ));
DESCR("halfvec L2 distance for ivfflat/hnsw");
#define HALFVEC_L2_OPS_OID 1995
DATA(insert OID = 1996 (	halfvec_ip_ops    1844   ));
DESCR("halfvec inner product for ivfflat/hnsw");
#define HALFVEC_IP_OPS_OID 1996
DATA(insert OID = 1997 (	halfvec_cosine_ops 1844   ));
DESCR("halfvec cosine distance for ivfflat/hnsw");
#define HALFVEC_COSINE_OPS_OID 1997
DATA(insert OID = 1998 (	sparsevec_l2_ops  1846   ));
DESCR("sparsevec L2 distance for hnsw");
#define SPARSEVEC_L2_OPS_OID 1998
DATA(insert OID = 1999 (	sparsevec_ip_ops  1846   ));
DESCR("sparsevec inner product for hnsw");
#define SPARSEVEC_IP_OPS_OID 1999
DATA(insert OID = 2020 (	sparsevec_cosine_ops 1846   ));
DESCR("sparsevec cosine distance for hnsw");
#define SPARSEVEC_COSINE_OPS_OID 2020
DATA(insert OID = 2021 (	bit_hamming_ops   1562   ));
DESCR("bit hamming distance for ivfflat/hnsw");
#define BIT_HAMMING_OPS_OID 2021
DATA(insert OID = 2022 (	bit_jaccard_ops   1562   ));
DESCR("bit jaccard distance for hnsw");
#define BIT_JACCARD_OPS_OID 2022
DATA(insert OID = 2023 (	sparsevec_ops	   1846   ));
DESCR("sparsevec btree operators");
DATA(insert OID = 2024 (	vector_l1_ops	   1842   ));
DESCR("vector L1 distance for hnsw");
#define VECTOR_L1_OPS_OID 2024
DATA(insert OID = 2025 (	halfvec_l1_ops	   1844   ));
DESCR("halfvec L1 distance for hnsw");
#define HALFVEC_L1_OPS_OID 2025
DATA(insert OID = 2026 (	sparsevec_l1_ops  1846   ));
DESCR("sparsevec L1 distance for hnsw");
#define SPARSEVEC_L1_OPS_OID 2026

#endif	 /* PG_OPCLASS_H */
