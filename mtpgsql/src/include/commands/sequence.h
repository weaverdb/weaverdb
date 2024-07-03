/*
 * Copyright (c) 2024 Myron Scott <myron@weaverdb.org> All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 */

/*-------------------------------------------------------------------------
 *
 * sequence.h
 *	  prototypes for sequence.c.
 *
 *
 *-------------------------------------------------------------------------
 */
#ifndef SEQUENCE_H
#define SEQUENCE_H

#include "nodes/parsenodes.h"
#include "utils/rel.h"

/*
 * Columns of a sequnece relation
 */

#define SEQ_COL_NAME			1
#define SEQ_COL_LASTVAL			2
#define SEQ_COL_INCBY			3
#define SEQ_COL_MAXVALUE		4
#define SEQ_COL_MINVALUE		5
#define SEQ_COL_CACHE			6
#define SEQ_COL_CYCLE			7
#define SEQ_COL_CALLED			8

#define SEQ_COL_FIRSTCOL		SEQ_COL_NAME
#define SEQ_COL_LASTCOL			SEQ_COL_CALLED

typedef struct FormData_pg_sequence
{
	NameData	sequence_name;
	int4		last_value;
	int4		increment_by;
	int4		max_value;
	int4		min_value;
	int4		cache_value;
	char		is_cycled;
	char		is_called;
} FormData_pg_sequence;

typedef FormData_pg_sequence *Form_pg_sequence;

typedef struct sequence_magic
{
	uint32		magic;
} sequence_magic;

typedef struct SeqTableData
{
	char	   *name;
	Oid			relid;
	Relation	rel;
	int4		cached;
	int4		last;
	int4		increment;
	struct SeqTableData *next;
} SeqTableData;

typedef SeqTableData *SeqTable;




PG_EXTERN void DefineSequence(CreateSeqStmt *stmt);
PG_EXTERN int4 nextval(struct varlena * seqname);
PG_EXTERN int4 currval(struct varlena * seqname);
PG_EXTERN int4 setval(struct varlena * seqname, int4 next);
PG_EXTERN void CloseSequences(void);

#endif	 /* SEQUENCE_H */
