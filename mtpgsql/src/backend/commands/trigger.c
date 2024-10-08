/*-------------------------------------------------------------------------
 *
 * trigger.c
 *	  PostgreSQL TRIGGERs support code.
 *
 * Portions Copyright (c) 2000-2024, Myron Scott  <myron@weaverdb.org>
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * IDENTIFICATION
 *
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"
#include "env/env.h"
#include "access/genam.h"
#include "access/heapam.h"
#include "catalog/catalog.h"
#include "catalog/catname.h"
#include "catalog/indexing.h"
#include "catalog/pg_language.h"
#include "catalog/pg_proc.h"
#include "catalog/pg_trigger.h"
#include "commands/comment.h"
#include "commands/trigger.h"
#include "executor/executor.h"
#include "miscadmin.h"
#ifdef USEACL
#include "utils/acl.h"
#endif
#include "utils/builtins.h"
#include "utils/inval.h"
#include "utils/syscache.h"
#include "utils/tqual.h"


typedef struct trigger_info {
	TriggerData*		CurrentTriggerData;  
	MemoryContext 		deftrig_global_cxt;  
	MemoryContext 		deftrig_cxt;  
        
 /* ----------
 * Global data that tells which triggers are actually in
 * state IMMEDIATE or DEFERRED.
 * ----------
 */
        bool deftrig_dfl_all_isset;
        bool deftrig_dfl_all_isdeferred;
        List *deftrig_dfl_trigstates;

        bool deftrig_all_isset;
        bool deftrig_all_isdeferred;
        List *deftrig_trigstates;

/* ----------
 * The list of events during the entire transaction.
 *
 * XXX This must finally be held in a file because of the huge
 *	   number of events that could occur in the real world.
 * ----------
 */
        int	deftrig_n_events;
        List *deftrig_events;       
        
} TriggerInfo;

static SectionId  trigger_id = SECTIONID("TRIG");

#ifdef TLS
TLS TriggerInfo* trigger_globals = NULL;
#else
#define trigger_globals GetEnv()->trigger_globals
#endif

TriggerInfo* GetTriggerInfo(void);

/* XXX no points for style */
extern TupleTableSlot *EvalPlanQual(EState *estate, Index rti, ItemPointer tid);

static void DescribeTrigger(TriggerDesc *trigdesc, Trigger *trigger);
static HeapTuple GetTupleForTrigger(EState *estate, ItemPointer tid,
				   TupleTableSlot **newSlot);


void
CreateTrigger(CreateTrigStmt *stmt)
{
	int16		tgtype;
	int16		tgattr[FUNC_MAX_ARGS];
	Datum		values[Natts_pg_trigger];
	char		nulls[Natts_pg_trigger];
	Relation	rel;
	Relation	tgrel;
	HeapScanDesc tgscan;
	ScanKeyData key;
	Relation	pgrel;
	HeapTuple	tuple;
	Relation	idescs[Num_pg_trigger_indices];
	Relation	ridescs[Num_pg_class_indices];
	Oid			fargtypes[FUNC_MAX_ARGS];
	int			found = 0;
	int			i;
	char		constrtrigname[NAMEDATALEN];
	char	   *constrname = "";
	Oid			constrrelid = 0;

	if (!allowSystemTableMods && IsSystemRelationName(stmt->relname))
		elog(ERROR, "CreateTrigger: can't create trigger for system relation %s", stmt->relname);

#ifdef USEACL
	if (!pg_ownercheck(GetPgUserName(), stmt->relname, RELNAME))
		elog(ERROR, "%s: %s", stmt->relname, aclcheck_error_strings[ACLCHECK_NOT_OWNER]);
#endif

	/* ----------
	 * If trigger is a constraint, user trigger name as constraint
	 * name and build a unique trigger name instead.
	 * ----------
	 */
	if (stmt->isconstraint)
	{
		constrname = stmt->trigname;
		stmt->trigname = constrtrigname;
		sprintf(constrtrigname, "RI_ConstraintTrigger_%ld", newoid());

		if (strcmp(stmt->constrrelname, "") == 0)
			constrrelid = 0;
		else
		{
			rel = heap_openr(stmt->constrrelname, NoLock);
			if (rel == NULL)
				elog(ERROR, "table \"%s\" does not exist",
					 stmt->constrrelname);
			constrrelid = rel->rd_id;
			heap_close(rel, NoLock);
		}
	}

	rel = heap_openr(stmt->relname, AccessExclusiveLock);

	TRIGGER_CLEAR_TYPE(tgtype);
	if (stmt->before)
		TRIGGER_SETT_BEFORE(tgtype);
	if (stmt->row)
		TRIGGER_SETT_ROW(tgtype);
	else
		elog(ERROR, "CreateTrigger: STATEMENT triggers are unimplemented, yet");

	for (i = 0; i < 3 && stmt->actions[i]; i++)
	{
		switch (stmt->actions[i])
		{
			case 'i':
				if (TRIGGER_FOR_INSERT(tgtype))
					elog(ERROR, "CreateTrigger: double INSERT event specified");
				TRIGGER_SETT_INSERT(tgtype);
				break;
			case 'd':
				if (TRIGGER_FOR_DELETE(tgtype))
					elog(ERROR, "CreateTrigger: double DELETE event specified");
				TRIGGER_SETT_DELETE(tgtype);
				break;
			case 'u':
				if (TRIGGER_FOR_UPDATE(tgtype))
					elog(ERROR, "CreateTrigger: double UPDATE event specified");
				TRIGGER_SETT_UPDATE(tgtype);
				break;
			default:
				elog(ERROR, "CreateTrigger: unknown event specified");
				break;
		}
	}

	/* Scan pg_trigger */
	tgrel = heap_openr(TriggerRelationName, RowExclusiveLock);
	ScanKeyEntryInitialize(&key, 0, Anum_pg_trigger_tgrelid,
						   F_OIDEQ, RelationGetRelid(rel));
	tgscan = heap_beginscan(tgrel, SnapshotNow, 1, &key);
	while (HeapTupleIsValid(tuple = heap_getnext(tgscan)))
	{
		Form_pg_trigger pg_trigger = (Form_pg_trigger) GETSTRUCT(tuple);

		if (namestrcmp(&(pg_trigger->tgname), stmt->trigname) == 0)
			elog(ERROR, "CreateTrigger: trigger %s already defined on relation %s",
				 stmt->trigname, stmt->relname);
		else
			found++;
	}
	heap_endscan(tgscan);

	MemSet(fargtypes, 0, FUNC_MAX_ARGS * sizeof(Oid));
	tuple = SearchSysCacheTuple(PROCNAME,
								PointerGetDatum(stmt->funcname),
								Int32GetDatum(0),
								PointerGetDatum(fargtypes),
								0);
	if (!HeapTupleIsValid(tuple) ||
		((Form_pg_proc) GETSTRUCT(tuple))->pronargs != 0)
		elog(ERROR, "CreateTrigger: function %s() does not exist",
			 stmt->funcname);
	if (((Form_pg_proc) GETSTRUCT(tuple))->prorettype != 0)
		elog(ERROR, "CreateTrigger: function %s() must return OPAQUE",
			 stmt->funcname);
	if (((Form_pg_proc) GETSTRUCT(tuple))->prolang != ClanguageId &&
		((Form_pg_proc) GETSTRUCT(tuple))->prolang != INTERNALlanguageId)
	{
		HeapTuple	langTup;

		langTup = SearchSysCacheTuple(LANGOID,
			ObjectIdGetDatum(((Form_pg_proc) GETSTRUCT(tuple))->prolang),
									  0, 0, 0);
		if (!HeapTupleIsValid(langTup))
			elog(ERROR, "CreateTrigger: cache lookup for PL %lu failed",
				 ((Form_pg_proc) GETSTRUCT(tuple))->prolang);
		if (((Form_pg_language) GETSTRUCT(langTup))->lanispl == false)
			elog(ERROR, "CreateTrigger: only builtin, C and PL functions are supported");
	}

	MemSet(nulls, ' ', Natts_pg_trigger * sizeof(char));

	values[Anum_pg_trigger_tgrelid - 1] = ObjectIdGetDatum(RelationGetRelid(rel));
	values[Anum_pg_trigger_tgname - 1] = NameGetDatum(namein(stmt->trigname));
	values[Anum_pg_trigger_tgfoid - 1] = ObjectIdGetDatum(tuple->t_data->t_oid);
	values[Anum_pg_trigger_tgtype - 1] = Int16GetDatum(tgtype);

	values[Anum_pg_trigger_tgenabled - 1] = true;
	values[Anum_pg_trigger_tgisconstraint - 1] = stmt->isconstraint;
	values[Anum_pg_trigger_tgconstrname - 1] = PointerGetDatum(constrname);;
	values[Anum_pg_trigger_tgconstrrelid - 1] = constrrelid;
	values[Anum_pg_trigger_tgdeferrable - 1] = stmt->deferrable;
	values[Anum_pg_trigger_tginitdeferred - 1] = stmt->initdeferred;

	if (stmt->args)
	{
		List	   *le;
		char	   *args;
		int16		nargs = length(stmt->args);
		int			len = 0;

		foreach(le, stmt->args)
		{
			char	   *ar = (char *) lfirst(le);

			len += strlen(ar) + VARHDRSZ;
			for (; *ar; ar++)
			{
				if (*ar == '\\')
					len++;
			}
		}
		args = (char *) palloc(len + 1);
		args[0] = 0;
		foreach(le, stmt->args)
		{
			char	   *s = (char *) lfirst(le);
			char	   *d = args + strlen(args);

			while (*s)
			{
				if (*s == '\\')
					*d++ = '\\';
				*d++ = *s++;
			}
			*d = 0;
			strcat(args, "\\000");
		}
		values[Anum_pg_trigger_tgnargs - 1] = Int16GetDatum(nargs);
		values[Anum_pg_trigger_tgargs - 1] = PointerGetDatum(byteain(args));
	}
	else
	{
		values[Anum_pg_trigger_tgnargs - 1] = Int16GetDatum(0);
		values[Anum_pg_trigger_tgargs - 1] = PointerGetDatum(byteain(""));
	}
	MemSet(tgattr, 0, FUNC_MAX_ARGS * sizeof(int16));
	values[Anum_pg_trigger_tgattr - 1] = PointerGetDatum(tgattr);

	tuple = heap_formtuple(tgrel->rd_att, values, nulls);
	heap_insert(tgrel, tuple);
	CatalogOpenIndices(Num_pg_trigger_indices, Name_pg_trigger_indices, idescs);
	CatalogIndexInsert(idescs, Num_pg_trigger_indices, tgrel, tuple);
	CatalogCloseIndices(Num_pg_trigger_indices, idescs);
	heap_freetuple(tuple);
	heap_close(tgrel, RowExclusiveLock);

	pfree(DatumGetPointer(values[Anum_pg_trigger_tgname - 1]));
	pfree(DatumGetPointer(values[Anum_pg_trigger_tgargs - 1]));

	/* update pg_class */
	pgrel = heap_openr(RelationRelationName, RowExclusiveLock);
	tuple = SearchSysCacheTupleCopy(RELNAME,
									PointerGetDatum(stmt->relname),
									0, 0, 0);
	if (!HeapTupleIsValid(tuple))
		elog(ERROR, "CreateTrigger: relation %s not found in pg_class", stmt->relname);

	((Form_pg_class) GETSTRUCT(tuple))->reltriggers = found + 1;
	RelationInvalidateHeapTuple(pgrel, tuple);
	heap_update(pgrel, &tuple->t_self, tuple, NULL, NULL);
	CatalogOpenIndices(Num_pg_class_indices, Name_pg_class_indices, ridescs);
	CatalogIndexInsert(ridescs, Num_pg_class_indices, pgrel, tuple);
	CatalogCloseIndices(Num_pg_class_indices, ridescs);
	heap_freetuple(tuple);
	heap_close(pgrel, RowExclusiveLock);

	/*
	 * We used to try to update the rel's relcache entry here, but that's
	 * fairly pointless since it will happen as a byproduct of the
	 * upcoming CommandCounterIncrement...
	 */
	/* Keep lock on target rel until end of xact */
	heap_close(rel, NoLock);
}

void
DropTrigger(DropTrigStmt *stmt)
{
	Relation	rel;
	Relation	tgrel;
	HeapScanDesc tgscan;
	ScanKeyData key;
	Relation	pgrel;
	HeapTuple	tuple;
	Relation	ridescs[Num_pg_class_indices];
	int			found = 0;
	int			tgfound = 0;

#ifdef USEACL
	if (!pg_ownercheck(GetPgUserName(), stmt->relname, RELNAME))
		elog(ERROR, "%s: %s", stmt->relname, aclcheck_error_strings[ACLCHECK_NOT_OWNER]);
#endif

	rel = heap_openr(stmt->relname, AccessExclusiveLock);

	tgrel = heap_openr(TriggerRelationName, RowExclusiveLock);
	ScanKeyEntryInitialize(&key, 0, Anum_pg_trigger_tgrelid,
						   F_OIDEQ, RelationGetRelid(rel));
	tgscan = heap_beginscan(tgrel, SnapshotNow, 1, &key);
	while (HeapTupleIsValid(tuple = heap_getnext(tgscan)))
	{
		Form_pg_trigger pg_trigger = (Form_pg_trigger) GETSTRUCT(tuple);

		if (namestrcmp(&(pg_trigger->tgname), stmt->trigname) == 0)
		{

			/*** Delete any comments associated with this trigger ***/

			DeleteComments(tuple->t_data->t_oid);

			heap_delete(tgrel, &tuple->t_self, NULL,NULL);
			tgfound++;

		}
		else
			found++;
	}
	if (tgfound == 0)
		elog(ERROR, "DropTrigger: there is no trigger %s on relation %s",
			 stmt->trigname, stmt->relname);
	if (tgfound > 1)
		elog(NOTICE, "DropTrigger: found (and deleted) %d triggers %s on relation %s",
			 tgfound, stmt->trigname, stmt->relname);
	heap_endscan(tgscan);
	heap_close(tgrel, RowExclusiveLock);

	/* update pg_class */
	pgrel = heap_openr(RelationRelationName, RowExclusiveLock);
	tuple = SearchSysCacheTupleCopy(RELNAME,
									PointerGetDatum(stmt->relname),
									0, 0, 0);
	if (!HeapTupleIsValid(tuple))
		elog(ERROR, "DropTrigger: relation %s not found in pg_class", stmt->relname);

	((Form_pg_class) GETSTRUCT(tuple))->reltriggers = found;
	RelationInvalidateHeapTuple(pgrel, tuple);
	heap_update(pgrel, &tuple->t_self, tuple, NULL, NULL);
	CatalogOpenIndices(Num_pg_class_indices, Name_pg_class_indices, ridescs);
	CatalogIndexInsert(ridescs, Num_pg_class_indices, pgrel, tuple);
	CatalogCloseIndices(Num_pg_class_indices, ridescs);
	heap_freetuple(tuple);
	heap_close(pgrel, RowExclusiveLock);

	/*
	 * We used to try to update the rel's relcache entry here, but that's
	 * fairly pointless since it will happen as a byproduct of the
	 * upcoming CommandCounterIncrement...
	 */
	/* Keep lock on target rel until end of xact */
	heap_close(rel, NoLock);
}

void
RelationRemoveTriggers(Relation rel)
{
	Relation	tgrel;
	HeapScanDesc tgscan;
	ScanKeyData key;
	HeapTuple	tup;

	tgrel = heap_openr(TriggerRelationName, RowExclusiveLock);
	ScanKeyEntryInitialize(&key, 0, Anum_pg_trigger_tgrelid,
						   F_OIDEQ, RelationGetRelid(rel));

	tgscan = heap_beginscan(tgrel, SnapshotNow, 1, &key);

	while (HeapTupleIsValid(tup = heap_getnext(tgscan)))
	{

		/*** Delete any comments associated with this trigger ***/

		DeleteComments(tup->t_data->t_oid);

		heap_delete(tgrel, &tup->t_self, NULL,NULL);

	}

	heap_endscan(tgscan);

	/* ----------
	 * Need to bump it here so the following doesn't see
	 * the already deleted triggers again for a self-referencing
	 * table.
	 * ----------
	 */
	CommandCounterIncrement();

	/* ----------
	 * Also drop all constraint triggers referencing this relation
	 * ----------
	 */
	ScanKeyEntryInitialize(&key, 0, Anum_pg_trigger_tgconstrrelid,
						   F_OIDEQ, RelationGetRelid(rel));

	tgscan = heap_beginscan(tgrel, SnapshotNow, 1, &key);
	while (HeapTupleIsValid(tup = heap_getnext(tgscan)))
	{
		Form_pg_trigger pg_trigger;
		Relation	refrel;
		DropTrigStmt stmt;

		pg_trigger = (Form_pg_trigger) GETSTRUCT(tup);

		refrel = heap_open(pg_trigger->tgrelid, NoLock);

		stmt.relname = pstrdup(RelationGetRelationName(refrel));
		stmt.trigname = nameout(&pg_trigger->tgname);

		heap_close(refrel, NoLock);

		elog(NOTICE, "DROP TABLE implicitly drops referential integrity trigger from table \"%s\"", stmt.relname);

		DropTrigger(&stmt);

		/* ----------
		 * Need to do a command counter increment here to show up
		 * new pg_class.reltriggers in the next loop invocation already
		 * (there are multiple referential integrity action
		 * triggers for the same FK table defined on the PK table).
		 * ----------
		 */
		CommandCounterIncrement();

		pfree(stmt.relname);
		pfree(stmt.trigname);
	}
	heap_endscan(tgscan);

	heap_close(tgrel, RowExclusiveLock);
}

void
RelationBuildTriggers(Relation relation)
{
	TriggerDesc *trigdesc = (TriggerDesc *) palloc(sizeof(TriggerDesc));
	int			ntrigs = relation->rd_rel->reltriggers;
	Trigger    *triggers = NULL;
	Trigger    *build;
	Relation	tgrel;
	Form_pg_trigger pg_trigger;
	Relation	irel = (Relation) NULL;
	ScanKeyData skey;
	HeapTupleData tuple;
	IndexScanDesc sd = (IndexScanDesc) NULL;
	HeapScanDesc tgscan = (HeapScanDesc) NULL;
	HeapTuple	htup;
	Buffer		buffer;
	struct varlena *val;
	bool		isnull;
	int			found;
	bool		hasindex;

	MemSet(trigdesc, 0, sizeof(TriggerDesc));

	ScanKeyEntryInitialize(&skey,
						   (bits16) 0x0,
						   (AttrNumber) 1,
						   (RegProcedure) F_OIDEQ,
						   ObjectIdGetDatum(RelationGetRelid(relation)));

	tgrel = heap_openr(TriggerRelationName, AccessShareLock);
	hasindex = (tgrel->rd_rel->relhasindex && !IsIgnoringSystemIndexes());
	if (hasindex)
	{
		irel = index_openr(TriggerRelidIndex);
		sd = index_beginscan(irel, false, 1, &skey);
	}
	else
		tgscan = heap_beginscan(tgrel, SnapshotNow, 1, &skey);

	for (found = 0;;)
	{
		if (hasindex)
		{
			if (!index_getnext(sd, ForwardScanDirection) ) break;

			tuple.t_self = sd->xs_ctup.t_self;
			heap_fetch(tgrel, SnapshotNow, &tuple, &buffer);
			if (!tuple.t_data)
				continue;
			htup = &tuple;
		}
		else
		{
			htup = heap_getnext(tgscan);
			if (!HeapTupleIsValid(htup))
				break;
		}
		if (found == ntrigs)
			elog(ERROR, "RelationBuildTriggers: unexpected record found for rel %s",
				 RelationGetRelationName(relation));

		pg_trigger = (Form_pg_trigger) GETSTRUCT(htup);

		if (triggers == NULL)
			triggers = (Trigger *) palloc(sizeof(Trigger));
		else
			triggers = (Trigger *) repalloc(triggers, (found + 1) * sizeof(Trigger));
		build = &(triggers[found]);

		build->tgoid = htup->t_data->t_oid;
		build->tgname = nameout(&pg_trigger->tgname);
		build->tgfoid = pg_trigger->tgfoid;
		build->tgfunc.fn_addr = NULL;
		build->tgtype = pg_trigger->tgtype;
		build->tgenabled = pg_trigger->tgenabled;
		build->tgisconstraint = pg_trigger->tgisconstraint;
		build->tgdeferrable = pg_trigger->tgdeferrable;
		build->tginitdeferred = pg_trigger->tginitdeferred;
		build->tgnargs = pg_trigger->tgnargs;
		memcpy(build->tgattr, &(pg_trigger->tgattr), FUNC_MAX_ARGS * sizeof(int16));
		val = (struct varlena *) fastgetattr(htup,
											 Anum_pg_trigger_tgargs,
											 tgrel->rd_att, &isnull);
		if (isnull)
			elog(ERROR, "RelationBuildTriggers: tgargs IS NULL for rel %s",
				 RelationGetRelationName(relation));
		if (build->tgnargs > 0)
		{
			char	   *p;
			int			i;

			val = (struct varlena *) fastgetattr(htup,
												 Anum_pg_trigger_tgargs,
												 tgrel->rd_att, &isnull);
			if (isnull)
				elog(ERROR, "RelationBuildTriggers: tgargs IS NULL for rel %s",
					 RelationGetRelationName(relation));
			p = (char *) VARDATA(val);
			build->tgargs = (char **) palloc(build->tgnargs * sizeof(char *));
			for (i = 0; i < build->tgnargs; i++)
			{
				build->tgargs[i] = pstrdup(p);
				p += strlen(p) + 1;
			}
		}
		else
			build->tgargs = NULL;

		found++;
		if (hasindex)
			ReleaseBuffer(relation, buffer);
	}

	if (found < ntrigs)
		elog(ERROR, "RelationBuildTriggers: %d record(s) not found for rel %s",
			 ntrigs - found,
			 RelationGetRelationName(relation));

	if (hasindex)
	{
		index_endscan(sd);
		index_close(irel);
	}
	else
		heap_endscan(tgscan);
	heap_close(tgrel, AccessShareLock);

	/* Build trigdesc */
	trigdesc->triggers = triggers;
	trigdesc->numtriggers = ntrigs;
	for (found = 0; found < ntrigs; found++)
		DescribeTrigger(trigdesc, &(triggers[found]));

	relation->trigdesc = trigdesc;
}

static void
DescribeTrigger(TriggerDesc *trigdesc, Trigger *trigger)
{
	uint16	   *n;
	Trigger  ***t,
			 ***tp;

	if (TRIGGER_FOR_ROW(trigger->tgtype))		/* Is ROW/STATEMENT
												 * trigger */
	{
		if (TRIGGER_FOR_BEFORE(trigger->tgtype))
		{
			n = trigdesc->n_before_row;
			t = trigdesc->tg_before_row;
		}
		else
		{
			n = trigdesc->n_after_row;
			t = trigdesc->tg_after_row;
		}
	}
	else
/* STATEMENT (NI) */
	{
		if (TRIGGER_FOR_BEFORE(trigger->tgtype))
		{
			n = trigdesc->n_before_statement;
			t = trigdesc->tg_before_statement;
		}
		else
		{
			n = trigdesc->n_after_statement;
			t = trigdesc->tg_after_statement;
		}
	}

	if (TRIGGER_FOR_INSERT(trigger->tgtype))
	{
		tp = &(t[TRIGGER_EVENT_INSERT]);
		if (*tp == NULL)
			*tp = (Trigger **) palloc(sizeof(Trigger *));
		else
			*tp = (Trigger **) repalloc(*tp, (n[TRIGGER_EVENT_INSERT] + 1) *
										sizeof(Trigger *));
		(*tp)[n[TRIGGER_EVENT_INSERT]] = trigger;
		(n[TRIGGER_EVENT_INSERT])++;
	}

	if (TRIGGER_FOR_DELETE(trigger->tgtype))
	{
		tp = &(t[TRIGGER_EVENT_DELETE]);
		if (*tp == NULL)
			*tp = (Trigger **) palloc(sizeof(Trigger *));
		else
			*tp = (Trigger **) repalloc(*tp, (n[TRIGGER_EVENT_DELETE] + 1) *
										sizeof(Trigger *));
		(*tp)[n[TRIGGER_EVENT_DELETE]] = trigger;
		(n[TRIGGER_EVENT_DELETE])++;
	}

	if (TRIGGER_FOR_UPDATE(trigger->tgtype))
	{
		tp = &(t[TRIGGER_EVENT_UPDATE]);
		if (*tp == NULL)
			*tp = (Trigger **) palloc(sizeof(Trigger *));
		else
			*tp = (Trigger **) repalloc(*tp, (n[TRIGGER_EVENT_UPDATE] + 1) *
										sizeof(Trigger *));
		(*tp)[n[TRIGGER_EVENT_UPDATE]] = trigger;
		(n[TRIGGER_EVENT_UPDATE])++;
	}

}

void
FreeTriggerDesc(TriggerDesc *trigdesc)
{
	Trigger  ***t;
	Trigger    *trigger;
	int			i;

	if (trigdesc == NULL)
		return;

	t = trigdesc->tg_before_statement;
	for (i = 0; i < 4; i++)
		if (t[i] != NULL)
			pfree(t[i]);
	t = trigdesc->tg_before_row;
	for (i = 0; i < 4; i++)
		if (t[i] != NULL)
			pfree(t[i]);
	t = trigdesc->tg_after_row;
	for (i = 0; i < 4; i++)
		if (t[i] != NULL)
			pfree(t[i]);
	t = trigdesc->tg_after_statement;
	for (i = 0; i < 4; i++)
		if (t[i] != NULL)
			pfree(t[i]);

	trigger = trigdesc->triggers;
	for (i = 0; i < trigdesc->numtriggers; i++)
	{
		pfree(trigger->tgname);
		if (trigger->tgnargs > 0)
		{
			while (--(trigger->tgnargs) >= 0)
				pfree(trigger->tgargs[trigger->tgnargs]);
			pfree(trigger->tgargs);
		}
		trigger++;
	}
	pfree(trigdesc->triggers);
	pfree(trigdesc);
}

bool
equalTriggerDescs(TriggerDesc *trigdesc1, TriggerDesc *trigdesc2)
{
	int			i,
				j;

	/*
	 * We need not examine the "index" data, just the trigger array
	 * itself; if we have the same triggers with the same types, the
	 * derived index data should match.
	 *
	 * XXX It seems possible that the same triggers could appear in different
	 * orders in the two trigger arrays; do we need to handle that?
	 */
	if (trigdesc1 != NULL)
	{
		if (trigdesc2 == NULL)
			return false;
		if (trigdesc1->numtriggers != trigdesc2->numtriggers)
			return false;
		for (i = 0; i < trigdesc1->numtriggers; i++)
		{
			Trigger    *trig1 = trigdesc1->triggers + i;
			Trigger    *trig2 = NULL;

			/*
			 * We can't assume that the triggers are always read from
			 * pg_trigger in the same order; so use the trigger OIDs to
			 * identify the triggers to compare.  (We assume here that the
			 * same OID won't appear twice in either trigger set.)
			 */
			for (j = 0; j < trigdesc2->numtriggers; j++)
			{
				trig2 = trigdesc2->triggers + i;
				if (trig1->tgoid == trig2->tgoid)
					break;
			}
			if (j >= trigdesc2->numtriggers)
				return false;
			if (strcmp(trig1->tgname, trig2->tgname) != 0)
				return false;
			if (trig1->tgfoid != trig2->tgfoid)
				return false;
			/* need not examine tgfunc, if tgfoid matches */
			if (trig1->tgtype != trig2->tgtype)
				return false;
			if (trig1->tgenabled != trig2->tgenabled)
				return false;
			if (trig1->tgisconstraint != trig2->tgisconstraint)
				return false;
			if (trig1->tgdeferrable != trig2->tgdeferrable)
				return false;
			if (trig1->tginitdeferred != trig2->tginitdeferred)
				return false;
			if (trig1->tgnargs != trig2->tgnargs)
				return false;
			if (memcmp(trig1->tgattr, trig2->tgattr,
					   sizeof(trig1->tgattr)) != 0)
				return false;
			for (j = 0; j < trig1->tgnargs; j++)
				if (strcmp(trig1->tgargs[j], trig2->tgargs[j]) != 0)
					return false;
		}
	}
	else if (trigdesc2 != NULL)
		return false;
	return true;
}


static HeapTuple
ExecCallTriggerFunc(Trigger *trigger)
{

	if (trigger->tgfunc.fn_addr == NULL)
		fmgr_info(trigger->tgfoid, &trigger->tgfunc);

	return (HeapTuple) ((*fmgr_faddr(&trigger->tgfunc)) ());
}

HeapTuple
ExecBRInsertTriggers(Relation rel, HeapTuple trigtuple)
{
	TriggerData *SaveTriggerData;
	int			ntrigs = rel->trigdesc->n_before_row[TRIGGER_EVENT_INSERT];
	Trigger   **trigger = rel->trigdesc->tg_before_row[TRIGGER_EVENT_INSERT];
	HeapTuple	newtuple = trigtuple;
	HeapTuple	oldtuple;
	int			i;

	SaveTriggerData = (TriggerData *) palloc(sizeof(TriggerData));
	SaveTriggerData->tg_event = TRIGGER_EVENT_INSERT | TRIGGER_EVENT_ROW | TRIGGER_EVENT_BEFORE;
	SaveTriggerData->tg_relation = rel;
	SaveTriggerData->tg_newtuple = NULL;
	for (i = 0; i < ntrigs; i++)
	{
		if (!trigger[i]->tgenabled)
			continue;
		SetTriggerData(SaveTriggerData);
		SaveTriggerData->tg_trigtuple = oldtuple = newtuple;
		SaveTriggerData->tg_trigger = trigger[i];
		newtuple = ExecCallTriggerFunc(trigger[i]);
		if (newtuple == NULL)
			break;
		else if (oldtuple != newtuple && oldtuple != trigtuple)
			heap_freetuple(oldtuple);
	}
	SetTriggerData(NULL);
	pfree(SaveTriggerData);
	return newtuple;
}

void
ExecARInsertTriggers(Relation rel, HeapTuple trigtuple)
{
	DeferredTriggerSaveEvent(rel, TRIGGER_EVENT_INSERT, NULL, trigtuple);
	return;
}

bool
ExecBRDeleteTriggers(EState *estate, ItemPointer tupleid)
{
	Relation	rel = estate->es_result_relation_info->ri_RelationDesc;
	TriggerData *SaveTriggerData;
	int			ntrigs = rel->trigdesc->n_before_row[TRIGGER_EVENT_DELETE];
	Trigger   **trigger = rel->trigdesc->tg_before_row[TRIGGER_EVENT_DELETE];
	HeapTuple	trigtuple;
	HeapTuple	newtuple = NULL;
	TupleTableSlot *newSlot;
	int			i;

	trigtuple = GetTupleForTrigger(estate, tupleid, &newSlot);
	if (trigtuple == NULL)
		return false;

	SaveTriggerData = (TriggerData *) palloc(sizeof(TriggerData));
	SaveTriggerData->tg_event = TRIGGER_EVENT_DELETE | TRIGGER_EVENT_ROW | TRIGGER_EVENT_BEFORE;
	SaveTriggerData->tg_relation = rel;
	SaveTriggerData->tg_newtuple = NULL;
	for (i = 0; i < ntrigs; i++)
	{
		if (!trigger[i]->tgenabled)
			continue;
		SetTriggerData(SaveTriggerData);
		SaveTriggerData->tg_trigtuple = trigtuple;
		SaveTriggerData->tg_trigger = trigger[i];
		newtuple = ExecCallTriggerFunc(trigger[i]);
		if (newtuple == NULL)
			break;
		if (newtuple != trigtuple)
			heap_freetuple(newtuple);
	}
	SetTriggerData(NULL);
	pfree(SaveTriggerData);
	heap_freetuple(trigtuple);

	return (newtuple == NULL) ? false : true;
}

void
ExecARDeleteTriggers(EState *estate, ItemPointer tupleid)
{
	Relation	rel = estate->es_result_relation_info->ri_RelationDesc;
	HeapTuple	trigtuple = GetTupleForTrigger(estate, tupleid, NULL);

	DeferredTriggerSaveEvent(rel, TRIGGER_EVENT_DELETE, trigtuple, NULL);
	return;
}

HeapTuple
ExecBRUpdateTriggers(EState *estate, ItemPointer tupleid, HeapTuple newtuple)
{
	Relation	rel = estate->es_result_relation_info->ri_RelationDesc;
	TriggerData *SaveTriggerData;
	int			ntrigs = rel->trigdesc->n_before_row[TRIGGER_EVENT_UPDATE];
	Trigger   **trigger = rel->trigdesc->tg_before_row[TRIGGER_EVENT_UPDATE];
	HeapTuple	trigtuple;
	HeapTuple	oldtuple;
	HeapTuple	intuple = newtuple;
	TupleTableSlot *newSlot;
	int			i;

	trigtuple = GetTupleForTrigger(estate, tupleid, &newSlot);
	if (trigtuple == NULL)
		return NULL;

	/*
	 * In READ COMMITTED isolevel it's possible that newtuple was changed
	 * due to concurrent update.
	 */
	if (newSlot != NULL)
		intuple = newtuple = ExecRemoveJunk(estate->es_junkFilter, newSlot);

	SaveTriggerData = (TriggerData *) palloc(sizeof(TriggerData));
	SaveTriggerData->tg_event = TRIGGER_EVENT_UPDATE | TRIGGER_EVENT_ROW | TRIGGER_EVENT_BEFORE;
	SaveTriggerData->tg_relation = rel;
	for (i = 0; i < ntrigs; i++)
	{
		if (!trigger[i]->tgenabled)
			continue;
		SetTriggerData(SaveTriggerData);
		SaveTriggerData->tg_trigtuple = trigtuple;
		SaveTriggerData->tg_newtuple = oldtuple = newtuple;
		SaveTriggerData->tg_trigger = trigger[i];
		newtuple = ExecCallTriggerFunc(trigger[i]);
		if (newtuple == NULL)
			break;
		else if (oldtuple != newtuple && oldtuple != intuple)
			heap_freetuple(oldtuple);
	}
	SetTriggerData(NULL);
	pfree(SaveTriggerData);
	heap_freetuple(trigtuple);
	return newtuple;
}

void
ExecARUpdateTriggers(EState *estate, ItemPointer tupleid, HeapTuple newtuple)
{
	Relation	rel = estate->es_result_relation_info->ri_RelationDesc;
	HeapTuple	trigtuple = GetTupleForTrigger(estate, tupleid, NULL);

	DeferredTriggerSaveEvent(rel, TRIGGER_EVENT_UPDATE, trigtuple, newtuple);
	return;
}


static HeapTuple
GetTupleForTrigger(EState *estate, ItemPointer tid, TupleTableSlot **newSlot)
{
	Relation	relation = estate->es_result_relation_info->ri_RelationDesc;
	HeapTupleData tuple;
	HeapTuple	result;
	Buffer		buffer;

	if (newSlot != NULL)
	{
		int			test;

		/*
		 * mark tuple for update
		 */
		*newSlot = NULL;
		tuple.t_self = *tid;
ltrmark:;
		test = heap_mark4update(relation, &buffer, &tuple, estate->es_snapshot);
		switch (test)
		{
			case HeapTupleSelfUpdated:
				ReleaseBuffer(relation, buffer);
				return (NULL);

			case HeapTupleMayBeUpdated:
				break;

			case HeapTupleUpdated:
				ReleaseBuffer(relation, buffer);
				if (GetTransactionInfo()->XactIsoLevel == XACT_SERIALIZABLE)
					elog(ERROR, "Can't serialize access due to concurrent update");
				else if (!(ItemPointerEquals(&(tuple.t_self), tid)))
				{
					TupleTableSlot *epqslot = EvalPlanQual(estate,
					 estate->es_result_relation_info->ri_RangeTableIndex,
														&(tuple.t_self));

					if (!(TupIsNull(epqslot)))
					{
						*tid = tuple.t_self;
						*newSlot = epqslot;
						goto ltrmark;
					}
				}

				/*
				 * if tuple was deleted or PlanQual failed for updated
				 * tuple - we have not process this tuple!
				 */
				return (NULL);

			default:
				ReleaseBuffer(relation, buffer);
				elog(ERROR, "Unknown status %u from heap_mark4update", test);
				return (NULL);
		}
	}
	else
	{
		PageHeader	dp;
		ItemId		lp;

		buffer = ReadBuffer(relation, ItemPointerGetBlockNumber(tid));

		if (!BufferIsValid(buffer))
			elog(ERROR, "GetTupleForTrigger: failed ReadBuffer");

		dp = (PageHeader) BufferGetPage(buffer);
		lp = PageGetItemId(dp, ItemPointerGetOffsetNumber(tid));

		Assert(ItemIdIsUsed(lp));

		tuple.t_datamcxt = NULL;
		tuple.t_datasrc = NULL;
		tuple.t_info = 0;
		tuple.t_data = (HeapTupleHeader) PageGetItem((Page) dp, lp);
		tuple.t_len = ItemIdGetLength(lp);
		tuple.t_self = *tid;
	}

	result = heap_copytuple(&tuple);
	ReleaseBuffer(relation, buffer);

	return result;
}


/* ----------
 * Deferred trigger stuff
 * ----------
 */


/* ----------
 * Internal data to the deferred trigger mechanism is held
 * during entire session in a global memor created at startup and
 * over statements/commands in a separate global memory which
 * is created at transaction start and destroyed at transaction
 * end.
 * ----------
 */

/* ----------
 * deferredTriggerCheckState()
 *
 *	Returns true if the trigger identified by tgoid is actually
 *	in state DEFERRED.
 * ----------
 */
static bool
deferredTriggerCheckState(Oid tgoid, int32 itemstate)
{
	MemoryContext oldcxt;
	List	   *sl;
	DeferredTriggerStatus trigstate;
    TriggerInfo*  info = GetTriggerInfo();

	/* ----------
	 * Not deferrable triggers (i.e. normal AFTER ROW triggers
	 * and constraints declared NOT DEFERRABLE, the state is
	 * allways false.
	 * ----------
	 */
	if ((itemstate & TRIGGER_DEFERRED_DEFERRABLE) == 0)
		return false;

	/* ----------
	 * Lookup if we know an individual state for this trigger
	 * ----------
	 */
	foreach(sl, info->deftrig_trigstates)
	{
		trigstate = (DeferredTriggerStatus) lfirst(sl);
		if (trigstate->dts_tgoid == tgoid)
			return trigstate->dts_tgisdeferred;
	}

	/* ----------
	 * No individual state known - so if the user issued a
	 * SET CONSTRAINT ALL ..., we return that instead of the
	 * triggers default state.
	 * ----------
	 */
	if (info->deftrig_all_isset)
		return info->deftrig_all_isdeferred;

	/* ----------
	 * No ALL state known either, remember the default state
	 * as the current and return that.
	 * ----------
	 */
	oldcxt = MemoryContextSwitchTo((MemoryContext) info->deftrig_cxt);

	trigstate = (DeferredTriggerStatus)
		palloc(sizeof(DeferredTriggerStatusData));
	trigstate->dts_tgoid = tgoid;
	trigstate->dts_tgisdeferred =
		((itemstate & TRIGGER_DEFERRED_INITDEFERRED) != 0);
	info->deftrig_trigstates = lappend(info->deftrig_trigstates, trigstate);

	MemoryContextSwitchTo(oldcxt);

	return trigstate->dts_tgisdeferred;
}


/* ----------
 * deferredTriggerAddEvent()
 *
 *	Add a new trigger event to the queue.
 * ----------
 */
static void
deferredTriggerAddEvent(DeferredTriggerEvent event)
{
	TriggerInfo* info = GetTriggerInfo();
        info->deftrig_events = lappend(info->deftrig_events, event);
	info->deftrig_n_events++;

	return;
}


/* ----------
 * deferredTriggerGetPreviousEvent()
 *
 *	Backward scan the eventlist to find the event a given OLD tuple
 *	resulted from in the same transaction.
 * ----------
 */
static DeferredTriggerEvent
deferredTriggerGetPreviousEvent(Oid relid, ItemPointer ctid)
{
	DeferredTriggerEvent previous;
	int			n;
        TriggerInfo*             info = GetTriggerInfo();

	for (n = info->deftrig_n_events - 1; n >= 0; n--)
	{
		previous = (DeferredTriggerEvent) nth(n, info->deftrig_events);

		if (previous->dte_relid != relid)
			continue;
		if (previous->dte_event & TRIGGER_DEFERRED_CANCELED)
			continue;

		if (ItemPointerGetBlockNumber(ctid) ==
			ItemPointerGetBlockNumber(&(previous->dte_newctid)) &&
			ItemPointerGetOffsetNumber(ctid) ==
			ItemPointerGetOffsetNumber(&(previous->dte_newctid)))
			return previous;
	}

	elog(ERROR,
	   "deferredTriggerGetPreviousEvent(): event for tuple %s not found",
		 tidout(ctid));
	return NULL;
}


/* ----------
 * deferredTriggerExecute()
 *
 *	Fetch the required tuples back from the heap and fire one
 *	single trigger function.
 * ----------
 */
static void
deferredTriggerExecute(DeferredTriggerEvent event, int itemno)
{
	Relation	rel;
	TriggerData SaveTriggerData;
	HeapTupleData oldtuple;
	HeapTupleData newtuple;
	HeapTuple	rettuple;
	Buffer		oldbuffer;
	Buffer		newbuffer;

	/* ----------
	 * Open the heap and fetch the required OLD and NEW tuples.
	 * ----------
	 */
	rel = heap_open(event->dte_relid, NoLock);

	if (ItemPointerIsValid(&(event->dte_oldctid)))
	{
		ItemPointerCopy(&(event->dte_oldctid), &(oldtuple.t_self));
		heap_fetch(rel, SnapshotAny, &oldtuple, &oldbuffer);
		if (!oldtuple.t_data)
			elog(ERROR, "deferredTriggerExecute(): failed to fetch old tuple");
	}

	if (ItemPointerIsValid(&(event->dte_newctid)))
	{
		ItemPointerCopy(&(event->dte_newctid), &(newtuple.t_self));
		heap_fetch(rel, SnapshotAny, &newtuple, &newbuffer);
		if (!newtuple.t_data)
			elog(ERROR, "deferredTriggerExecute(): failed to fetch new tuple");
	}

	/* ----------
	 * Setup the trigger information
	 * ----------
	 */
	SaveTriggerData.tg_event = (event->dte_event & TRIGGER_EVENT_OPMASK) |
		TRIGGER_EVENT_ROW;
	SaveTriggerData.tg_relation = rel;

	switch (event->dte_event & TRIGGER_EVENT_OPMASK)
	{
		case TRIGGER_EVENT_INSERT:
			SaveTriggerData.tg_trigtuple = &newtuple;
			SaveTriggerData.tg_newtuple = NULL;
			SaveTriggerData.tg_trigger =
				rel->trigdesc->tg_after_row[TRIGGER_EVENT_INSERT][itemno];
			break;

		case TRIGGER_EVENT_UPDATE:
			SaveTriggerData.tg_trigtuple = &oldtuple;
			SaveTriggerData.tg_newtuple = &newtuple;
			SaveTriggerData.tg_trigger =
				rel->trigdesc->tg_after_row[TRIGGER_EVENT_UPDATE][itemno];
			break;

		case TRIGGER_EVENT_DELETE:
			SaveTriggerData.tg_trigtuple = &oldtuple;
			SaveTriggerData.tg_newtuple = NULL;
			SaveTriggerData.tg_trigger =
				rel->trigdesc->tg_after_row[TRIGGER_EVENT_DELETE][itemno];
			break;
	}

	/* ----------
	 * Call the trigger and throw away an eventually returned
	 * updated tuple.
	 * ----------
	 */
	SetTriggerData(&SaveTriggerData);
	rettuple = ExecCallTriggerFunc(SaveTriggerData.tg_trigger);
	SetTriggerData(NULL);
	if (rettuple != NULL && rettuple != &oldtuple && rettuple != &newtuple)
		heap_freetuple(rettuple);

	/* ----------
	 * Might have been a referential integrity constraint trigger.
	 * Reset the snapshot overriding flag.
	 * ----------
	 */
	GetSnapshotHolder()->ReferentialIntegritySnapshotOverride = false;

	/* ----------
	 * Release buffers and close the relation
	 * ----------
	 */
	if (ItemPointerIsValid(&(event->dte_oldctid)))
		ReleaseBuffer(rel, oldbuffer);
	if (ItemPointerIsValid(&(event->dte_newctid)))
		ReleaseBuffer(rel, newbuffer);

	heap_close(rel, NoLock);

	return;
}


/* ----------
 * deferredTriggerInvokeEvents()
 *
 *	Scan the event queue for not yet invoked triggers. Check if they
 *	should be invoked now and do so.
 * ----------
 */
static void
deferredTriggerInvokeEvents(bool immediate_only)
{
	List	   *el;
	DeferredTriggerEvent event;
	int			still_deferred_ones;
//	int			eventno = -1;
	int			i;
        TriggerInfo*            info = GetTriggerInfo();

	/* ----------
	 * For now we process all events - to speedup transaction blocks
	 * we need to remember the actual end of the queue at EndQuery
	 * and process only events that are newer. On state changes we
	 * simply reset the position to the beginning of the queue and
	 * process all events once with the new states when the
	 * SET CONSTRAINTS ... command finishes and calls EndQuery.
	 * ----------
	 */
	foreach(el, info->deftrig_events)
	{
//		eventno++;

		/* ----------
		 * Get the event and check if it is completely done.
		 * ----------
		 */
		event = (DeferredTriggerEvent) lfirst(el);
		if (event->dte_event & (TRIGGER_DEFERRED_DONE |
								TRIGGER_DEFERRED_CANCELED))
			continue;

		/* ----------
		 * Check each trigger item in the event.
		 * ----------
		 */
		still_deferred_ones = false;
		for (i = 0; i < event->dte_n_items; i++)
		{
			if (event->dte_item[i].dti_state & TRIGGER_DEFERRED_DONE)
				continue;

			/* ----------
			 * This trigger item hasn't been called yet. Check if
			 * we should call it now.
			 * ----------
			 */
			if (immediate_only && deferredTriggerCheckState(
											event->dte_item[i].dti_tgoid,
										   event->dte_item[i].dti_state))
			{
				still_deferred_ones = true;
				continue;
			}

			/* ----------
			 * So let's fire it...
			 * ----------
			 */
			deferredTriggerExecute(event, i);
			event->dte_item[i].dti_state |= TRIGGER_DEFERRED_DONE;
		}

		/* ----------
		 * Remember in the event itself if all trigger items are
		 * done.
		 * ----------
		 */
		if (!still_deferred_ones)
			event->dte_event |= TRIGGER_DEFERRED_DONE;
	}
}


/* ----------
 *
 *	Initialize the deferred trigger mechanism. This is called during
 *	backend startup and is guaranteed to be before the first of all
 *	transactions.
 * ----------
 */
TriggerInfo*
GetTriggerInfo(void)
{
    TriggerInfo* info = trigger_globals;
    if ( info == NULL ) {
        info = AllocateEnvSpace(trigger_id,sizeof(TriggerInfo));
        info->deftrig_global_cxt =  AllocSetContextCreate(MemoryContextGetTopContext(),
                                            "DeferredTriggerMemoryContext",
                                            0,
                                            1024,
                                            ALLOCSET_DEFAULT_MAXSIZE);
        info->deftrig_cxt = NULL;
        info->deftrig_dfl_all_isset = false;
        info->deftrig_dfl_all_isdeferred = false;
        info->deftrig_dfl_trigstates = NIL;
        
        trigger_globals = info;
    }
	return info;
}


/* ----------
 * DeferredTriggerBeginXact()
 *
 *	Called at transaction start (either BEGIN or implicit for single
 *	statement outside of transaction block).
 * ----------
 */
void
DeferredTriggerBeginXact(void)
{
	MemoryContext oldcxt;
	List	   *l;
	DeferredTriggerStatus dflstat;
	DeferredTriggerStatus stat;

    TriggerInfo*  info = GetTriggerInfo();

	if (info->deftrig_cxt != NULL)
		elog(FATAL,
		   "DeferredTriggerBeginXact() called while inside transaction");

	/* ----------
	 * Create the per transaction memory context and copy all states
	 * from the per session context to here.
	 * ----------
	 */
	info->deftrig_cxt = AllocSetContextCreate(MemoryContextGetEnv()->TopTransactionContext,
                                                              "DeferredTriggerContext",
                                                               ALLOCSET_DEFAULT_MINSIZE,
                                                               ALLOCSET_DEFAULT_INITSIZE,
                                                               ALLOCSET_DEFAULT_MAXSIZE);
	oldcxt = MemoryContextSwitchTo((MemoryContext) info->deftrig_cxt);

	info->deftrig_all_isset = info->deftrig_dfl_all_isset;
	info->deftrig_all_isdeferred = info->deftrig_dfl_all_isdeferred;

	info->deftrig_trigstates = NIL;
	foreach(l, info->deftrig_dfl_trigstates)
	{
		dflstat = (DeferredTriggerStatus) lfirst(l);
		stat = (DeferredTriggerStatus)
			palloc(sizeof(DeferredTriggerStatusData));

		stat->dts_tgoid = dflstat->dts_tgoid;
		stat->dts_tgisdeferred = dflstat->dts_tgisdeferred;

		info->deftrig_trigstates = lappend(info->deftrig_trigstates, stat);
	}

	MemoryContextSwitchTo(oldcxt);

	info->deftrig_n_events = 0;
	info->deftrig_events = NIL;
}


/* ----------
 * DeferredTriggerEndQuery()
 *
 *	Called after one query sent down by the user has completely been
 *	processed. At this time we invoke all outstanding IMMEDIATE triggers.
 * ----------
 */
void
DeferredTriggerEndQuery(void)
{
    TriggerInfo*  info = GetTriggerInfo();

	/* ----------
	 * Ignore call if we aren't in a transaction.
	 * ----------
	 */
	if (info->deftrig_cxt == NULL)
		return;

	deferredTriggerInvokeEvents(true);
}


/* ----------
 * DeferredTriggerEndXact()
 *
 *	Called just before the current transaction is committed. At this
 *	time we invoke all DEFERRED triggers and tidy up.
 * ----------
 */
void
DeferredTriggerEndXact(void)
{
    TriggerInfo*  info = GetTriggerInfo();

	/* ----------
	 * Ignore call if we aren't in a transaction.
	 * ----------
	 */
	if (info->deftrig_cxt == NULL)
		return;

	deferredTriggerInvokeEvents(false);
/*  Don't worry about this, it goes away with the transaction
	MemoryContextDelete(info->deftrig_cxt);
 */
        info->deftrig_cxt = NULL;
}


/* ----------
 * DeferredTriggerAbortXact()
 *
 *	The current transaction has entered the abort state.
 *	All outstanding triggers are canceled so we simply throw
 *	away anything we know.
 * ----------
 */
void
DeferredTriggerAbortXact(void)
{
    TriggerInfo*  info = GetTriggerInfo();

	/* ----------
	 * Ignore call if we aren't in a transaction.
	 * ----------
	 */
	if (info->deftrig_cxt == NULL)
		return;
	
/*
	MemoryContextDelete(info->deftrig_cxt);
*/
        info->deftrig_cxt = NULL;
}


/* ----------
 * DeferredTriggerSetState()
 *
 *	Called for the users SET CONSTRAINTS ... utility command.
 * ----------
 */
void
DeferredTriggerSetState(ConstraintsSetStmt *stmt)
{
	Relation	tgrel;
	Relation	irel = (Relation) NULL;
	List	   *l;
	List	   *ls;
	List	   *lnext;
	List	   *loid = NIL;
	MemoryContext oldcxt;
	bool		found;
	DeferredTriggerStatus state;
	bool		hasindex;
    TriggerInfo*  info = GetTriggerInfo();

	/* ----------
	 * Handle SET CONSTRAINTS ALL ...
	 * ----------
	 */
	if (stmt->constraints == NIL)
	{
		if (!IsTransactionBlock())
		{
			/* ----------
			 * ... outside of a transaction block
			 * ----------
			 */
			oldcxt = MemoryContextSwitchTo((MemoryContext) info->deftrig_global_cxt);

			/* ----------
			 * Drop all information about individual trigger states per
			 * session.
			 * ----------
			 */
			l = info->deftrig_dfl_trigstates;
			while (l != NIL)
			{
				lnext = lnext(l);
				pfree(lfirst(l));
				pfree(l);
				l = lnext;
			}
			info->deftrig_dfl_trigstates = NIL;

			/* ----------
			 * Set the session ALL state to known.
			 * ----------
			 */
			info->deftrig_dfl_all_isset = true;
			info->deftrig_dfl_all_isdeferred = stmt->deferred;

			MemoryContextSwitchTo(oldcxt);

			return;
		}
		else
		{
			/* ----------
			 * ... inside of a transaction block
			 * ----------
			 */
			oldcxt = MemoryContextSwitchTo((MemoryContext)info->deftrig_cxt);

			/* ----------
			 * Drop all information about individual trigger states per
			 * transaction.
			 * ----------
			 */
			l = info->deftrig_trigstates;
			while (l != NIL)
			{
				lnext = lnext(l);
				pfree(lfirst(l));
				pfree(l);
				l = lnext;
			}
			info->deftrig_trigstates = NIL;

			/* ----------
			 * Set the per transaction ALL state to known.
			 * ----------
			 */
			info->deftrig_all_isset = true;
			info->deftrig_all_isdeferred = stmt->deferred;

			MemoryContextSwitchTo(oldcxt);

			return;
		}
	}

	/* ----------
	 * Handle SET CONSTRAINTS constraint-name [, ...]
	 * First lookup all trigger Oid's for the constraint names.
	 * ----------
	 */
	tgrel = heap_openr(TriggerRelationName, AccessShareLock);
	hasindex = (tgrel->rd_rel->relhasindex && !IsIgnoringSystemIndexes());
	if (hasindex)
		irel = index_openr(TriggerConstrNameIndex);

	foreach(l, stmt->constraints)
	{
		ScanKeyData skey;
		HeapTupleData tuple;
		IndexScanDesc sd = (IndexScanDesc) NULL;
		HeapScanDesc tgscan = (HeapScanDesc) NULL;
		HeapTuple	htup;
		Buffer		buffer;
		Form_pg_trigger pg_trigger;
		Oid			constr_oid;

		/* ----------
		 * Check that only named constraints are set explicitly
		 * ----------
		 */
		if (strcmp((char *) lfirst(l), "") == 0)
			elog(ERROR, "unnamed constraints cannot be set explicitly");

		/* ----------
		 * Setup to scan pg_trigger by tgconstrname ...
		 * ----------
		 */
		ScanKeyEntryInitialize(&skey,
							   (bits16) 0x0,
							   (AttrNumber) 1,
							   (RegProcedure) F_NAMEEQ,
							   PointerGetDatum((char *) lfirst(l)));

		if (hasindex)
			sd = index_beginscan(irel, false, 1, &skey);
		else
			tgscan = heap_beginscan(tgrel, SnapshotNow, 1, &skey);

		/* ----------
		 * ... and search for the constraint trigger row
		 * ----------
		 */
		found = false;
		for (;;)
		{
			if (hasindex)
			{
				if ( index_getnext(sd, ForwardScanDirection) ) break;

				tuple.t_self = sd->xs_ctup.t_self;
				heap_fetch(tgrel, SnapshotNow, &tuple, &buffer);
				if (!tuple.t_data)
					continue;
				htup = &tuple;
			}
			else
			{
				htup = heap_getnext(tgscan);
				if (!HeapTupleIsValid(htup))
					break;
			}

			/* ----------
			 * If we found some, check that they fit the deferrability
			 * but skip ON <event> RESTRICT ones, since they are silently
			 * never deferrable.
			 * ----------
			 */
			pg_trigger = (Form_pg_trigger) GETSTRUCT(htup);
			if (stmt->deferred && !pg_trigger->tgdeferrable &&
				pg_trigger->tgfoid != F_RI_FKEY_RESTRICT_UPD &&
				pg_trigger->tgfoid != F_RI_FKEY_RESTRICT_DEL)
				elog(ERROR, "Constraint '%s' is not deferrable",
					 (char *) lfirst(l));

			constr_oid = htup->t_data->t_oid;
			loid = lappend(loid, (Node *) constr_oid);
			found = true;

			if (hasindex)
				ReleaseBuffer(tgrel, buffer);
		}

		/* ----------
		 * Not found ?
		 * ----------
		 */
		if (!found)
			elog(ERROR, "Constraint '%s' does not exist", (char *) lfirst(l));

		if (hasindex)
			index_endscan(sd);
		else
			heap_endscan(tgscan);
	}
	if (hasindex)
		index_close(irel);
	heap_close(tgrel, AccessShareLock);


	if (!IsTransactionBlock())
	{
		/* ----------
		 * Outside of a transaction block set the trigger
		 * states of individual triggers on session level.
		 * ----------
		 */
		oldcxt = MemoryContextSwitchTo((MemoryContext) info->deftrig_global_cxt);

		foreach(l, loid)
		{
			found = false;
			foreach(ls, info->deftrig_dfl_trigstates)
			{
				state = (DeferredTriggerStatus) lfirst(ls);
				if (state->dts_tgoid == (Oid) lfirst(l))
				{
					state->dts_tgisdeferred = stmt->deferred;
					found = true;
					break;
				}
			}
			if (!found)
			{
				state = (DeferredTriggerStatus)
					palloc(sizeof(DeferredTriggerStatusData));
				state->dts_tgoid = (Oid) lfirst(l);
				state->dts_tgisdeferred = stmt->deferred;

				info->deftrig_dfl_trigstates =
					lappend(info->deftrig_dfl_trigstates, state);
			}
		}

		MemoryContextSwitchTo(oldcxt);

		return;
	}
	else
	{
		/* ----------
		 * Inside of a transaction block set the trigger
		 * states of individual triggers on transaction level.
		 * ----------
		 */
		oldcxt = MemoryContextSwitchTo((MemoryContext)info->deftrig_cxt);

		foreach(l, loid)
		{
			found = false;
			foreach(ls, info->deftrig_trigstates)
			{
				state = (DeferredTriggerStatus) lfirst(ls);
				if (state->dts_tgoid == (Oid) lfirst(l))
				{
					state->dts_tgisdeferred = stmt->deferred;
					found = true;
					break;
				}
			}
			if (!found)
			{
				state = (DeferredTriggerStatus)
					palloc(sizeof(DeferredTriggerStatusData));
				state->dts_tgoid = (Oid) lfirst(l);
				state->dts_tgisdeferred = stmt->deferred;

				info->deftrig_trigstates =
					lappend(info->deftrig_trigstates, state);
			}
		}

		MemoryContextSwitchTo(oldcxt);

		return;
	}
}


/* ----------
 * DeferredTriggerSaveEvent()
 *
 *	Called by ExecAR...Triggers() to add the event to the queue.
 * ----------
 */
void
DeferredTriggerSaveEvent(Relation rel, int event,
						 HeapTuple oldtup, HeapTuple newtup)
{
	MemoryContext oldcxt;
	DeferredTriggerEvent new_event;
	DeferredTriggerEvent prev_event;
	int			new_size;
	int			i;
	int			ntriggers;
	Trigger   **triggers;
	ItemPointerData oldctid;
	ItemPointerData newctid;
	TriggerData SaveTriggerData;
	TransactionId   xid;
        TriggerInfo*  info = GetTriggerInfo();

	if (info->deftrig_cxt == NULL)
		elog(ERROR,
			 "DeferredTriggerSaveEvent() called outside of transaction");

	/* ----------
	 * Check if we're interested in this row at all
	 * ----------
	 */
	if (rel->trigdesc->n_after_row[TRIGGER_EVENT_INSERT] == 0 &&
		rel->trigdesc->n_after_row[TRIGGER_EVENT_UPDATE] == 0 &&
		rel->trigdesc->n_after_row[TRIGGER_EVENT_DELETE] == 0 &&
		rel->trigdesc->n_before_row[TRIGGER_EVENT_INSERT] == 0 &&
		rel->trigdesc->n_before_row[TRIGGER_EVENT_UPDATE] == 0 &&
		rel->trigdesc->n_before_row[TRIGGER_EVENT_DELETE] == 0)
		return;

	/* ----------
	 * Get the CTID's of OLD and NEW
	 * ----------
	 */
	if (oldtup != NULL)
		ItemPointerCopy(&(oldtup->t_self), &(oldctid));
	else
		ItemPointerSetInvalid(&(oldctid));
	if (newtup != NULL)
		ItemPointerCopy(&(newtup->t_self), &(newctid));
	else
		ItemPointerSetInvalid(&(newctid));

	/* ----------
	 * Create a new event
	 * ----------
	 */
	oldcxt = MemoryContextSwitchTo((MemoryContext) info->deftrig_cxt);

	ntriggers = rel->trigdesc->n_after_row[event];
	triggers = rel->trigdesc->tg_after_row[event];

	new_size = sizeof(DeferredTriggerEventData) +
		ntriggers * sizeof(DeferredTriggerEventItem);

	new_event = (DeferredTriggerEvent) palloc(new_size);
	new_event->dte_event = event & TRIGGER_EVENT_OPMASK;
	new_event->dte_relid = rel->rd_id;
	ItemPointerCopy(&oldctid, &(new_event->dte_oldctid));
	ItemPointerCopy(&newctid, &(new_event->dte_newctid));
	new_event->dte_n_items = ntriggers;
	new_event->dte_item[ntriggers].dti_state = new_size;
	for (i = 0; i < ntriggers; i++)
	{
		new_event->dte_item[i].dti_tgoid = triggers[i]->tgoid;
		new_event->dte_item[i].dti_state =
			((triggers[i]->tgdeferrable) ?
			 TRIGGER_DEFERRED_DEFERRABLE : 0) |
			((triggers[i]->tginitdeferred) ?
			 TRIGGER_DEFERRED_INITDEFERRED : 0) |
			((rel->trigdesc->n_before_row[event] > 0) ?
			 TRIGGER_DEFERRED_HAS_BEFORE : 0);
	}
	MemoryContextSwitchTo(oldcxt);

	switch (event & TRIGGER_EVENT_OPMASK)
	{
		case TRIGGER_EVENT_INSERT:
			new_event->dte_event |= TRIGGER_DEFERRED_ROW_INSERTED;
			new_event->dte_event |= TRIGGER_DEFERRED_KEY_CHANGED;
			break;

		case TRIGGER_EVENT_UPDATE:
			/* ----------
			 * On UPDATE check if the tuple updated has been inserted
			 * or a foreign referenced key value that's changing now
			 * has been updated once before in this transaction.
			 * ----------
			 */
			xid = GetCurrentTransactionId();
			if (oldtup->t_data->t_xmin != xid)
				prev_event = NULL;
			else
				prev_event =
					deferredTriggerGetPreviousEvent(rel->rd_id, &oldctid);

			/* ----------
			 * Now check if one of the referenced keys is changed.
			 * ----------
			 */
			for (i = 0; i < ntriggers; i++)
			{
				bool		is_ri_trigger;
				bool		key_unchanged;

				/* ----------
				 * We are interested in RI_FKEY triggers only.
				 * ----------
				 */
				switch (triggers[i]->tgfoid)
				{
					case F_RI_FKEY_NOACTION_UPD:
					case F_RI_FKEY_CASCADE_UPD:
					case F_RI_FKEY_RESTRICT_UPD:
					case F_RI_FKEY_SETNULL_UPD:
					case F_RI_FKEY_SETDEFAULT_UPD:
						is_ri_trigger = true;
						break;

					default:
						is_ri_trigger = false;
						break;
				}
				if (!is_ri_trigger)
					continue;

				SaveTriggerData.tg_event = TRIGGER_EVENT_UPDATE;
				SaveTriggerData.tg_relation = rel;
				SaveTriggerData.tg_trigtuple = oldtup;
				SaveTriggerData.tg_newtuple = newtup;
				SaveTriggerData.tg_trigger = triggers[i];

				SetTriggerData(&SaveTriggerData);
				key_unchanged = RI_FKey_keyequal_upd();
				SetTriggerData( NULL);

				if (key_unchanged)
				{
					/* ----------
					 * The key hasn't changed, so no need later to invoke
					 * the trigger at all. But remember other states from
					 * the possible earlier event.
					 * ----------
					 */
					new_event->dte_item[i].dti_state |= TRIGGER_DEFERRED_DONE;

					if (prev_event)
					{
						if (prev_event->dte_event &
							TRIGGER_DEFERRED_ROW_INSERTED)
						{
							/* ----------
							 * This is a row inserted during our transaction.
							 * So any key value is considered changed.
							 * ----------
							 */
							new_event->dte_event |=
								TRIGGER_DEFERRED_ROW_INSERTED;
							new_event->dte_event |=
								TRIGGER_DEFERRED_KEY_CHANGED;
							new_event->dte_item[i].dti_state |=
								TRIGGER_DEFERRED_KEY_CHANGED;
						}
						else
						{
							/* ----------
							 * This is a row, previously updated. So
							 * if this key has been changed before, we
							 * still remember that it happened.
							 * ----------
							 */
							if (prev_event->dte_item[i].dti_state &
								TRIGGER_DEFERRED_KEY_CHANGED)
							{
								new_event->dte_item[i].dti_state |=
									TRIGGER_DEFERRED_KEY_CHANGED;
								new_event->dte_event |=
									TRIGGER_DEFERRED_KEY_CHANGED;
							}
						}
					}
				}
				else
				{
					/* ----------
					 * Bomb out if this key has been changed before.
					 * Otherwise remember that we do so.
					 * ----------
					 */
					if (prev_event)
					{
						if (prev_event->dte_event &
							TRIGGER_DEFERRED_ROW_INSERTED)
							elog(ERROR, "triggered data change violation "
								 "on relation \"%s\"",
								 nameout(&(rel->rd_rel->relname)));

						if (prev_event->dte_item[i].dti_state &
							TRIGGER_DEFERRED_KEY_CHANGED)
							elog(ERROR, "triggered data change violation "
								 "on relation \"%s\"",
								 nameout(&(rel->rd_rel->relname)));
					}

					/* ----------
					 * This is the first change to this key, so let
					 * it happen.
					 * ----------
					 */
					new_event->dte_item[i].dti_state |=
						TRIGGER_DEFERRED_KEY_CHANGED;
					new_event->dte_event |= TRIGGER_DEFERRED_KEY_CHANGED;
				}
			}

			break;

		case TRIGGER_EVENT_DELETE:
			/* ----------
			 * On DELETE check if the tuple deleted has been inserted
			 * or a possibly referenced key value has changed in this
			 * transaction.
			 * ----------
			 */
			 
			 xid = GetCurrentTransactionId();
			if (oldtup->t_data->t_xmin != xid)
				break;

			/* ----------
			 * Look at the previous event to the same tuple.
			 * ----------
			 */
			prev_event = deferredTriggerGetPreviousEvent(rel->rd_id, &oldctid);
			if (prev_event->dte_event & TRIGGER_DEFERRED_KEY_CHANGED)
				elog(ERROR, "triggered data change violation "
					 "on relation \"%s\"",
					 nameout(&(rel->rd_rel->relname)));

			break;
	}

	/* ----------
	 * Anything's fine up to here. Add the new event to the queue.
	 * ----------
	 */
	oldcxt = MemoryContextSwitchTo((MemoryContext) info->deftrig_cxt);
	deferredTriggerAddEvent(new_event);
	MemoryContextSwitchTo(oldcxt);

	return;
}

TriggerData*
GetTriggerData(void) 
{
    TriggerInfo* trigger = GetTriggerInfo();
    return trigger->CurrentTriggerData;
}

void
SetTriggerData(TriggerData* trigger) 
{
    TriggerInfo* tinfo = GetTriggerInfo();
    tinfo->CurrentTriggerData = trigger;
}
