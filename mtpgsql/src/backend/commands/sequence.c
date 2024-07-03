/*
 * Copyright (c) 2024 Myron Scott <myron@weaverdb.org> All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 */

/*-------------------------------------------------------------------------
 *
 * sequence.c
 *	  PostgreSQL sequences support code.
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"
#include "env/env.h"

#include "access/heapam.h"
#include "commands/creatinh.h"
#include "commands/sequence.h"
#include "miscadmin.h"
#ifdef USEACL
#include "utils/acl.h"
#endif
#include "utils/builtins.h"
#include "env/freespace.h"

#include "access/hio.h"

#define SEQ_MAGIC	  0x1717

#define SEQ_MAXVALUE	((int4)0x7FFFFFFF)
#define SEQ_MINVALUE	-(SEQ_MAXVALUE)

static SectionId sequence_id = SECTIONID("SEQT");

typedef struct SeqCache {
    SeqTable      head;
} SeqCache;


#ifdef TLS
TLS SeqCache* sequence_globals = NULL;
#else
#define sequence_globals GetEnv()->sequence_globals
#endif

/* static SeqTable seqtab = NULL; */
static SeqCache* get_seq_cache(void);
static SeqTable init_sequence(char *caller, char *name);
static Form_pg_sequence read_info(char *caller, SeqTable elm, Buffer *buf);
static void init_params(CreateSeqStmt *seq, Form_pg_sequence new);
static int	get_param(DefElem *def);

/*
 * DefineSequence
 *				Creates a new sequence relation
 */
void
DefineSequence(CreateSeqStmt *seq)
{
	FormData_pg_sequence new;
	CreateStmt *stmt = makeNode(CreateStmt);
	ColumnDef  *coldef;
	TypeName   *typnam;
	Relation	rel;
	Buffer		buf;
	PageHeader	page;
	sequence_magic *sm;
	HeapTuple	tuple;
	TupleDesc	tupDesc;
	Datum		value[SEQ_COL_LASTCOL];
	char		null[SEQ_COL_LASTCOL];
	int			i;
	NameData	name;

	/* Check and set values */
	init_params(seq, &new);

	/*
	 * Create relation (and fill *null & *value)
	 */
	stmt->tableElts = NIL;
	for (i = SEQ_COL_FIRSTCOL; i <= SEQ_COL_LASTCOL; i++)
	{
		typnam = makeNode(TypeName);
		typnam->setof = FALSE;
		typnam->arrayBounds = NULL;
		typnam->typmod = -1;
		coldef = makeNode(ColumnDef);
		coldef->typename = typnam;
		coldef->raw_default = NULL;
		coldef->cooked_default = NULL;
		coldef->is_not_null = false;
		null[i - 1] = ' ';

		switch (i)
		{
			case SEQ_COL_NAME:
				typnam->name = "name";
				coldef->colname = "sequence_name";
				namestrcpy(&name, seq->seqname);
				value[i - 1] = NameGetDatum(&name);
				break;
			case SEQ_COL_LASTVAL:
				typnam->name = "int4";
				coldef->colname = "last_value";
				value[i - 1] = Int32GetDatum(new.last_value);
				break;
			case SEQ_COL_INCBY:
				typnam->name = "int4";
				coldef->colname = "increment_by";
				value[i - 1] = Int32GetDatum(new.increment_by);
				break;
			case SEQ_COL_MAXVALUE:
				typnam->name = "int4";
				coldef->colname = "max_value";
				value[i - 1] = Int32GetDatum(new.max_value);
				break;
			case SEQ_COL_MINVALUE:
				typnam->name = "int4";
				coldef->colname = "min_value";
				value[i - 1] = Int32GetDatum(new.min_value);
				break;
			case SEQ_COL_CACHE:
				typnam->name = "int4";
				coldef->colname = "cache_value";
				value[i - 1] = Int32GetDatum(new.cache_value);
				break;
			case SEQ_COL_CYCLE:
				typnam->name = "char";
				coldef->colname = "is_cycled";
				value[i - 1] = CharGetDatum(new.is_cycled);
				break;
			case SEQ_COL_CALLED:
				typnam->name = "char";
				coldef->colname = "is_called";
				value[i - 1] = CharGetDatum('f');
				break;
		}
		stmt->tableElts = lappend(stmt->tableElts, coldef);
	}

	stmt->relname = seq->seqname;
	stmt->inhRelnames = NIL;
	stmt->constraints = NIL;

	DefineRelation(stmt, RELKIND_SEQUENCE);

	rel = heap_openr(seq->seqname, AccessExclusiveLock);

	tupDesc = RelationGetDescr(rel);

	Assert(RelationGetNumberOfBlocks(rel) == 0);
	buf = ReadBuffer(rel, P_NEW);

	if (!BufferIsValid(buf))
		elog(ERROR, "DefineSequence: ReadBuffer failed");

	page = (PageHeader) BufferGetPage(buf);

	PageInit((Page) page, BufferGetPageSize(buf), sizeof(sequence_magic));
	sm = (sequence_magic *) PageGetSpecialPointer(page);
	sm->magic = SEQ_MAGIC;

	/* Now - form & insert sequence tuple */
	tuple = heap_formtuple(tupDesc, value, null);
	/* don't do this 
        heap_insert(rel, tuple);
        do this b/c sequences only have one page */
        RelationPutHeapTuple(rel,buf,tuple);
        
	if (WriteBuffer(rel,buf) == STATUS_ERROR)
		elog(ERROR, "DefineSequence: WriteBuffer failed");

	heap_close(rel, AccessExclusiveLock);
}


int4
nextval(struct varlena * seqin)
{
	char	   *seqname = textout(seqin);
	SeqTable	elm;
	Buffer		buf;
	Form_pg_sequence seq;
	int4		incby,
				maxv,
				minv,
				cache;
	int4		result,
				next,
				rescnt = 0;

	/* open and AccessShareLock sequence */
	elm = init_sequence("nextval", seqname);
	pfree(seqname);

	if (elm->last != elm->cached)		/* some numbers were cached */
	{
		elm->last += elm->increment;
		return elm->last;
	}

	seq = read_info("nextval", elm, &buf);		/* lock page' buffer and
												 * read tuple */

	next = result = seq->last_value;
	incby = seq->increment_by;
	maxv = seq->max_value;
	minv = seq->min_value;
	cache = seq->cache_value;

	if (seq->is_called != 't')
		rescnt++;				/* last_value if not called */

	while (rescnt < cache)		/* try to fetch cache numbers */
	{

		/*
		 * Check MAXVALUE for ascending sequences and MINVALUE for
		 * descending sequences
		 */
		if (incby > 0)			/* ascending sequence */
		{
			if ((maxv >= 0 && next > maxv - incby) ||
				(maxv < 0 && next + incby > maxv))
			{
				if (rescnt > 0)
					break;		/* stop caching */
				if (seq->is_cycled != 't')
					elog(ERROR, "%s.nextval: got MAXVALUE (%d)",
						 elm->name, maxv);
				next = minv;
			}
			else
				next += incby;
		}
		else
/* descending sequence */
		{
			if ((minv < 0 && next < minv - incby) ||
				(minv >= 0 && next + incby < minv))
			{
				if (rescnt > 0)
					break;		/* stop caching */
				if (seq->is_cycled != 't')
					elog(ERROR, "%s.nextval: got MINVALUE (%d)",
						 elm->name, minv);
				next = maxv;
			}
			else
				next += incby;
		}
		rescnt++;				/* got result */
		if (rescnt == 1)		/* if it's first one - */
			result = next;		/* it's what to return */
	}

	/* save info in local cache */
	elm->last = result;			/* last returned number */
	elm->cached = next;			/* last cached number */

	/* save info in sequence relation */
	seq->last_value = next;		/* last fetched number */
	seq->is_called = 't';

	LockBuffer((elm->rel), buf, BUFFER_LOCK_UNLOCK);

	if (WriteBuffer(elm->rel, buf) == STATUS_ERROR)
		elog(ERROR, "%s.nextval: WriteBuffer failed", elm->name);

	return result;

}


int4
currval(struct varlena * seqin)
{
	char	   *seqname = textout(seqin);
	SeqTable	elm;
	int4		result;

	/* open and AccessShareLock sequence */
	elm = init_sequence("currval", seqname);
	pfree(seqname);

	if (elm->increment == 0)	/* nextval/read_info were not called */
		elog(ERROR, "%s.currval is not yet defined in this session", elm->name);

	result = elm->last;

	return result;

}

int4
setval(struct varlena * seqin, int4 next)
{
	char	   *seqname = textout(seqin);
	SeqTable	elm;
	Buffer		buf;
	Form_pg_sequence seq;

#ifdef USEACL
	if (pg_aclcheck(seqname, getpgusername(), ACL_WR) != ACLCHECK_OK)
		elog(ERROR, "%s.setval: you don't have permissions to set sequence %s",
			 seqname, seqname);
#endif

	/* open and AccessShareLock sequence */
	elm = init_sequence("setval", seqname);
	seq = read_info("setval", elm, &buf);		/* lock page' buffer and
												 * read tuple */

	if (seq->cache_value != 1)
	{
		elog(ERROR, "%s.setval: can't set value of sequence %s, cache != 1",
			 seqname, seqname);
	}

	if ((next < seq->min_value) || (next > seq->max_value))
	{
		elog(ERROR, "%s.setval: value %d is of of bounds (%d,%d)",
			 seqname, next, seq->min_value, seq->max_value);
	}

	/* save info in local cache */
	elm->last = next;			/* last returned number */
	elm->cached = next;			/* last cached number */

	/* save info in sequence relation */
	seq->last_value = next;		/* last fetched number */
	seq->is_called = 't';

	LockBuffer((elm->rel), buf, BUFFER_LOCK_UNLOCK);

	if (WriteBuffer(elm->rel, buf) == STATUS_ERROR)
		elog(ERROR, "%s.settval: WriteBuffer failed", seqname);

	return next;
}

static Form_pg_sequence
read_info(char *caller, SeqTable elm, Buffer *buf)
{
	PageHeader	page;
	ItemId		lp;
	HeapTupleData tuple;
	sequence_magic *sm;
	Form_pg_sequence seq;

	if (RelationGetNumberOfBlocks(elm->rel) != 1)
		elog(ERROR, "%s.%s: invalid number of blocks in sequence ",
			 elm->name, caller);

	*buf = ReadBuffer(elm->rel, 0);
	if (!BufferIsValid(*buf))
		elog(ERROR, "%s.%s: ReadBuffer failed", elm->name, caller);

	LockBuffer(elm->rel, *buf, BUFFER_LOCK_EXCLUSIVE);

	page = (PageHeader) BufferGetPage(*buf);
	sm = (sequence_magic *) PageGetSpecialPointer(page);

	if (sm->magic != SEQ_MAGIC)
		elog(ERROR, "%s.%s: bad magic (%08X)", elm->name, caller, sm->magic);

	lp = PageGetItemId(page, FirstOffsetNumber);
	Assert(ItemIdIsUsed(lp));
	tuple.t_data = (HeapTupleHeader) PageGetItem((Page) page, lp);

	seq = (Form_pg_sequence) GETSTRUCT(&tuple);

	elm->increment = seq->increment_by;

	return seq;

}


static SeqTable
init_sequence(char *caller, char *name)
{
	SeqTable	elm,
				prev = (SeqTable) NULL;
	Relation	seqrel;
	SeqCache* 	cache = (SeqCache*)get_seq_cache();

	/* Look to see if we already have a seqtable entry for name */
	for (elm = cache->head; elm != (SeqTable) NULL; elm = elm->next)
	{
		if (strcmp(elm->name, name) == 0)
			break;
		prev = elm;
	}

	/* If so, and if it's already been opened in this xact, just return it */
	if (elm != (SeqTable) NULL && elm->rel != (Relation) NULL)
		return elm;

	/* Else open and check it */
	seqrel = heap_openr(name, AccessShareLock);
	if (seqrel->rd_rel->relkind != RELKIND_SEQUENCE)
		elog(ERROR, "%s.%s: %s is not sequence !", name, caller, name);

	if (elm != (SeqTable) NULL)
	{

		/*
		 * We are using a seqtable entry left over from a previous xact;
		 * must check for relid change.
		 */
		elm->rel = seqrel;
		if (RelationGetRelid(seqrel) != elm->relid)
		{
			elog(NOTICE, "%s.%s: sequence was re-created",
				 name, caller);
			elm->relid = RelationGetRelid(seqrel);
			elm->cached = elm->last = elm->increment = 0;
		}
	}
	else
	{

		/*
		 * Time to make a new seqtable entry.  These entries live as long
		 * as the backend does, so we use plain malloc for them.
		 */
                MemoryContext oldcxt = MemoryContextSwitchTo(MemoryContextGetTopContext());

		elm = (SeqTable) palloc(sizeof(SeqTableData));
		elm->name = palloc(strlen(name) + 1);
	
                MemoryContextSwitchTo(oldcxt);

		strcpy(elm->name, name);
		elm->rel = seqrel;
		elm->relid = RelationGetRelid(seqrel);
		elm->cached = elm->last = elm->increment = 0;
		elm->next = (SeqTable) NULL;

		if (cache->head == (SeqTable) NULL)
			cache->head = elm;
		else
			prev->next = elm;
	}

	return elm;
}


/*
 * CloseSequences
 *				is calling by xact mgr at commit/abort.
 */
void
CloseSequences(void)
{
	SeqTable	elm;
	Relation	rel;
	SeqCache* cache = get_seq_cache();

	for (elm = cache->head; elm != (SeqTable) NULL; elm = elm->next)
	{
		if (elm->rel != (Relation) NULL)		/* opened in current xact */
		{
			rel = elm->rel;
			elm->rel = (Relation) NULL;
			heap_close(rel, NoLock);
/*  allow the transaction manager to release lock */
		}
	}
}


static void
init_params(CreateSeqStmt *seq, Form_pg_sequence new)
{
	DefElem    *last_value = NULL;
	DefElem    *increment_by = NULL;
	DefElem    *max_value = NULL;
	DefElem    *min_value = NULL;
	DefElem    *cache_value = NULL;
	List	   *option;

	new->is_cycled = 'f';
	foreach(option, seq->options)
	{
		DefElem    *defel = (DefElem *) lfirst(option);

		if (!strcasecmp(defel->defname, "increment"))
			increment_by = defel;
		else if (!strcasecmp(defel->defname, "start"))
			last_value = defel;
		else if (!strcasecmp(defel->defname, "maxvalue"))
			max_value = defel;
		else if (!strcasecmp(defel->defname, "minvalue"))
			min_value = defel;
		else if (!strcasecmp(defel->defname, "cache"))
			cache_value = defel;
		else if (!strcasecmp(defel->defname, "cycle"))
		{
			if (defel->arg != (Node *) NULL)
				elog(ERROR, "DefineSequence: CYCLE ??");
			new->is_cycled = 't';
		}
		else
			elog(ERROR, "DefineSequence: option \"%s\" not recognized",
				 defel->defname);
	}

	if (increment_by == (DefElem *) NULL)		/* INCREMENT BY */
		new->increment_by = 1;
	else if ((new->increment_by = get_param(increment_by)) == 0)
		elog(ERROR, "DefineSequence: can't INCREMENT by 0");

	if (max_value == (DefElem *) NULL)	/* MAXVALUE */
	{
		if (new->increment_by > 0)
			new->max_value = SEQ_MAXVALUE;		/* ascending seq */
		else
			new->max_value = -1;/* descending seq */
	}
	else
		new->max_value = get_param(max_value);

	if (min_value == (DefElem *) NULL)	/* MINVALUE */
	{
		if (new->increment_by > 0)
			new->min_value = 1; /* ascending seq */
		else
			new->min_value = SEQ_MINVALUE;		/* descending seq */
	}
	else
		new->min_value = get_param(min_value);

	if (new->min_value >= new->max_value)
		elog(ERROR, "DefineSequence: MINVALUE (%d) can't be >= MAXVALUE (%d)",
			 new->min_value, new->max_value);

	if (last_value == (DefElem *) NULL) /* START WITH */
	{
		if (new->increment_by > 0)
			new->last_value = new->min_value;	/* ascending seq */
		else
			new->last_value = new->max_value;	/* descending seq */
	}
	else
		new->last_value = get_param(last_value);

	if (new->last_value < new->min_value)
		elog(ERROR, "DefineSequence: START value (%d) can't be < MINVALUE (%d)",
			 new->last_value, new->min_value);
	if (new->last_value > new->max_value)
		elog(ERROR, "DefineSequence: START value (%d) can't be > MAXVALUE (%d)",
			 new->last_value, new->max_value);

	if (cache_value == (DefElem *) NULL)		/* CACHE */
		new->cache_value = 1;
	else if ((new->cache_value = get_param(cache_value)) <= 0)
		elog(ERROR, "DefineSequence: CACHE (%d) can't be <= 0",
			 new->cache_value);

}


static int
get_param(DefElem *def)
{
	if (def->arg == (Node *) NULL)
		elog(ERROR, "DefineSequence: \"%s\" value unspecified", def->defname);

	if (nodeTag(def->arg) == T_Integer)
		return intVal(def->arg);

	elog(ERROR, "DefineSequence: \"%s\" is to be integer", def->defname);
	return -1;
}

SeqCache*
get_seq_cache(void)
{
    SeqCache* table = sequence_globals;
    if ( table == NULL ) {
        table = (SeqCache*)AllocateEnvSpace(sequence_id,sizeof(SeqCache));
        sequence_globals = table;
    }
    return table;
}
