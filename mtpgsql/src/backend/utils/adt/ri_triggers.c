/* ----------
 * ri_triggers.c
 *
 *	Generic trigger procedures for referential integrity constraint
 *	checks.
 *
 *	1999 Jan Wieck
 *
 *
 *
 * ----------
 */


/* ----------
 * Internal TODO:
 *
 *		Add MATCH PARTIAL logic.
 * ----------
 */


#include "postgres.h"

#include "fmgr.h"
#include "access/heapam.h"
#include "catalog/pg_operator.h"
#include "catalog/catname.h"
#include "commands/trigger.h"
#include "executor/spi.h"
#include "executor/spi_priv.h"
#include "utils/builtins.h"
#include "utils/memutils.h"
#include "utils/syscache.h"
#include "lib/hasht.h"


/* ----------
 * Local definitions
 * ----------
 */
#define RI_CONSTRAINT_NAME_ARGNO		0
#define RI_FK_RELNAME_ARGNO				1
#define RI_PK_RELNAME_ARGNO				2
#define RI_MATCH_TYPE_ARGNO				3
#define RI_FIRST_ATTNAME_ARGNO			4

#define RI_MAX_NUMKEYS					16
#define RI_MAX_ARGUMENTS		(RI_FIRST_ATTNAME_ARGNO + (RI_MAX_NUMKEYS * 2))
#define RI_KEYPAIR_FK_IDX				0
#define RI_KEYPAIR_PK_IDX				1

#define RI_INIT_QUERYHASHSIZE			128
#define RI_INIT_OPREQHASHSIZE			128

#define RI_MATCH_TYPE_UNSPECIFIED		0
#define RI_MATCH_TYPE_FULL				1
#define RI_MATCH_TYPE_PARTIAL			2

#define RI_KEYS_ALL_NULL				0
#define RI_KEYS_SOME_NULL				1
#define RI_KEYS_NONE_NULL				2


#define RI_PLAN_CHECK_LOOKUPPK_NOCOLS	1
#define RI_PLAN_CHECK_LOOKUPPK			2
#define RI_PLAN_CASCADE_DEL_DODELETE	1
#define RI_PLAN_CASCADE_UPD_DOUPDATE	1
#define RI_PLAN_NOACTION_DEL_CHECKREF	1
#define RI_PLAN_NOACTION_UPD_CHECKREF	1
#define RI_PLAN_RESTRICT_DEL_CHECKREF	1
#define RI_PLAN_RESTRICT_UPD_CHECKREF	1
#define RI_PLAN_SETNULL_DEL_DOUPDATE	1
#define RI_PLAN_SETNULL_UPD_DOUPDATE	1


/* ----------
 * RI_QueryKey
 *
 *	The key identifying a prepared SPI plan in our private hashtable
 * ----------
 */
typedef struct RI_QueryKey
{
	int32		constr_type;
	Oid			constr_id;
	int32		constr_queryno;
	Oid			fk_relid;
	Oid			pk_relid;
	int32		nkeypairs;
	int16		keypair[RI_MAX_NUMKEYS][2];
} RI_QueryKey;


/* ----------
 * RI_QueryHashEntry
 * ----------
 */
typedef struct RI_QueryHashEntry
{
	RI_QueryKey key;
	void	   *plan;
} RI_QueryHashEntry;


typedef struct RI_OpreqHashEntry
{
	Oid			typeid;
	Oid			oprfnid;
	FmgrInfo	oprfmgrinfo;
} RI_OpreqHashEntry;



/* ----------
 * Local data
 * ----------
 */
static HTAB *ri_query_cache = (HTAB *) NULL;
static HTAB *ri_opreq_cache = (HTAB *) NULL;


/* ----------
 * Local function prototypes
 * ----------
 */
static int	ri_DetermineMatchType(char *str);
static int ri_NullCheck(Relation rel, HeapTuple tup,
			 RI_QueryKey *key, int pairidx);
static void ri_BuildQueryKeyFull(RI_QueryKey *key, Oid constr_id,
					 int32 constr_queryno,
					 Relation fk_rel, Relation pk_rel,
					 int argc, char **argv);
static bool ri_KeysEqual(Relation rel, HeapTuple oldtup, HeapTuple newtup,
			 RI_QueryKey *key, int pairidx);
static bool ri_AllKeysUnequal(Relation rel, HeapTuple oldtup, HeapTuple newtup,
				  RI_QueryKey *key, int pairidx);
static bool ri_OneKeyEqual(Relation rel, int column, HeapTuple oldtup,
			   HeapTuple newtup, RI_QueryKey *key, int pairidx);
static bool ri_AttributesEqual(Oid typeid, Datum oldvalue, Datum newvalue);

static void ri_InitHashTables(void);
static void *ri_FetchPreparedPlan(RI_QueryKey *key);
static void ri_HashPreparedPlan(RI_QueryKey *key, void *plan);



/* ----------
 * RI_FKey_check -
 *
 *	Check foreign key existance (combined for INSERT and UPDATE).
 * ----------
 */
static HeapTuple
RI_FKey_check(FmgrInfo *proinfo)
{
	TriggerData *trigdata;
	int			tgnargs;
	char	  **tgargs;
	Relation	fk_rel;
	Relation	pk_rel;
	HeapTuple	new_row;
	/*
	HeapTuple	old_row;
	*/
	RI_QueryKey qkey;
	void	   *qplan;
	Datum		check_values[RI_MAX_NUMKEYS];
	char		check_nulls[RI_MAX_NUMKEYS + 1];
	bool		isnull;
	int			i;
	int			match_type;

	trigdata = GetTriggerData();
	SetTriggerData(NULL);
	GetSnapshotHolder()->ReferentialIntegritySnapshotOverride = true;

	/* ----------
	 * Check that this is a valid trigger call on the right time and event.
	 * ----------
	 */
	if (trigdata == NULL)
		elog(ERROR, "RI_FKey_check() not fired by trigger manager");
	if (!TRIGGER_FIRED_AFTER(trigdata->tg_event) ||
		!TRIGGER_FIRED_FOR_ROW(trigdata->tg_event))
		elog(ERROR, "RI_FKey_check() must be fired AFTER ROW");
	if (!TRIGGER_FIRED_BY_INSERT(trigdata->tg_event) &&
		!TRIGGER_FIRED_BY_UPDATE(trigdata->tg_event))
		elog(ERROR, "RI_FKey_check() must be fired for INSERT or UPDATE");

	/* ----------
	 * Check for the correct # of call arguments
	 * ----------
	 */
	tgnargs = trigdata->tg_trigger->tgnargs;
	tgargs = trigdata->tg_trigger->tgargs;
	if (tgnargs < 4 || (tgnargs % 2) != 0)
		elog(ERROR, "wrong # of arguments in call to RI_FKey_check()");
	if (tgnargs > RI_MAX_ARGUMENTS)
		elog(ERROR, "too many keys (%d max) in call to RI_FKey_check()",
			 RI_MAX_NUMKEYS);

	/* ----------
	 * Get the relation descriptors of the FK and PK tables and
	 * the new tuple.
	 * ----------
	 */
	fk_rel = trigdata->tg_relation;
	pk_rel = heap_openr(tgargs[RI_PK_RELNAME_ARGNO], NoLock);
	if (TRIGGER_FIRED_BY_UPDATE(trigdata->tg_event))
	{
		/*
		old_row = trigdata->tg_trigtuple;
		*/
		new_row = trigdata->tg_newtuple;
	}
	else
	{
		/*
		old_row = NULL;
		*/
		new_row = trigdata->tg_trigtuple;
	}

	/* ----------
	 * SQL3 11.9 <referential constraint definition>
	 *	Gereral rules 2) a):
	 *		If Rf and Rt are empty (no columns to compare given)
	 *		constraint is true if 0 < (SELECT COUNT(*) FROM T)
	 *
	 *	Note: The special case that no columns are given cannot
	 *		occur up to now in Postgres, it's just there for
	 *		future enhancements.
	 * ----------
	 */
	if (tgnargs == 4)
	{
		ri_BuildQueryKeyFull(&qkey, trigdata->tg_trigger->tgoid,
							 RI_PLAN_CHECK_LOOKUPPK_NOCOLS,
							 fk_rel, pk_rel,
							 tgnargs, tgargs);

		if ((qplan = ri_FetchPreparedPlan(&qkey)) == NULL)
		{
			char		querystr[8192];

			/* ----------
			 * The query string built is
			 *	SELECT oid FROM <pktable>
			 * ----------
			 */
			sprintf(querystr, "SELECT oid FROM \"%s\" FOR UPDATE OF \"%s\"",
					tgargs[RI_PK_RELNAME_ARGNO],
					tgargs[RI_PK_RELNAME_ARGNO]);

			/* ----------
			 * Prepare, save and remember the new plan.
			 * ----------
			 */
			qplan = SPI_prepare(querystr, 0, NULL);
			qplan = SPI_saveplan(qplan);
			ri_HashPreparedPlan(&qkey, qplan);
		}
		heap_close(pk_rel, NoLock);

		/* ----------
		 * Execute the plan
		 * ----------
		 */
		if (SPI_connect() != SPI_OK_CONNECT)
			elog(NOTICE, "SPI_connect() failed in RI_FKey_check()");

		if (SPI_execp(qplan, check_values, check_nulls, 1) != SPI_OK_SELECT)
			elog(ERROR, "SPI_execp() failed in RI_FKey_check()");

		if (SPI_GetInfo()->SPI_processed == 0)
			elog(ERROR, "%s referential integrity violation - "
				 "no rows found in %s",
				 tgargs[RI_CONSTRAINT_NAME_ARGNO],
				 tgargs[RI_PK_RELNAME_ARGNO]);

		if (SPI_finish() != SPI_OK_FINISH)
			elog(NOTICE, "SPI_finish() failed in RI_FKey_check()");

		return NULL;

	}

	match_type = ri_DetermineMatchType(tgargs[RI_MATCH_TYPE_ARGNO]);

	if (match_type == RI_MATCH_TYPE_PARTIAL)
	{
		elog(ERROR, "MATCH PARTIAL not yet supported");
		return NULL;
	}

	ri_BuildQueryKeyFull(&qkey, trigdata->tg_trigger->tgoid,
						 RI_PLAN_CHECK_LOOKUPPK, fk_rel, pk_rel,
						 tgnargs, tgargs);

	switch (ri_NullCheck(fk_rel, new_row, &qkey, RI_KEYPAIR_FK_IDX))
	{
		case RI_KEYS_ALL_NULL:
			/* ----------
			 * No check - if NULLs are allowed at all is
			 * already checked by NOT NULL constraint.
			 *
			 * This is true for MATCH FULL, MATCH PARTIAL, and
			 * MATCH <unspecified>
			 * ----------
			 */
			heap_close(pk_rel, NoLock);
			return NULL;

		case RI_KEYS_SOME_NULL:
			/* ----------
			 * This is the only case that differs between the
			 * three kinds of MATCH.
			 * ----------
			 */
			switch (match_type)
			{
				case RI_MATCH_TYPE_FULL:
					/* ----------
					 * Not allowed - MATCH FULL says either all or none
					 * of the attributes can be NULLs
					 * ----------
					 */
					elog(ERROR, "%s referential integrity violation - "
						 "MATCH FULL doesn't allow mixing of NULL "
						 "and NON-NULL key values",
						 tgargs[RI_CONSTRAINT_NAME_ARGNO]);
					heap_close(pk_rel, NoLock);
					return NULL;

				case RI_MATCH_TYPE_UNSPECIFIED:
					/* ----------
					 * MATCH <unspecified> - if ANY column is null, we
					 * have a match.
					 * ----------
					 */
					heap_close(pk_rel, NoLock);
					return NULL;

				case RI_MATCH_TYPE_PARTIAL:
					/* ----------
					 * MATCH PARTIAL - all non-null columns must match.
					 * (not implemented, can be done by modifying the query
					 * below to only include non-null columns, or by
					 * writing a special version here)
					 * ----------
					 */
					elog(ERROR, "MATCH PARTIAL not yet implemented");
					heap_close(pk_rel, NoLock);
					return NULL;
			}

		case RI_KEYS_NONE_NULL:
			/* ----------
			 * Have a full qualified key - continue below for all three
			 * kinds of MATCH.
			 * ----------
			 */
			break;
	}
	heap_close(pk_rel, NoLock);

	/* ----------
	 * Note:
	 * We cannot avoid the check on UPDATE, even if old and new
	 * key are the same. Otherwise, someone could DELETE the PK
	 * that consists of the DEFAULT values, and if there are any
	 * references, a ON DELETE SET DEFAULT action would update
	 * the references to exactly these values but we wouldn't see
	 * that weired case (this is the only place to see it).
	 * ----------
	 */
	if (SPI_connect() != SPI_OK_CONNECT)
		elog(NOTICE, "SPI_connect() failed in RI_FKey_check()");

	/* ----------
	 * Fetch or prepare a saved plan for the real check
	 * ----------
	 */
	if ((qplan = ri_FetchPreparedPlan(&qkey)) == NULL)
	{
		char		buf[256];
		char		querystr[8192];
		char	   *querysep;
		Oid			queryoids[RI_MAX_NUMKEYS];

		/* ----------
		 * The query string built is
		 *	SELECT oid FROM <pktable> WHERE pkatt1 = $1 [AND ...]
		 * The type id's for the $ parameters are those of the
		 * corresponding FK attributes. Thus, SPI_prepare could
		 * eventually fail if the parser cannot identify some way
		 * how to compare these two types by '='.
		 * ----------
		 */
		sprintf(querystr, "SELECT oid FROM \"%s\"",
				tgargs[RI_PK_RELNAME_ARGNO]);
		querysep = "WHERE";
		for (i = 0; i < qkey.nkeypairs; i++)
		{
			sprintf(buf, " %s \"%s\" = $%d", querysep,
					tgargs[5 + i * 2], i + 1);
			strcat(querystr, buf);
			querysep = "AND";
			queryoids[i] = SPI_gettypeid(fk_rel->rd_att,
									 qkey.keypair[i][RI_KEYPAIR_FK_IDX]);
		}
		sprintf(buf, " FOR UPDATE OF \"%s\"",
				tgargs[RI_PK_RELNAME_ARGNO]);
		strcat(querystr, buf);

		/* ----------
		 * Prepare, save and remember the new plan.
		 * ----------
		 */
		qplan = SPI_prepare(querystr, qkey.nkeypairs, queryoids);
		qplan = SPI_saveplan(qplan);
		ri_HashPreparedPlan(&qkey, qplan);
	}

	/* ----------
	 * We have a plan now. Build up the arguments for SPI_execp()
	 * from the key values in the new FK tuple.
	 * ----------
	 */
	for (i = 0; i < qkey.nkeypairs; i++)
	{
		/* ----------
		 * We can implement MATCH PARTIAL by excluding this column from
		 * the query if it is null.  Simple!  Unfortunately, the
		 * referential actions aren't so I've not bothered to do so
		 * for the moment.
		 * ----------
		 */

		check_values[i] = SPI_getbinval(new_row,
										fk_rel->rd_att,
									  qkey.keypair[i][RI_KEYPAIR_FK_IDX],
										&isnull);
		if (isnull)
			check_nulls[i] = 'n';
		else
			check_nulls[i] = ' ';
	}
	check_nulls[i] = '\0';

	/* ----------
	 * Now check that foreign key exists in PK table
	 * ----------
	 */
	if (SPI_execp(qplan, check_values, check_nulls, 1) != SPI_OK_SELECT)
		elog(ERROR, "SPI_execp() failed in RI_FKey_check()");

	if (SPI_GetInfo()->SPI_processed == 0)
		elog(ERROR, "%s referential integrity violation - "
			 "key referenced from %s not found in %s",
			 tgargs[RI_CONSTRAINT_NAME_ARGNO],
			 tgargs[RI_FK_RELNAME_ARGNO],
			 tgargs[RI_PK_RELNAME_ARGNO]);

	if (SPI_finish() != SPI_OK_FINISH)
		elog(NOTICE, "SPI_finish() failed in RI_FKey_check()");

	return NULL;

	/* ----------
	 * Never reached
	 * ----------
	 */
	elog(ERROR, "internal error #1 in ri_triggers.c");
	return NULL;
}


/* ----------
 * RI_FKey_check_ins -
 *
 *	Check foreign key existance at insert event on FK table.
 * ----------
 */
HeapTuple
RI_FKey_check_ins(FmgrInfo *proinfo)
{
	return RI_FKey_check(proinfo);
}


/* ----------
 * RI_FKey_check_upd -
 *
 *	Check foreign key existance at update event on FK table.
 * ----------
 */
HeapTuple
RI_FKey_check_upd(FmgrInfo *proinfo)
{
	return RI_FKey_check(proinfo);
}


/* ----------
 * RI_FKey_noaction_del -
 *
 *	Give an error and roll back the current transaction if the
 *	delete has resulted in a violation of the given referential
 *	integrity constraint.
 * ----------
 */
HeapTuple
RI_FKey_noaction_del(FmgrInfo *proinfo)
{
	TriggerData *trigdata;
	int			tgnargs;
	char	  **tgargs;
	Relation	fk_rel;
	Relation	pk_rel;
	HeapTuple	old_row;
	RI_QueryKey qkey;
	void	   *qplan;
	Datum		del_values[RI_MAX_NUMKEYS];
	char		del_nulls[RI_MAX_NUMKEYS + 1];
	bool		isnull;
	int			i;

	trigdata = GetTriggerData();
	SetTriggerData(NULL);
	GetSnapshotHolder()->ReferentialIntegritySnapshotOverride = true;

	/* ----------
	 * Check that this is a valid trigger call on the right time and event.
	 * ----------
	 */
	if (trigdata == NULL)
		elog(ERROR, "RI_FKey_noaction_del() not fired by trigger manager");
	if (!TRIGGER_FIRED_AFTER(trigdata->tg_event) ||
		!TRIGGER_FIRED_FOR_ROW(trigdata->tg_event))
		elog(ERROR, "RI_FKey_noaction_del() must be fired AFTER ROW");
	if (!TRIGGER_FIRED_BY_DELETE(trigdata->tg_event))
		elog(ERROR, "RI_FKey_noaction_del() must be fired for DELETE");

	/* ----------
	 * Check for the correct # of call arguments
	 * ----------
	 */
	tgnargs = trigdata->tg_trigger->tgnargs;
	tgargs = trigdata->tg_trigger->tgargs;
	if (tgnargs < 4 || (tgnargs % 2) != 0)
		elog(ERROR, "wrong # of arguments in call to RI_FKey_noaction_del()");
	if (tgnargs > RI_MAX_ARGUMENTS)
		elog(ERROR, "too many keys (%d max) in call to RI_FKey_noaction_del()",
			 RI_MAX_NUMKEYS);

	/* ----------
	 * Nothing to do if no column names to compare given
	 * ----------
	 */
	if (tgnargs == 4)
		return NULL;

	/* ----------
	 * Get the relation descriptors of the FK and PK tables and
	 * the old tuple.
	 * ----------
	 */
	fk_rel = heap_openr(tgargs[RI_FK_RELNAME_ARGNO], NoLock);
	pk_rel = trigdata->tg_relation;
	old_row = trigdata->tg_trigtuple;

	switch (ri_DetermineMatchType(tgargs[RI_MATCH_TYPE_ARGNO]))
	{
			/* ----------
			 * SQL3 11.9 <referential constraint definition>
			 *	Gereral rules 6) a) iv):
			 *		MATCH <unspecified> or MATCH FULL
			 *			... ON DELETE CASCADE
			 * ----------
			 */
		case RI_MATCH_TYPE_UNSPECIFIED:
		case RI_MATCH_TYPE_FULL:
			ri_BuildQueryKeyFull(&qkey, trigdata->tg_trigger->tgoid,
								 RI_PLAN_NOACTION_DEL_CHECKREF,
								 fk_rel, pk_rel,
								 tgnargs, tgargs);

			switch (ri_NullCheck(pk_rel, old_row, &qkey, RI_KEYPAIR_PK_IDX))
			{
				case RI_KEYS_ALL_NULL:
				case RI_KEYS_SOME_NULL:
					/* ----------
					 * No check - MATCH FULL means there cannot be any
					 * reference to old key if it contains NULL
					 * ----------
					 */
					heap_close(fk_rel, NoLock);
					return NULL;

				case RI_KEYS_NONE_NULL:
					/* ----------
					 * Have a full qualified key - continue below
					 * ----------
					 */
					break;
			}
			heap_close(fk_rel, NoLock);

			if (SPI_connect() != SPI_OK_CONNECT)
				elog(NOTICE, "SPI_connect() failed in RI_FKey_noaction_del()");

			/* ----------
			 * Fetch or prepare a saved plan for the restrict delete
			 * lookup if foreign references exist
			 * ----------
			 */
			if ((qplan = ri_FetchPreparedPlan(&qkey)) == NULL)
			{
				char		buf[256];
				char		querystr[8192];
				char	   *querysep;
				Oid			queryoids[RI_MAX_NUMKEYS];

				/* ----------
				 * The query string built is
				 *	SELECT oid FROM <fktable> WHERE fkatt1 = $1 [AND ...]
				 * The type id's for the $ parameters are those of the
				 * corresponding PK attributes. Thus, SPI_prepare could
				 * eventually fail if the parser cannot identify some way
				 * how to compare these two types by '='.
				 * ----------
				 */
				sprintf(querystr, "SELECT oid FROM \"%s\"",
						tgargs[RI_FK_RELNAME_ARGNO]);
				querysep = "WHERE";
				for (i = 0; i < qkey.nkeypairs; i++)
				{
					sprintf(buf, " %s \"%s\" = $%d", querysep,
							tgargs[4 + i * 2], i + 1);
					strcat(querystr, buf);
					querysep = "AND";
					queryoids[i] = SPI_gettypeid(pk_rel->rd_att,
									 qkey.keypair[i][RI_KEYPAIR_PK_IDX]);
				}
				sprintf(buf, " FOR UPDATE OF \"%s\"",
						tgargs[RI_FK_RELNAME_ARGNO]);
				strcat(querystr, buf);

				/* ----------
				 * Prepare, save and remember the new plan.
				 * ----------
				 */
				qplan = SPI_prepare(querystr, qkey.nkeypairs, queryoids);
				qplan = SPI_saveplan(qplan);
				ri_HashPreparedPlan(&qkey, qplan);
			}

			/* ----------
			 * We have a plan now. Build up the arguments for SPI_execp()
			 * from the key values in the deleted PK tuple.
			 * ----------
			 */
			for (i = 0; i < qkey.nkeypairs; i++)
			{
				del_values[i] = SPI_getbinval(old_row,
											  pk_rel->rd_att,
									  qkey.keypair[i][RI_KEYPAIR_PK_IDX],
											  &isnull);
				if (isnull)
					del_nulls[i] = 'n';
				else
					del_nulls[i] = ' ';
			}
			del_nulls[i] = '\0';

			/* ----------
			 * Now check for existing references
			 * ----------
			 */
			if (SPI_execp(qplan, del_values, del_nulls, 1) != SPI_OK_SELECT)
				elog(ERROR, "SPI_execp() failed in RI_FKey_noaction_del()");

			if (SPI_GetInfo()->SPI_processed > 0)
				elog(ERROR, "%s referential integrity violation - "
					 "key in %s still referenced from %s",
					 tgargs[RI_CONSTRAINT_NAME_ARGNO],
					 tgargs[RI_PK_RELNAME_ARGNO],
					 tgargs[RI_FK_RELNAME_ARGNO]);

			if (SPI_finish() != SPI_OK_FINISH)
				elog(NOTICE, "SPI_finish() failed in RI_FKey_noaction_del()");

			return NULL;

			/* ----------
			 * Handle MATCH PARTIAL restrict delete.
			 * ----------
			 */
		case RI_MATCH_TYPE_PARTIAL:
			elog(ERROR, "MATCH PARTIAL not yet supported");
			return NULL;
	}

	/* ----------
	 * Never reached
	 * ----------
	 */
	elog(ERROR, "internal error #2 in ri_triggers.c");
	return NULL;
}


/* ----------
 * RI_FKey_noaction_upd -
 *
 *	Give an error and roll back the current transaction if the
 *	update has resulted in a violation of the given referential
 *	integrity constraint.
 * ----------
 */
HeapTuple
RI_FKey_noaction_upd(FmgrInfo *proinfo)
{
	TriggerData *trigdata;
	int			tgnargs;
	char	  **tgargs;
	Relation	fk_rel;
	Relation	pk_rel;
	HeapTuple	new_row;
	HeapTuple	old_row;
	RI_QueryKey qkey;
	void	   *qplan;
	Datum		upd_values[RI_MAX_NUMKEYS];
	char		upd_nulls[RI_MAX_NUMKEYS + 1];
	bool		isnull;
	int			i;

	trigdata = GetTriggerData();
	SetTriggerData(NULL);
	GetSnapshotHolder()->ReferentialIntegritySnapshotOverride = true;

	/* ----------
	 * Check that this is a valid trigger call on the right time and event.
	 * ----------
	 */
	if (trigdata == NULL)
		elog(ERROR, "RI_FKey_noaction_upd() not fired by trigger manager");
	if (!TRIGGER_FIRED_AFTER(trigdata->tg_event) ||
		!TRIGGER_FIRED_FOR_ROW(trigdata->tg_event))
		elog(ERROR, "RI_FKey_noaction_upd() must be fired AFTER ROW");
	if (!TRIGGER_FIRED_BY_UPDATE(trigdata->tg_event))
		elog(ERROR, "RI_FKey_noaction_upd() must be fired for UPDATE");

	/* ----------
	 * Check for the correct # of call arguments
	 * ----------
	 */
	tgnargs = trigdata->tg_trigger->tgnargs;
	tgargs = trigdata->tg_trigger->tgargs;
	if (tgnargs < 4 || (tgnargs % 2) != 0)
		elog(ERROR, "wrong # of arguments in call to RI_FKey_noaction_upd()");
	if (tgnargs > RI_MAX_ARGUMENTS)
		elog(ERROR, "too many keys (%d max) in call to RI_FKey_noaction_upd()",
			 RI_MAX_NUMKEYS);

	/* ----------
	 * Nothing to do if no column names to compare given
	 * ----------
	 */
	if (tgnargs == 4)
		return NULL;

	/* ----------
	 * Get the relation descriptors of the FK and PK tables and
	 * the new and old tuple.
	 * ----------
	 */
	fk_rel = heap_openr(tgargs[RI_FK_RELNAME_ARGNO], NoLock);
	pk_rel = trigdata->tg_relation;
	new_row = trigdata->tg_newtuple;
	old_row = trigdata->tg_trigtuple;

	switch (ri_DetermineMatchType(tgargs[RI_MATCH_TYPE_ARGNO]))
	{
			/* ----------
			 * SQL3 11.9 <referential constraint definition>
			 *	Gereral rules 6) a) iv):
			 *		MATCH <unspecified> or MATCH FULL
			 *			... ON DELETE CASCADE
			 * ----------
			 */
		case RI_MATCH_TYPE_UNSPECIFIED:
		case RI_MATCH_TYPE_FULL:
			ri_BuildQueryKeyFull(&qkey, trigdata->tg_trigger->tgoid,
								 RI_PLAN_NOACTION_UPD_CHECKREF,
								 fk_rel, pk_rel,
								 tgnargs, tgargs);

			switch (ri_NullCheck(pk_rel, old_row, &qkey, RI_KEYPAIR_PK_IDX))
			{
				case RI_KEYS_ALL_NULL:
				case RI_KEYS_SOME_NULL:
					/* ----------
					 * No check - MATCH FULL means there cannot be any
					 * reference to old key if it contains NULL
					 * ----------
					 */
					heap_close(fk_rel, NoLock);
					return NULL;

				case RI_KEYS_NONE_NULL:
					/* ----------
					 * Have a full qualified key - continue below
					 * ----------
					 */
					break;
			}
			heap_close(fk_rel, NoLock);

			/* ----------
			 * No need to check anything if old and new keys are equal
			 * ----------
			 */
			if (ri_KeysEqual(pk_rel, old_row, new_row, &qkey,
							 RI_KEYPAIR_PK_IDX))
				return NULL;

			if (SPI_connect() != SPI_OK_CONNECT)
				elog(NOTICE, "SPI_connect() failed in RI_FKey_noaction_upd()");

			/* ----------
			 * Fetch or prepare a saved plan for the noaction update
			 * lookup if foreign references exist
			 * ----------
			 */
			if ((qplan = ri_FetchPreparedPlan(&qkey)) == NULL)
			{
				char		buf[256];
				char		querystr[8192];
				char	   *querysep;
				Oid			queryoids[RI_MAX_NUMKEYS];

				/* ----------
				 * The query string built is
				 *	SELECT oid FROM <fktable> WHERE fkatt1 = $1 [AND ...]
				 * The type id's for the $ parameters are those of the
				 * corresponding PK attributes. Thus, SPI_prepare could
				 * eventually fail if the parser cannot identify some way
				 * how to compare these two types by '='.
				 * ----------
				 */
				sprintf(querystr, "SELECT oid FROM \"%s\"",
						tgargs[RI_FK_RELNAME_ARGNO]);
				querysep = "WHERE";
				for (i = 0; i < qkey.nkeypairs; i++)
				{
					sprintf(buf, " %s \"%s\" = $%d", querysep,
							tgargs[4 + i * 2], i + 1);
					strcat(querystr, buf);
					querysep = "AND";
					queryoids[i] = SPI_gettypeid(pk_rel->rd_att,
									 qkey.keypair[i][RI_KEYPAIR_PK_IDX]);
				}
				sprintf(buf, " FOR UPDATE OF \"%s\"",
						tgargs[RI_FK_RELNAME_ARGNO]);
				strcat(querystr, buf);

				/* ----------
				 * Prepare, save and remember the new plan.
				 * ----------
				 */
				qplan = SPI_prepare(querystr, qkey.nkeypairs, queryoids);
				qplan = SPI_saveplan(qplan);
				ri_HashPreparedPlan(&qkey, qplan);
			}

			/* ----------
			 * We have a plan now. Build up the arguments for SPI_execp()
			 * from the key values in the updated PK tuple.
			 * ----------
			 */
			for (i = 0; i < qkey.nkeypairs; i++)
			{
				upd_values[i] = SPI_getbinval(old_row,
											  pk_rel->rd_att,
									  qkey.keypair[i][RI_KEYPAIR_PK_IDX],
											  &isnull);
				if (isnull)
					upd_nulls[i] = 'n';
				else
					upd_nulls[i] = ' ';
			}
			upd_nulls[i] = '\0';

			/* ----------
			 * Now check for existing references
			 * ----------
			 */
			if (SPI_execp(qplan, upd_values, upd_nulls, 1) != SPI_OK_SELECT)
				elog(ERROR, "SPI_execp() failed in RI_FKey_noaction_upd()");

			if (SPI_GetInfo()->SPI_processed > 0)
				elog(ERROR, "%s referential integrity violation - "
					 "key in %s still referenced from %s",
					 tgargs[RI_CONSTRAINT_NAME_ARGNO],
					 tgargs[RI_PK_RELNAME_ARGNO],
					 tgargs[RI_FK_RELNAME_ARGNO]);

			if (SPI_finish() != SPI_OK_FINISH)
				elog(NOTICE, "SPI_finish() failed in RI_FKey_noaction_upd()");

			return NULL;

			/* ----------
			 * Handle MATCH PARTIAL noaction update.
			 * ----------
			 */
		case RI_MATCH_TYPE_PARTIAL:
			elog(ERROR, "MATCH PARTIAL not yet supported");
			return NULL;
	}

	/* ----------
	 * Never reached
	 * ----------
	 */
	elog(ERROR, "internal error #3 in ri_triggers.c");
	return NULL;
}


/* ----------
 * RI_FKey_cascade_del -
 *
 *	Cascaded delete foreign key references at delete event on PK table.
 * ----------
 */
HeapTuple
RI_FKey_cascade_del(FmgrInfo *proinfo)
{
	TriggerData *trigdata;
	int			tgnargs;
	char	  **tgargs;
	Relation	fk_rel;
	Relation	pk_rel;
	HeapTuple	old_row;
	RI_QueryKey qkey;
	void	   *qplan;
	Datum		del_values[RI_MAX_NUMKEYS];
	char		del_nulls[RI_MAX_NUMKEYS + 1];
	bool		isnull;
	int			i;

	trigdata = GetTriggerData();
	SetTriggerData(NULL);
	GetSnapshotHolder()->ReferentialIntegritySnapshotOverride = true;

	/* ----------
	 * Check that this is a valid trigger call on the right time and event.
	 * ----------
	 */
	if (trigdata == NULL)
		elog(ERROR, "RI_FKey_cascade_del() not fired by trigger manager");
	if (!TRIGGER_FIRED_AFTER(trigdata->tg_event) ||
		!TRIGGER_FIRED_FOR_ROW(trigdata->tg_event))
		elog(ERROR, "RI_FKey_cascade_del() must be fired AFTER ROW");
	if (!TRIGGER_FIRED_BY_DELETE(trigdata->tg_event))
		elog(ERROR, "RI_FKey_cascade_del() must be fired for DELETE");

	/* ----------
	 * Check for the correct # of call arguments
	 * ----------
	 */
	tgnargs = trigdata->tg_trigger->tgnargs;
	tgargs = trigdata->tg_trigger->tgargs;
	if (tgnargs < 4 || (tgnargs % 2) != 0)
		elog(ERROR, "wrong # of arguments in call to RI_FKey_cascade_del()");
	if (tgnargs > RI_MAX_ARGUMENTS)
		elog(ERROR, "too many keys (%d max) in call to RI_FKey_cascade_del()",
			 RI_MAX_NUMKEYS);

	/* ----------
	 * Nothing to do if no column names to compare given
	 * ----------
	 */
	if (tgnargs == 4)
		return NULL;

	/* ----------
	 * Get the relation descriptors of the FK and PK tables and
	 * the old tuple.
	 * ----------
	 */
	fk_rel = heap_openr(tgargs[RI_FK_RELNAME_ARGNO], NoLock);
	pk_rel = trigdata->tg_relation;
	old_row = trigdata->tg_trigtuple;

	switch (ri_DetermineMatchType(tgargs[RI_MATCH_TYPE_ARGNO]))
	{
			/* ----------
			 * SQL3 11.9 <referential constraint definition>
			 *	Gereral rules 6) a) i):
			 *		MATCH <unspecified> or MATCH FULL
			 *			... ON DELETE CASCADE
			 * ----------
			 */
		case RI_MATCH_TYPE_UNSPECIFIED:
		case RI_MATCH_TYPE_FULL:
			ri_BuildQueryKeyFull(&qkey, trigdata->tg_trigger->tgoid,
								 RI_PLAN_CASCADE_DEL_DODELETE,
								 fk_rel, pk_rel,
								 tgnargs, tgargs);

			switch (ri_NullCheck(pk_rel, old_row, &qkey, RI_KEYPAIR_PK_IDX))
			{
				case RI_KEYS_ALL_NULL:
				case RI_KEYS_SOME_NULL:
					/* ----------
					 * No check - MATCH FULL means there cannot be any
					 * reference to old key if it contains NULL
					 * ----------
					 */
					heap_close(fk_rel, NoLock);
					return NULL;

				case RI_KEYS_NONE_NULL:
					/* ----------
					 * Have a full qualified key - continue below
					 * ----------
					 */
					break;
			}
			heap_close(fk_rel, NoLock);

			if (SPI_connect() != SPI_OK_CONNECT)
				elog(NOTICE, "SPI_connect() failed in RI_FKey_cascade_del()");

			/* ----------
			 * Fetch or prepare a saved plan for the cascaded delete
			 * ----------
			 */
			if ((qplan = ri_FetchPreparedPlan(&qkey)) == NULL)
			{
				char		buf[256];
				char		querystr[8192];
				char	   *querysep;
				Oid			queryoids[RI_MAX_NUMKEYS];

				/* ----------
				 * The query string built is
				 *	DELETE FROM <fktable> WHERE fkatt1 = $1 [AND ...]
				 * The type id's for the $ parameters are those of the
				 * corresponding PK attributes. Thus, SPI_prepare could
				 * eventually fail if the parser cannot identify some way
				 * how to compare these two types by '='.
				 * ----------
				 */
				sprintf(querystr, "DELETE FROM \"%s\"",
						tgargs[RI_FK_RELNAME_ARGNO]);
				querysep = "WHERE";
				for (i = 0; i < qkey.nkeypairs; i++)
				{
					sprintf(buf, " %s \"%s\" = $%d", querysep,
							tgargs[4 + i * 2], i + 1);
					strcat(querystr, buf);
					querysep = "AND";
					queryoids[i] = SPI_gettypeid(pk_rel->rd_att,
									 qkey.keypair[i][RI_KEYPAIR_PK_IDX]);
				}

				/* ----------
				 * Prepare, save and remember the new plan.
				 * ----------
				 */
				qplan = SPI_prepare(querystr, qkey.nkeypairs, queryoids);
				qplan = SPI_saveplan(qplan);
				ri_HashPreparedPlan(&qkey, qplan);
			}

			/* ----------
			 * We have a plan now. Build up the arguments for SPI_execp()
			 * from the key values in the deleted PK tuple.
			 * ----------
			 */
			for (i = 0; i < qkey.nkeypairs; i++)
			{
				del_values[i] = SPI_getbinval(old_row,
											  pk_rel->rd_att,
									  qkey.keypair[i][RI_KEYPAIR_PK_IDX],
											  &isnull);
				if (isnull)
					del_nulls[i] = 'n';
				else
					del_nulls[i] = ' ';
			}
			del_nulls[i] = '\0';

			/* ----------
			 * Now delete constraint
			 * ----------
			 */
			if (SPI_execp(qplan, del_values, del_nulls, 0) != SPI_OK_DELETE)
				elog(ERROR, "SPI_execp() failed in RI_FKey_cascade_del()");

			if (SPI_finish() != SPI_OK_FINISH)
				elog(NOTICE, "SPI_finish() failed in RI_FKey_cascade_del()");

			return NULL;

			/* ----------
			 * Handle MATCH PARTIAL cascaded delete.
			 * ----------
			 */
		case RI_MATCH_TYPE_PARTIAL:
			elog(ERROR, "MATCH PARTIAL not yet supported");
			return NULL;
	}

	/* ----------
	 * Never reached
	 * ----------
	 */
	elog(ERROR, "internal error #4 in ri_triggers.c");
	return NULL;
}


/* ----------
 * RI_FKey_cascade_upd -
 *
 *	Cascaded update/delete foreign key references at update event on PK table.
 * ----------
 */
HeapTuple
RI_FKey_cascade_upd(FmgrInfo *proinfo)
{
	TriggerData *trigdata;
	int			tgnargs;
	char	  **tgargs;
	Relation	fk_rel;
	Relation	pk_rel;
	HeapTuple	new_row;
	HeapTuple	old_row;
	RI_QueryKey qkey;
	void	   *qplan;
	Datum		upd_values[RI_MAX_NUMKEYS * 2];
	char		upd_nulls[RI_MAX_NUMKEYS * 2 + 1];
	bool		isnull;
	int			i;
	int			j;

	trigdata = GetTriggerData();
	SetTriggerData(NULL);
	GetSnapshotHolder()->ReferentialIntegritySnapshotOverride = true;

	/* ----------
	 * Check that this is a valid trigger call on the right time and event.
	 * ----------
	 */
	if (trigdata == NULL)
		elog(ERROR, "RI_FKey_cascade_upd() not fired by trigger manager");
	if (!TRIGGER_FIRED_AFTER(trigdata->tg_event) ||
		!TRIGGER_FIRED_FOR_ROW(trigdata->tg_event))
		elog(ERROR, "RI_FKey_cascade_upd() must be fired AFTER ROW");
	if (!TRIGGER_FIRED_BY_UPDATE(trigdata->tg_event))
		elog(ERROR, "RI_FKey_cascade_upd() must be fired for UPDATE");

	/* ----------
	 * Check for the correct # of call arguments
	 * ----------
	 */
	tgnargs = trigdata->tg_trigger->tgnargs;
	tgargs = trigdata->tg_trigger->tgargs;
	if (tgnargs < 4 || (tgnargs % 2) != 0)
		elog(ERROR, "wrong # of arguments in call to RI_FKey_cascade_upd()");
	if (tgnargs > RI_MAX_ARGUMENTS)
		elog(ERROR, "too many keys (%d max) in call to RI_FKey_cascade_upd()",
			 RI_MAX_NUMKEYS);

	/* ----------
	 * Nothing to do if no column names to compare given
	 * ----------
	 */
	if (tgnargs == 4)
		return NULL;

	/* ----------
	 * Get the relation descriptors of the FK and PK tables and
	 * the new and old tuple.
	 * ----------
	 */
	fk_rel = heap_openr(tgargs[RI_FK_RELNAME_ARGNO], NoLock);
	pk_rel = trigdata->tg_relation;
	new_row = trigdata->tg_newtuple;
	old_row = trigdata->tg_trigtuple;

	switch (ri_DetermineMatchType(tgargs[RI_MATCH_TYPE_ARGNO]))
	{
			/* ----------
			 * SQL3 11.9 <referential constraint definition>
			 *	Gereral rules 7) a) i):
			 *		MATCH <unspecified> or MATCH FULL
			 *			... ON UPDATE CASCADE
			 * ----------
			 */
		case RI_MATCH_TYPE_UNSPECIFIED:
		case RI_MATCH_TYPE_FULL:
			ri_BuildQueryKeyFull(&qkey, trigdata->tg_trigger->tgoid,
								 RI_PLAN_CASCADE_UPD_DOUPDATE,
								 fk_rel, pk_rel,
								 tgnargs, tgargs);

			switch (ri_NullCheck(pk_rel, old_row, &qkey, RI_KEYPAIR_PK_IDX))
			{
				case RI_KEYS_ALL_NULL:
				case RI_KEYS_SOME_NULL:
					/* ----------
					 * No update - MATCH FULL means there cannot be any
					 * reference to old key if it contains NULL
					 * ----------
					 */
					heap_close(fk_rel, NoLock);
					return NULL;

				case RI_KEYS_NONE_NULL:
					/* ----------
					 * Have a full qualified key - continue below
					 * ----------
					 */
					break;
			}
			heap_close(fk_rel, NoLock);

			/* ----------
			 * No need to do anything if old and new keys are equal
			 * ----------
			 */
			if (ri_KeysEqual(pk_rel, old_row, new_row, &qkey,
							 RI_KEYPAIR_PK_IDX))
				return NULL;

			if (SPI_connect() != SPI_OK_CONNECT)
				elog(NOTICE, "SPI_connect() failed in RI_FKey_cascade_upd()");

			/* ----------
			 * Fetch or prepare a saved plan for the cascaded update
			 * of foreign references
			 * ----------
			 */
			if ((qplan = ri_FetchPreparedPlan(&qkey)) == NULL)
			{
				char		buf[256];
				char		querystr[8192];
				char		qualstr[8192];
				char	   *querysep;
				char	   *qualsep;
				Oid			queryoids[RI_MAX_NUMKEYS * 2];

				/* ----------
				 * The query string built is
				 *	UPDATE <fktable> SET fkatt1 = $1 [, ...]
				 *			WHERE fkatt1 = $n [AND ...]
				 * The type id's for the $ parameters are those of the
				 * corresponding PK attributes. Thus, SPI_prepare could
				 * eventually fail if the parser cannot identify some way
				 * how to compare these two types by '='.
				 * ----------
				 */
				sprintf(querystr, "UPDATE \"%s\" SET",
						tgargs[RI_FK_RELNAME_ARGNO]);
				qualstr[0] = '\0';
				querysep = "";
				qualsep = "WHERE";
				for (i = 0, j = qkey.nkeypairs; i < qkey.nkeypairs; i++, j++)
				{
					sprintf(buf, "%s \"%s\" = $%d", querysep,
							tgargs[4 + i * 2], i + 1);
					strcat(querystr, buf);
					sprintf(buf, " %s \"%s\" = $%d", qualsep,
							tgargs[4 + i * 2], j + 1);
					strcat(qualstr, buf);
					querysep = ",";
					qualsep = "AND";
					queryoids[i] = SPI_gettypeid(pk_rel->rd_att,
									 qkey.keypair[i][RI_KEYPAIR_PK_IDX]);
					queryoids[j] = queryoids[i];
				}
				strcat(querystr, qualstr);

				/* ----------
				 * Prepare, save and remember the new plan.
				 * ----------
				 */
				qplan = SPI_prepare(querystr, qkey.nkeypairs * 2, queryoids);
				qplan = SPI_saveplan(qplan);
				ri_HashPreparedPlan(&qkey, qplan);
			}

			/* ----------
			 * We have a plan now. Build up the arguments for SPI_execp()
			 * from the key values in the updated PK tuple.
			 * ----------
			 */
			for (i = 0, j = qkey.nkeypairs; i < qkey.nkeypairs; i++, j++)
			{
				upd_values[i] = SPI_getbinval(new_row,
											  pk_rel->rd_att,
									  qkey.keypair[i][RI_KEYPAIR_PK_IDX],
											  &isnull);
				if (isnull)
					upd_nulls[i] = 'n';
				else
					upd_nulls[i] = ' ';

				upd_values[j] = SPI_getbinval(old_row,
											  pk_rel->rd_att,
									  qkey.keypair[i][RI_KEYPAIR_PK_IDX],
											  &isnull);
				if (isnull)
					upd_nulls[j] = 'n';
				else
					upd_nulls[j] = ' ';
			}
			upd_nulls[j] = '\0';

			/* ----------
			 * Now update the existing references
			 * ----------
			 */
			if (SPI_execp(qplan, upd_values, upd_nulls, 0) != SPI_OK_UPDATE)
				elog(ERROR, "SPI_execp() failed in RI_FKey_cascade_upd()");

			if (SPI_finish() != SPI_OK_FINISH)
				elog(NOTICE, "SPI_finish() failed in RI_FKey_cascade_upd()");

			return NULL;

			/* ----------
			 * Handle MATCH PARTIAL cascade update.
			 * ----------
			 */
		case RI_MATCH_TYPE_PARTIAL:
			elog(ERROR, "MATCH PARTIAL not yet supported");
			return NULL;
	}

	/* ----------
	 * Never reached
	 * ----------
	 */
	elog(ERROR, "internal error #5 in ri_triggers.c");
	return NULL;
}


/* ----------
 * RI_FKey_restrict_del -
 *
 *	Restrict delete from PK table to rows unreferenced by foreign key.
 *
 *	SQL3 intends that this referential action occur BEFORE the
 *	update is performed, rather than after.  This appears to be
 *	the only difference between "NO ACTION" and "RESTRICT".
 *
 *	For now, however, we treat "RESTRICT" and "NO ACTION" as
 *	equivalent.
 * ----------
 */
HeapTuple
RI_FKey_restrict_del(FmgrInfo *proinfo)
{
	TriggerData *trigdata;
	int			tgnargs;
	char	  **tgargs;
	Relation	fk_rel;
	Relation	pk_rel;
	HeapTuple	old_row;
	RI_QueryKey qkey;
	void	   *qplan;
	Datum		del_values[RI_MAX_NUMKEYS];
	char		del_nulls[RI_MAX_NUMKEYS + 1];
	bool		isnull;
	int			i;

	trigdata = GetTriggerData();
	SetTriggerData(NULL);
	GetSnapshotHolder()->ReferentialIntegritySnapshotOverride = true;

	/* ----------
	 * Check that this is a valid trigger call on the right time and event.
	 * ----------
	 */
	if (trigdata == NULL)
		elog(ERROR, "RI_FKey_restrict_del() not fired by trigger manager");
	if (!TRIGGER_FIRED_AFTER(trigdata->tg_event) ||
		!TRIGGER_FIRED_FOR_ROW(trigdata->tg_event))
		elog(ERROR, "RI_FKey_restrict_del() must be fired AFTER ROW");
	if (!TRIGGER_FIRED_BY_DELETE(trigdata->tg_event))
		elog(ERROR, "RI_FKey_restrict_del() must be fired for DELETE");

	/* ----------
	 * Check for the correct # of call arguments
	 * ----------
	 */
	tgnargs = trigdata->tg_trigger->tgnargs;
	tgargs = trigdata->tg_trigger->tgargs;
	if (tgnargs < 4 || (tgnargs % 2) != 0)
		elog(ERROR, "wrong # of arguments in call to RI_FKey_restrict_del()");
	if (tgnargs > RI_MAX_ARGUMENTS)
		elog(ERROR, "too many keys (%d max) in call to RI_FKey_restrict_del()",
			 RI_MAX_NUMKEYS);

	/* ----------
	 * Nothing to do if no column names to compare given
	 * ----------
	 */
	if (tgnargs == 4)
		return NULL;

	/* ----------
	 * Get the relation descriptors of the FK and PK tables and
	 * the old tuple.
	 * ----------
	 */
	fk_rel = heap_openr(tgargs[RI_FK_RELNAME_ARGNO], NoLock);
	pk_rel = trigdata->tg_relation;
	old_row = trigdata->tg_trigtuple;

	switch (ri_DetermineMatchType(tgargs[RI_MATCH_TYPE_ARGNO]))
	{
			/* ----------
			 * SQL3 11.9 <referential constraint definition>
			 *	Gereral rules 6) a) iv):
			 *		MATCH <unspecified> or MATCH FULL
			 *			... ON DELETE CASCADE
			 * ----------
			 */
		case RI_MATCH_TYPE_UNSPECIFIED:
		case RI_MATCH_TYPE_FULL:
			ri_BuildQueryKeyFull(&qkey, trigdata->tg_trigger->tgoid,
								 RI_PLAN_RESTRICT_DEL_CHECKREF,
								 fk_rel, pk_rel,
								 tgnargs, tgargs);

			switch (ri_NullCheck(pk_rel, old_row, &qkey, RI_KEYPAIR_PK_IDX))
			{
				case RI_KEYS_ALL_NULL:
				case RI_KEYS_SOME_NULL:
					/* ----------
					 * No check - MATCH FULL means there cannot be any
					 * reference to old key if it contains NULL
					 * ----------
					 */
					heap_close(fk_rel, NoLock);
					return NULL;

				case RI_KEYS_NONE_NULL:
					/* ----------
					 * Have a full qualified key - continue below
					 * ----------
					 */
					break;
			}
			heap_close(fk_rel, NoLock);

			if (SPI_connect() != SPI_OK_CONNECT)
				elog(NOTICE, "SPI_connect() failed in RI_FKey_restrict_del()");

			/* ----------
			 * Fetch or prepare a saved plan for the restrict delete
			 * lookup if foreign references exist
			 * ----------
			 */
			if ((qplan = ri_FetchPreparedPlan(&qkey)) == NULL)
			{
				char		buf[256];
				char		querystr[8192];
				char	   *querysep;
				Oid			queryoids[RI_MAX_NUMKEYS];

				/* ----------
				 * The query string built is
				 *	SELECT oid FROM <fktable> WHERE fkatt1 = $1 [AND ...]
				 * The type id's for the $ parameters are those of the
				 * corresponding PK attributes. Thus, SPI_prepare could
				 * eventually fail if the parser cannot identify some way
				 * how to compare these two types by '='.
				 * ----------
				 */
				sprintf(querystr, "SELECT oid FROM \"%s\"",
						tgargs[RI_FK_RELNAME_ARGNO]);
				querysep = "WHERE";
				for (i = 0; i < qkey.nkeypairs; i++)
				{
					sprintf(buf, " %s \"%s\" = $%d", querysep,
							tgargs[4 + i * 2], i + 1);
					strcat(querystr, buf);
					querysep = "AND";
					queryoids[i] = SPI_gettypeid(pk_rel->rd_att,
									 qkey.keypair[i][RI_KEYPAIR_PK_IDX]);
				}
				sprintf(buf, " FOR UPDATE OF \"%s\"",
						tgargs[RI_FK_RELNAME_ARGNO]);
				strcat(querystr, buf);

				/* ----------
				 * Prepare, save and remember the new plan.
				 * ----------
				 */
				qplan = SPI_prepare(querystr, qkey.nkeypairs, queryoids);
				qplan = SPI_saveplan(qplan);
				ri_HashPreparedPlan(&qkey, qplan);
			}

			/* ----------
			 * We have a plan now. Build up the arguments for SPI_execp()
			 * from the key values in the deleted PK tuple.
			 * ----------
			 */
			for (i = 0; i < qkey.nkeypairs; i++)
			{
				del_values[i] = SPI_getbinval(old_row,
											  pk_rel->rd_att,
									  qkey.keypair[i][RI_KEYPAIR_PK_IDX],
											  &isnull);
				if (isnull)
					del_nulls[i] = 'n';
				else
					del_nulls[i] = ' ';
			}
			del_nulls[i] = '\0';

			/* ----------
			 * Now check for existing references
			 * ----------
			 */
			if (SPI_execp(qplan, del_values, del_nulls, 1) != SPI_OK_SELECT)
				elog(ERROR, "SPI_execp() failed in RI_FKey_restrict_del()");

			if (SPI_GetInfo()->SPI_processed > 0)
				elog(ERROR, "%s referential integrity violation - "
					 "key in %s still referenced from %s",
					 tgargs[RI_CONSTRAINT_NAME_ARGNO],
					 tgargs[RI_PK_RELNAME_ARGNO],
					 tgargs[RI_FK_RELNAME_ARGNO]);

			if (SPI_finish() != SPI_OK_FINISH)
				elog(NOTICE, "SPI_finish() failed in RI_FKey_restrict_del()");

			return NULL;

			/* ----------
			 * Handle MATCH PARTIAL restrict delete.
			 * ----------
			 */
		case RI_MATCH_TYPE_PARTIAL:
			elog(ERROR, "MATCH PARTIAL not yet supported");
			return NULL;
	}

	/* ----------
	 * Never reached
	 * ----------
	 */
	elog(ERROR, "internal error #6 in ri_triggers.c");
	return NULL;
}


/* ----------
 * RI_FKey_restrict_upd -
 *
 *	Restrict update of PK to rows unreferenced by foreign key.
 *
 *	SQL3 intends that this referential action occur BEFORE the
 *	update is performed, rather than after.  This appears to be
 *	the only difference between "NO ACTION" and "RESTRICT".
 *
 *	For now, however, we treat "RESTRICT" and "NO ACTION" as
 *	equivalent.
 * ----------
 */
HeapTuple
RI_FKey_restrict_upd(FmgrInfo *proinfo)
{
	TriggerData *trigdata;
	int			tgnargs;
	char	  **tgargs;
	Relation	fk_rel;
	Relation	pk_rel;
	HeapTuple	new_row;
	HeapTuple	old_row;
	RI_QueryKey qkey;
	void	   *qplan;
	Datum		upd_values[RI_MAX_NUMKEYS];
	char		upd_nulls[RI_MAX_NUMKEYS + 1];
	bool		isnull;
	int			i;

	trigdata = GetTriggerData();
	SetTriggerData(NULL);
	GetSnapshotHolder()->ReferentialIntegritySnapshotOverride = true;

	/* ----------
	 * Check that this is a valid trigger call on the right time and event.
	 * ----------
	 */
	if (trigdata == NULL)
		elog(ERROR, "RI_FKey_restrict_upd() not fired by trigger manager");
	if (!TRIGGER_FIRED_AFTER(trigdata->tg_event) ||
		!TRIGGER_FIRED_FOR_ROW(trigdata->tg_event))
		elog(ERROR, "RI_FKey_restrict_upd() must be fired AFTER ROW");
	if (!TRIGGER_FIRED_BY_UPDATE(trigdata->tg_event))
		elog(ERROR, "RI_FKey_restrict_upd() must be fired for UPDATE");

	/* ----------
	 * Check for the correct # of call arguments
	 * ----------
	 */
	tgnargs = trigdata->tg_trigger->tgnargs;
	tgargs = trigdata->tg_trigger->tgargs;
	if (tgnargs < 4 || (tgnargs % 2) != 0)
		elog(ERROR, "wrong # of arguments in call to RI_FKey_restrict_upd()");
	if (tgnargs > RI_MAX_ARGUMENTS)
		elog(ERROR, "too many keys (%d max) in call to RI_FKey_restrict_upd()",
			 RI_MAX_NUMKEYS);

	/* ----------
	 * Nothing to do if no column names to compare given
	 * ----------
	 */
	if (tgnargs == 4)
		return NULL;

	/* ----------
	 * Get the relation descriptors of the FK and PK tables and
	 * the new and old tuple.
	 * ----------
	 */
	fk_rel = heap_openr(tgargs[RI_FK_RELNAME_ARGNO], NoLock);
	pk_rel = trigdata->tg_relation;
	new_row = trigdata->tg_newtuple;
	old_row = trigdata->tg_trigtuple;

	switch (ri_DetermineMatchType(tgargs[RI_MATCH_TYPE_ARGNO]))
	{
			/* ----------
			 * SQL3 11.9 <referential constraint definition>
			 *	Gereral rules 6) a) iv):
			 *		MATCH <unspecified> or MATCH FULL
			 *			... ON DELETE CASCADE
			 * ----------
			 */
		case RI_MATCH_TYPE_UNSPECIFIED:
		case RI_MATCH_TYPE_FULL:
			ri_BuildQueryKeyFull(&qkey, trigdata->tg_trigger->tgoid,
								 RI_PLAN_RESTRICT_UPD_CHECKREF,
								 fk_rel, pk_rel,
								 tgnargs, tgargs);

			switch (ri_NullCheck(pk_rel, old_row, &qkey, RI_KEYPAIR_PK_IDX))
			{
				case RI_KEYS_ALL_NULL:
				case RI_KEYS_SOME_NULL:
					/* ----------
					 * No check - MATCH FULL means there cannot be any
					 * reference to old key if it contains NULL
					 * ----------
					 */
					heap_close(fk_rel, NoLock);
					return NULL;

				case RI_KEYS_NONE_NULL:
					/* ----------
					 * Have a full qualified key - continue below
					 * ----------
					 */
					break;
			}
			heap_close(fk_rel, NoLock);

			/* ----------
			 * No need to check anything if old and new keys are equal
			 * ----------
			 */
			if (ri_KeysEqual(pk_rel, old_row, new_row, &qkey,
							 RI_KEYPAIR_PK_IDX))
				return NULL;

			if (SPI_connect() != SPI_OK_CONNECT)
				elog(NOTICE, "SPI_connect() failed in RI_FKey_restrict_upd()");

			/* ----------
			 * Fetch or prepare a saved plan for the restrict update
			 * lookup if foreign references exist
			 * ----------
			 */
			if ((qplan = ri_FetchPreparedPlan(&qkey)) == NULL)
			{
				char		buf[256];
				char		querystr[8192];
				char	   *querysep;
				Oid			queryoids[RI_MAX_NUMKEYS];

				/* ----------
				 * The query string built is
				 *	SELECT oid FROM <fktable> WHERE fkatt1 = $1 [AND ...]
				 * The type id's for the $ parameters are those of the
				 * corresponding PK attributes. Thus, SPI_prepare could
				 * eventually fail if the parser cannot identify some way
				 * how to compare these two types by '='.
				 * ----------
				 */
				sprintf(querystr, "SELECT oid FROM \"%s\"",
						tgargs[RI_FK_RELNAME_ARGNO]);
				querysep = "WHERE";
				for (i = 0; i < qkey.nkeypairs; i++)
				{
					sprintf(buf, " %s \"%s\" = $%d", querysep,
							tgargs[4 + i * 2], i + 1);
					strcat(querystr, buf);
					querysep = "AND";
					queryoids[i] = SPI_gettypeid(pk_rel->rd_att,
									 qkey.keypair[i][RI_KEYPAIR_PK_IDX]);
				}
				sprintf(buf, " FOR UPDATE OF \"%s\"",
						tgargs[RI_FK_RELNAME_ARGNO]);
				strcat(querystr, buf);

				/* ----------
				 * Prepare, save and remember the new plan.
				 * ----------
				 */
				qplan = SPI_prepare(querystr, qkey.nkeypairs, queryoids);
				qplan = SPI_saveplan(qplan);
				ri_HashPreparedPlan(&qkey, qplan);
			}

			/* ----------
			 * We have a plan now. Build up the arguments for SPI_execp()
			 * from the key values in the updated PK tuple.
			 * ----------
			 */
			for (i = 0; i < qkey.nkeypairs; i++)
			{
				upd_values[i] = SPI_getbinval(old_row,
											  pk_rel->rd_att,
									  qkey.keypair[i][RI_KEYPAIR_PK_IDX],
											  &isnull);
				if (isnull)
					upd_nulls[i] = 'n';
				else
					upd_nulls[i] = ' ';
			}
			upd_nulls[i] = '\0';

			/* ----------
			 * Now check for existing references
			 * ----------
			 */
			if (SPI_execp(qplan, upd_values, upd_nulls, 1) != SPI_OK_SELECT)
				elog(ERROR, "SPI_execp() failed in RI_FKey_restrict_upd()");

			if (SPI_GetInfo()->SPI_processed > 0)
				elog(ERROR, "%s referential integrity violation - "
					 "key in %s still referenced from %s",
					 tgargs[RI_CONSTRAINT_NAME_ARGNO],
					 tgargs[RI_PK_RELNAME_ARGNO],
					 tgargs[RI_FK_RELNAME_ARGNO]);

			if (SPI_finish() != SPI_OK_FINISH)
				elog(NOTICE, "SPI_finish() failed in RI_FKey_restrict_upd()");

			return NULL;

			/* ----------
			 * Handle MATCH PARTIAL restrict update.
			 * ----------
			 */
		case RI_MATCH_TYPE_PARTIAL:
			elog(ERROR, "MATCH PARTIAL not yet supported");
			return NULL;
	}

	/* ----------
	 * Never reached
	 * ----------
	 */
	elog(ERROR, "internal error #7 in ri_triggers.c");
	return NULL;
}


/* ----------
 * RI_FKey_setnull_del -
 *
 *	Set foreign key references to NULL values at delete event on PK table.
 * ----------
 */
HeapTuple
RI_FKey_setnull_del(FmgrInfo *proinfo)
{
	TriggerData *trigdata;
	int			tgnargs;
	char	  **tgargs;
	Relation	fk_rel;
	Relation	pk_rel;
	HeapTuple	old_row;
	RI_QueryKey qkey;
	void	   *qplan;
	Datum		upd_values[RI_MAX_NUMKEYS];
	char		upd_nulls[RI_MAX_NUMKEYS + 1];
	bool		isnull;
	int			i;

	trigdata = GetTriggerData();
	SetTriggerData(NULL);
	GetSnapshotHolder()->ReferentialIntegritySnapshotOverride = true;

	/* ----------
	 * Check that this is a valid trigger call on the right time and event.
	 * ----------
	 */
	if (trigdata == NULL)
		elog(ERROR, "RI_FKey_setnull_del() not fired by trigger manager");
	if (!TRIGGER_FIRED_AFTER(trigdata->tg_event) ||
		!TRIGGER_FIRED_FOR_ROW(trigdata->tg_event))
		elog(ERROR, "RI_FKey_setnull_del() must be fired AFTER ROW");
	if (!TRIGGER_FIRED_BY_DELETE(trigdata->tg_event))
		elog(ERROR, "RI_FKey_setnull_del() must be fired for DELETE");

	/* ----------
	 * Check for the correct # of call arguments
	 * ----------
	 */
	tgnargs = trigdata->tg_trigger->tgnargs;
	tgargs = trigdata->tg_trigger->tgargs;
	if (tgnargs < 4 || (tgnargs % 2) != 0)
		elog(ERROR, "wrong # of arguments in call to RI_FKey_setnull_del()");
	if (tgnargs > RI_MAX_ARGUMENTS)
		elog(ERROR, "too many keys (%d max) in call to RI_FKey_setnull_del()",
			 RI_MAX_NUMKEYS);

	/* ----------
	 * Nothing to do if no column names to compare given
	 * ----------
	 */
	if (tgnargs == 4)
		return NULL;

	/* ----------
	 * Get the relation descriptors of the FK and PK tables and
	 * the old tuple.
	 * ----------
	 */
	fk_rel = heap_openr(tgargs[RI_FK_RELNAME_ARGNO], NoLock);
	pk_rel = trigdata->tg_relation;
	old_row = trigdata->tg_trigtuple;

	switch (ri_DetermineMatchType(tgargs[RI_MATCH_TYPE_ARGNO]))
	{
			/* ----------
			 * SQL3 11.9 <referential constraint definition>
			 *	Gereral rules 6) a) ii):
			 *		MATCH <UNSPECIFIED> or MATCH FULL
			 *			... ON DELETE SET NULL
			 * ----------
			 */
		case RI_MATCH_TYPE_UNSPECIFIED:
		case RI_MATCH_TYPE_FULL:
			ri_BuildQueryKeyFull(&qkey, trigdata->tg_trigger->tgoid,
								 RI_PLAN_SETNULL_DEL_DOUPDATE,
								 fk_rel, pk_rel,
								 tgnargs, tgargs);

			switch (ri_NullCheck(pk_rel, old_row, &qkey, RI_KEYPAIR_PK_IDX))
			{
				case RI_KEYS_ALL_NULL:
				case RI_KEYS_SOME_NULL:
					/* ----------
					 * No update - MATCH FULL means there cannot be any
					 * reference to old key if it contains NULL
					 * ----------
					 */
					heap_close(fk_rel, NoLock);
					return NULL;

				case RI_KEYS_NONE_NULL:
					/* ----------
					 * Have a full qualified key - continue below
					 * ----------
					 */
					break;
			}
			heap_close(fk_rel, NoLock);

			if (SPI_connect() != SPI_OK_CONNECT)
				elog(NOTICE, "SPI_connect() failed in RI_FKey_setnull_del()");

			/* ----------
			 * Fetch or prepare a saved plan for the set null delete
			 * operation
			 * ----------
			 */
			if ((qplan = ri_FetchPreparedPlan(&qkey)) == NULL)
			{
				char		buf[256];
				char		querystr[8192];
				char		qualstr[8192];
				char	   *querysep;
				char	   *qualsep;
				Oid			queryoids[RI_MAX_NUMKEYS];

				/* ----------
				 * The query string built is
				 *	UPDATE <fktable> SET fkatt1 = NULL [, ...]
				 *			WHERE fkatt1 = $1 [AND ...]
				 * The type id's for the $ parameters are those of the
				 * corresponding PK attributes. Thus, SPI_prepare could
				 * eventually fail if the parser cannot identify some way
				 * how to compare these two types by '='.
				 * ----------
				 */
				sprintf(querystr, "UPDATE \"%s\" SET",
						tgargs[RI_FK_RELNAME_ARGNO]);
				qualstr[0] = '\0';
				querysep = "";
				qualsep = "WHERE";
				for (i = 0; i < qkey.nkeypairs; i++)
				{
					sprintf(buf, "%s \"%s\" = NULL", querysep,
							tgargs[4 + i * 2]);
					strcat(querystr, buf);
					sprintf(buf, " %s \"%s\" = $%d", qualsep,
							tgargs[4 + i * 2], i + 1);
					strcat(qualstr, buf);
					querysep = ",";
					qualsep = "AND";
					queryoids[i] = SPI_gettypeid(pk_rel->rd_att,
									 qkey.keypair[i][RI_KEYPAIR_PK_IDX]);
				}
				strcat(querystr, qualstr);

				/* ----------
				 * Prepare, save and remember the new plan.
				 * ----------
				 */
				qplan = SPI_prepare(querystr, qkey.nkeypairs, queryoids);
				qplan = SPI_saveplan(qplan);
				ri_HashPreparedPlan(&qkey, qplan);
			}

			/* ----------
			 * We have a plan now. Build up the arguments for SPI_execp()
			 * from the key values in the updated PK tuple.
			 * ----------
			 */
			for (i = 0; i < qkey.nkeypairs; i++)
			{
				upd_values[i] = SPI_getbinval(old_row,
											  pk_rel->rd_att,
									  qkey.keypair[i][RI_KEYPAIR_PK_IDX],
											  &isnull);
				if (isnull)
					upd_nulls[i] = 'n';
				else
					upd_nulls[i] = ' ';
			}
			upd_nulls[i] = '\0';

			/* ----------
			 * Now update the existing references
			 * ----------
			 */
			if (SPI_execp(qplan, upd_values, upd_nulls, 0) != SPI_OK_UPDATE)
				elog(ERROR, "SPI_execp() failed in RI_FKey_setnull_del()");

			if (SPI_finish() != SPI_OK_FINISH)
				elog(NOTICE, "SPI_finish() failed in RI_FKey_setnull_del()");

			return NULL;

			/* ----------
			 * Handle MATCH PARTIAL set null delete.
			 * ----------
			 */
		case RI_MATCH_TYPE_PARTIAL:
			elog(ERROR, "MATCH PARTIAL not yet supported");
			return NULL;
	}

	/* ----------
	 * Never reached
	 * ----------
	 */
	elog(ERROR, "internal error #8 in ri_triggers.c");
	return NULL;
}


/* ----------
 * RI_FKey_setnull_upd -
 *
 *	Set foreign key references to NULL at update event on PK table.
 * ----------
 */
HeapTuple
RI_FKey_setnull_upd(FmgrInfo *proinfo)
{
	TriggerData *trigdata;
	int			tgnargs;
	char	  **tgargs;
	Relation	fk_rel;
	Relation	pk_rel;
	HeapTuple	new_row;
	HeapTuple	old_row;
	RI_QueryKey qkey;
	void	   *qplan;
	Datum		upd_values[RI_MAX_NUMKEYS];
	char		upd_nulls[RI_MAX_NUMKEYS + 1];
	bool		isnull;
	int			i;
	int			match_type;
	bool		use_cached_query;

	trigdata = GetTriggerData();
	SetTriggerData(NULL);
	GetSnapshotHolder()->ReferentialIntegritySnapshotOverride = true;

	/* ----------
	 * Check that this is a valid trigger call on the right time and event.
	 * ----------
	 */
	if (trigdata == NULL)
		elog(ERROR, "RI_FKey_setnull_upd() not fired by trigger manager");
	if (!TRIGGER_FIRED_AFTER(trigdata->tg_event) ||
		!TRIGGER_FIRED_FOR_ROW(trigdata->tg_event))
		elog(ERROR, "RI_FKey_setnull_upd() must be fired AFTER ROW");
	if (!TRIGGER_FIRED_BY_UPDATE(trigdata->tg_event))
		elog(ERROR, "RI_FKey_setnull_upd() must be fired for UPDATE");

	/* ----------
	 * Check for the correct # of call arguments
	 * ----------
	 */
	tgnargs = trigdata->tg_trigger->tgnargs;
	tgargs = trigdata->tg_trigger->tgargs;
	if (tgnargs < 4 || (tgnargs % 2) != 0)
		elog(ERROR, "wrong # of arguments in call to RI_FKey_setnull_upd()");
	if (tgnargs > RI_MAX_ARGUMENTS)
		elog(ERROR, "too many keys (%d max) in call to RI_FKey_setnull_upd()",
			 RI_MAX_NUMKEYS);

	/* ----------
	 * Nothing to do if no column names to compare given
	 * ----------
	 */
	if (tgnargs == 4)
		return NULL;

	/* ----------
	 * Get the relation descriptors of the FK and PK tables and
	 * the old tuple.
	 * ----------
	 */
	fk_rel = heap_openr(tgargs[RI_FK_RELNAME_ARGNO], NoLock);
	pk_rel = trigdata->tg_relation;
	new_row = trigdata->tg_newtuple;
	old_row = trigdata->tg_trigtuple;
	match_type = ri_DetermineMatchType(tgargs[RI_MATCH_TYPE_ARGNO]);

	switch (match_type)
	{
			/* ----------
			 * SQL3 11.9 <referential constraint definition>
			 *	Gereral rules 7) a) ii) 2):
			 *		MATCH FULL
			 *			... ON UPDATE SET NULL
			 * ----------
			 */
		case RI_MATCH_TYPE_UNSPECIFIED:
		case RI_MATCH_TYPE_FULL:
			ri_BuildQueryKeyFull(&qkey, trigdata->tg_trigger->tgoid,
								 RI_PLAN_SETNULL_UPD_DOUPDATE,
								 fk_rel, pk_rel,
								 tgnargs, tgargs);

			switch (ri_NullCheck(pk_rel, old_row, &qkey, RI_KEYPAIR_PK_IDX))
			{
				case RI_KEYS_ALL_NULL:
				case RI_KEYS_SOME_NULL:
					/* ----------
					 * No update - MATCH FULL means there cannot be any
					 * reference to old key if it contains NULL
					 * ----------
					 */
					heap_close(fk_rel, NoLock);
					return NULL;

				case RI_KEYS_NONE_NULL:
					/* ----------
					 * Have a full qualified key - continue below
					 * ----------
					 */
					break;
			}
			heap_close(fk_rel, NoLock);


			/* ----------
			 * No need to do anything if old and new keys are equal
			 * ----------
			 */
			if (ri_KeysEqual(pk_rel, old_row, new_row, &qkey,
							 RI_KEYPAIR_PK_IDX))
				return NULL;

			if (SPI_connect() != SPI_OK_CONNECT)
				elog(NOTICE, "SPI_connect() failed in RI_FKey_setnull_upd()");

			/*
			 * "MATCH <unspecified>" only changes columns corresponding to
			 * the referenced columns that have changed in pk_rel.	This
			 * means the "SET attrn=NULL [, attrn=NULL]" string will be
			 * change as well.	In this case, we need to build a temporary
			 * plan rather than use our cached plan, unless the update
			 * happens to change all columns in the key.  Fortunately, for
			 * the most common case of a single-column foreign key, this
			 * will be true.
			 *
			 * In case you're wondering, the inequality check works because
			 * we know that the old key value has no NULLs (see above).
			 */

			use_cached_query = match_type == RI_MATCH_TYPE_FULL ||
				ri_AllKeysUnequal(pk_rel, old_row, new_row,
								  &qkey, RI_KEYPAIR_PK_IDX);

			/* ----------
			 * Fetch or prepare a saved plan for the set null update
			 * operation if possible, or build a temporary plan if not.
			 * ----------
			 */
			if (!use_cached_query ||
				(qplan = ri_FetchPreparedPlan(&qkey)) == NULL)
			{
				char		buf[256];
				char		querystr[8192];
				char		qualstr[8192];
				char	   *querysep;
				char	   *qualsep;
				Oid			queryoids[RI_MAX_NUMKEYS];

				/* ----------
				 * The query string built is
				 *	UPDATE <fktable> SET fkatt1 = NULL [, ...]
				 *			WHERE fkatt1 = $1 [AND ...]
				 * The type id's for the $ parameters are those of the
				 * corresponding PK attributes. Thus, SPI_prepare could
				 * eventually fail if the parser cannot identify some way
				 * how to compare these two types by '='.
				 * ----------
				 */
				sprintf(querystr, "UPDATE \"%s\" SET",
						tgargs[RI_FK_RELNAME_ARGNO]);
				qualstr[0] = '\0';
				querysep = "";
				qualsep = "WHERE";
				for (i = 0; i < qkey.nkeypairs; i++)
				{

					/*
					 * MATCH <unspecified> - only change columns
					 * corresponding to changed columns in pk_rel's key
					 */
					if (match_type == RI_MATCH_TYPE_FULL ||
					  !ri_OneKeyEqual(pk_rel, i, old_row, new_row, &qkey,
									  RI_KEYPAIR_PK_IDX))
					{
						sprintf(buf, "%s \"%s\" = NULL", querysep,
								tgargs[4 + i * 2]);
						strcat(querystr, buf);
						querysep = ",";
					}
					sprintf(buf, " %s \"%s\" = $%d", qualsep,
							tgargs[4 + i * 2], i + 1);
					strcat(qualstr, buf);
					qualsep = "AND";
					queryoids[i] = SPI_gettypeid(pk_rel->rd_att,
									 qkey.keypair[i][RI_KEYPAIR_PK_IDX]);
				}
				strcat(querystr, qualstr);

				/* ----------
				 * Prepare the new plan.
				 * ----------
				 */
				qplan = SPI_prepare(querystr, qkey.nkeypairs, queryoids);

				/*
				 * Save and remember the plan if we're building the
				 * "standard" plan.
				 */
				if (use_cached_query)
				{
					qplan = SPI_saveplan(qplan);
					ri_HashPreparedPlan(&qkey, qplan);
				}
			}

			/* ----------
			 * We have a plan now. Build up the arguments for SPI_execp()
			 * from the key values in the updated PK tuple.
			 * ----------
			 */
			for (i = 0; i < qkey.nkeypairs; i++)
			{
				upd_values[i] = SPI_getbinval(old_row,
											  pk_rel->rd_att,
									  qkey.keypair[i][RI_KEYPAIR_PK_IDX],
											  &isnull);
				if (isnull)
					upd_nulls[i] = 'n';
				else
					upd_nulls[i] = ' ';
			}
			upd_nulls[i] = '\0';

			/* ----------
			 * Now update the existing references
			 * ----------
			 */
			if (SPI_execp(qplan, upd_values, upd_nulls, 0) != SPI_OK_UPDATE)
				elog(ERROR, "SPI_execp() failed in RI_FKey_setnull_upd()");

			if (SPI_finish() != SPI_OK_FINISH)
				elog(NOTICE, "SPI_finish() failed in RI_FKey_setnull_upd()");

			return NULL;

			/* ----------
			 * Handle MATCH PARTIAL set null update.
			 * ----------
			 */
		case RI_MATCH_TYPE_PARTIAL:
			elog(ERROR, "MATCH PARTIAL not yet supported");
			return NULL;
	}

	/* ----------
	 * Never reached
	 * ----------
	 */
	elog(ERROR, "internal error #9 in ri_triggers.c");
	return NULL;
}


/* ----------
 * RI_FKey_setdefault_del -
 *
 *	Set foreign key references to defaults at delete event on PK table.
 * ----------
 */
HeapTuple
RI_FKey_setdefault_del(FmgrInfo *proinfo)
{
	TriggerData *trigdata;
	int			tgnargs;
	char	  **tgargs;
	Relation	fk_rel;
	Relation	pk_rel;
	HeapTuple	old_row;
	RI_QueryKey qkey;
	void	   *qplan;
	Datum		upd_values[RI_MAX_NUMKEYS];
	char		upd_nulls[RI_MAX_NUMKEYS + 1];
	bool		isnull;
	int			i;

	trigdata = GetTriggerData();
	SetTriggerData(NULL);
	GetSnapshotHolder()->ReferentialIntegritySnapshotOverride = true;

	/* ----------
	 * Check that this is a valid trigger call on the right time and event.
	 * ----------
	 */
	if (trigdata == NULL)
		elog(ERROR, "RI_FKey_setdefault_del() not fired by trigger manager");
	if (!TRIGGER_FIRED_AFTER(trigdata->tg_event) ||
		!TRIGGER_FIRED_FOR_ROW(trigdata->tg_event))
		elog(ERROR, "RI_FKey_setdefault_del() must be fired AFTER ROW");
	if (!TRIGGER_FIRED_BY_DELETE(trigdata->tg_event))
		elog(ERROR, "RI_FKey_setdefault_del() must be fired for DELETE");

	/* ----------
	 * Check for the correct # of call arguments
	 * ----------
	 */
	tgnargs = trigdata->tg_trigger->tgnargs;
	tgargs = trigdata->tg_trigger->tgargs;
	if (tgnargs < 4 || (tgnargs % 2) != 0)
		elog(ERROR, "wrong # of arguments in call to RI_FKey_setdefault_del()");
	if (tgnargs > RI_MAX_ARGUMENTS)
		elog(ERROR, "too many keys (%d max) in call to RI_FKey_setdefault_del()",
			 RI_MAX_NUMKEYS);

	/* ----------
	 * Nothing to do if no column names to compare given
	 * ----------
	 */
	if (tgnargs == 4)
		return NULL;

	/* ----------
	 * Get the relation descriptors of the FK and PK tables and
	 * the old tuple.
	 * ----------
	 */
	fk_rel = heap_openr(tgargs[RI_FK_RELNAME_ARGNO], NoLock);
	pk_rel = trigdata->tg_relation;
	old_row = trigdata->tg_trigtuple;

	switch (ri_DetermineMatchType(tgargs[RI_MATCH_TYPE_ARGNO]))
	{
			/* ----------
			 * SQL3 11.9 <referential constraint definition>
			 *	Gereral rules 6) a) iii):
			 *		MATCH <UNSPECIFIED> or MATCH FULL
			 *			... ON DELETE SET DEFAULT
			 * ----------
			 */
		case RI_MATCH_TYPE_UNSPECIFIED:
		case RI_MATCH_TYPE_FULL:
			ri_BuildQueryKeyFull(&qkey, trigdata->tg_trigger->tgoid,
								 RI_PLAN_SETNULL_DEL_DOUPDATE,
								 fk_rel, pk_rel,
								 tgnargs, tgargs);

			switch (ri_NullCheck(pk_rel, old_row, &qkey, RI_KEYPAIR_PK_IDX))
			{
				case RI_KEYS_ALL_NULL:
				case RI_KEYS_SOME_NULL:
					/* ----------
					 * No update - MATCH FULL means there cannot be any
					 * reference to old key if it contains NULL
					 * ----------
					 */
					heap_close(fk_rel, NoLock);
					return NULL;

				case RI_KEYS_NONE_NULL:
					/* ----------
					 * Have a full qualified key - continue below
					 * ----------
					 */
					break;
			}
			heap_close(fk_rel, NoLock);

			if (SPI_connect() != SPI_OK_CONNECT)
				elog(NOTICE, "SPI_connect() failed in RI_FKey_setdefault_del()");

			/* ----------
			 * Prepare a plan for the set defalt delete operation.
			 * Unfortunately we need to do it on every invocation
			 * because the default value could potentially change
			 * between calls.
			 * ----------
			 */
			{
				char		buf[256];
				char		querystr[8192];
				char		qualstr[8192];
				char	   *querysep;
				char	   *qualsep;
				Oid			queryoids[RI_MAX_NUMKEYS];
				Plan	   *spi_plan;
				AttrDefault *defval;
				TargetEntry *spi_qptle;
				int			i,
							j;

				/* ----------
				 * The query string built is
				 *	UPDATE <fktable> SET fkatt1 = NULL [, ...]
				 *			WHERE fkatt1 = $1 [AND ...]
				 * The type id's for the $ parameters are those of the
				 * corresponding PK attributes. Thus, SPI_prepare could
				 * eventually fail if the parser cannot identify some way
				 * how to compare these two types by '='.
				 * ----------
				 */
				sprintf(querystr, "UPDATE \"%s\" SET",
						tgargs[RI_FK_RELNAME_ARGNO]);
				qualstr[0] = '\0';
				querysep = "";
				qualsep = "WHERE";
				for (i = 0; i < qkey.nkeypairs; i++)
				{
					sprintf(buf, "%s \"%s\" = NULL", querysep,
							tgargs[4 + i * 2]);
					strcat(querystr, buf);
					sprintf(buf, " %s \"%s\" = $%d", qualsep,
							tgargs[4 + i * 2], i + 1);
					strcat(qualstr, buf);
					querysep = ",";
					qualsep = "AND";
					queryoids[i] = SPI_gettypeid(pk_rel->rd_att,
									 qkey.keypair[i][RI_KEYPAIR_PK_IDX]);
				}
				strcat(querystr, qualstr);

				/* ----------
				 * Prepare the plan
				 * ----------
				 */
				qplan = SPI_prepare(querystr, qkey.nkeypairs, queryoids);

				/* ----------
				 * Here now follows very ugly code depending on internals
				 * of the SPI manager.
				 *
				 * EVIL EVIL EVIL (but must be - Jan)
				 *
				 * We replace the CONST NULL targetlist expressions
				 * in the generated plan by (any) default values found
				 * in the tuple constructor.
				 * ----------
				 */
				spi_plan = (Plan *) lfirst(((_SPI_plan *) qplan)->ptlist);
				if (fk_rel->rd_att->constr != NULL)
					defval = fk_rel->rd_att->constr->defval;
				else
					defval = NULL;
				for (i = 0; i < qkey.nkeypairs && defval != NULL; i++)
				{
					/* ----------
					 * For each key attribute lookup the tuple constructor
					 * for a corresponding default value
					 * ----------
					 */
					for (j = 0; j < fk_rel->rd_att->constr->num_defval; j++)
					{
						if (defval[j].adnum ==
							qkey.keypair[i][RI_KEYPAIR_FK_IDX])
						{
							/* ----------
							 * That's the one - push the expression
							 * from defval.adbin into the plan's targetlist
							 * ----------
							 */
							spi_qptle = (TargetEntry *)
								nth(defval[j].adnum - 1,
									spi_plan->targetlist);
							spi_qptle->expr = stringToNode(defval[j].adbin);

							break;
						}
					}
				}
			}

			/* ----------
			 * We have a plan now. Build up the arguments for SPI_execp()
			 * from the key values in the deleted PK tuple.
			 * ----------
			 */
			for (i = 0; i < qkey.nkeypairs; i++)
			{
				upd_values[i] = SPI_getbinval(old_row,
											  pk_rel->rd_att,
									  qkey.keypair[i][RI_KEYPAIR_PK_IDX],
											  &isnull);
				if (isnull)
					upd_nulls[i] = 'n';
				else
					upd_nulls[i] = ' ';
			}
			upd_nulls[i] = '\0';

			/* ----------
			 * Now update the existing references
			 * ----------
			 */
			if (SPI_execp(qplan, upd_values, upd_nulls, 0) != SPI_OK_UPDATE)
				elog(ERROR, "SPI_execp() failed in RI_FKey_setdefault_del()");

			if (SPI_finish() != SPI_OK_FINISH)
				elog(NOTICE, "SPI_finish() failed in RI_FKey_setdefault_del()");

			return NULL;

			/* ----------
			 * Handle MATCH PARTIAL set null delete.
			 * ----------
			 */
		case RI_MATCH_TYPE_PARTIAL:
			elog(ERROR, "MATCH PARTIAL not yet supported");
			return NULL;
	}

	/* ----------
	 * Never reached
	 * ----------
	 */
	elog(ERROR, "internal error #10 in ri_triggers.c");
	return NULL;
}


/* ----------
 * RI_FKey_setdefault_upd -
 *
 *	Set foreign key references to defaults at update event on PK table.
 * ----------
 */
HeapTuple
RI_FKey_setdefault_upd(FmgrInfo *proinfo)
{
	TriggerData *trigdata;
	int			tgnargs;
	char	  **tgargs;
	Relation	fk_rel;
	Relation	pk_rel;
	HeapTuple	new_row;
	HeapTuple	old_row;
	RI_QueryKey qkey;
	void	   *qplan;
	Datum		upd_values[RI_MAX_NUMKEYS];
	char		upd_nulls[RI_MAX_NUMKEYS + 1];
	bool		isnull;
	int			i;
	int			match_type;

	trigdata = GetTriggerData();
	SetTriggerData(NULL);
	GetSnapshotHolder()->ReferentialIntegritySnapshotOverride = true;

	/* ----------
	 * Check that this is a valid trigger call on the right time and event.
	 * ----------
	 */
	if (trigdata == NULL)
		elog(ERROR, "RI_FKey_setdefault_upd() not fired by trigger manager");
	if (!TRIGGER_FIRED_AFTER(trigdata->tg_event) ||
		!TRIGGER_FIRED_FOR_ROW(trigdata->tg_event))
		elog(ERROR, "RI_FKey_setdefault_upd() must be fired AFTER ROW");
	if (!TRIGGER_FIRED_BY_UPDATE(trigdata->tg_event))
		elog(ERROR, "RI_FKey_setdefault_upd() must be fired for UPDATE");

	/* ----------
	 * Check for the correct # of call arguments
	 * ----------
	 */
	tgnargs = trigdata->tg_trigger->tgnargs;
	tgargs = trigdata->tg_trigger->tgargs;
	if (tgnargs < 4 || (tgnargs % 2) != 0)
		elog(ERROR, "wrong # of arguments in call to RI_FKey_setdefault_upd()");
	if (tgnargs > RI_MAX_ARGUMENTS)
		elog(ERROR, "too many keys (%d max) in call to RI_FKey_setdefault_upd()",
			 RI_MAX_NUMKEYS);

	/* ----------
	 * Nothing to do if no column names to compare given
	 * ----------
	 */
	if (tgnargs == 4)
		return NULL;

	/* ----------
	 * Get the relation descriptors of the FK and PK tables and
	 * the old tuple.
	 * ----------
	 */
	fk_rel = heap_openr(tgargs[RI_FK_RELNAME_ARGNO], NoLock);
	pk_rel = trigdata->tg_relation;
	new_row = trigdata->tg_newtuple;
	old_row = trigdata->tg_trigtuple;

	match_type = ri_DetermineMatchType(tgargs[RI_MATCH_TYPE_ARGNO]);

	switch (match_type)
	{
			/* ----------
			 * SQL3 11.9 <referential constraint definition>
			 *	Gereral rules 7) a) iii):
			 *		MATCH <UNSPECIFIED> or MATCH FULL
			 *			... ON UPDATE SET DEFAULT
			 * ----------
			 */
		case RI_MATCH_TYPE_UNSPECIFIED:
		case RI_MATCH_TYPE_FULL:
			ri_BuildQueryKeyFull(&qkey, trigdata->tg_trigger->tgoid,
								 RI_PLAN_SETNULL_DEL_DOUPDATE,
								 fk_rel, pk_rel,
								 tgnargs, tgargs);

			switch (ri_NullCheck(pk_rel, old_row, &qkey, RI_KEYPAIR_PK_IDX))
			{
				case RI_KEYS_ALL_NULL:
				case RI_KEYS_SOME_NULL:
					/* ----------
					 * No update - MATCH FULL means there cannot be any
					 * reference to old key if it contains NULL
					 * ----------
					 */
					heap_close(fk_rel, NoLock);
					return NULL;

				case RI_KEYS_NONE_NULL:
					/* ----------
					 * Have a full qualified key - continue below
					 * ----------
					 */
					break;
			}
			heap_close(fk_rel, NoLock);

			/* ----------
			 * No need to do anything if old and new keys are equal
			 * ----------
			 */
			if (ri_KeysEqual(pk_rel, old_row, new_row, &qkey,
							 RI_KEYPAIR_PK_IDX))
				return NULL;

			if (SPI_connect() != SPI_OK_CONNECT)
				elog(NOTICE, "SPI_connect() failed in RI_FKey_setdefault_upd()");

			/* ----------
			 * Prepare a plan for the set defalt delete operation.
			 * Unfortunately we need to do it on every invocation
			 * because the default value could potentially change
			 * between calls.
			 * ----------
			 */
			{
				char		buf[256];
				char		querystr[8192];
				char		qualstr[8192];
				char	   *querysep;
				char	   *qualsep;
				Oid			queryoids[RI_MAX_NUMKEYS];
				Plan	   *spi_plan;
				AttrDefault *defval;
				TargetEntry *spi_qptle;
				int			i,
							j;

				/* ----------
				 * The query string built is
				 *	UPDATE <fktable> SET fkatt1 = NULL [, ...]
				 *			WHERE fkatt1 = $1 [AND ...]
				 * The type id's for the $ parameters are those of the
				 * corresponding PK attributes. Thus, SPI_prepare could
				 * eventually fail if the parser cannot identify some way
				 * how to compare these two types by '='.
				 * ----------
				 */
				sprintf(querystr, "UPDATE \"%s\" SET",
						tgargs[RI_FK_RELNAME_ARGNO]);
				qualstr[0] = '\0';
				querysep = "";
				qualsep = "WHERE";
				for (i = 0; i < qkey.nkeypairs; i++)
				{

					/*
					 * MATCH <unspecified> - only change columns
					 * corresponding to changed columns in pk_rel's key
					 */
					if (match_type == RI_MATCH_TYPE_FULL ||
						!ri_OneKeyEqual(pk_rel, i, old_row,
									  new_row, &qkey, RI_KEYPAIR_PK_IDX))
					{
						sprintf(buf, "%s \"%s\" = NULL", querysep,
								tgargs[4 + i * 2]);
						strcat(querystr, buf);
						querysep = ",";
					}
					sprintf(buf, " %s \"%s\" = $%d", qualsep,
							tgargs[4 + i * 2], i + 1);
					strcat(qualstr, buf);
					qualsep = "AND";
					queryoids[i] = SPI_gettypeid(pk_rel->rd_att,
									 qkey.keypair[i][RI_KEYPAIR_PK_IDX]);
				}
				strcat(querystr, qualstr);

				/* ----------
				 * Prepare the plan
				 * ----------
				 */
				qplan = SPI_prepare(querystr, qkey.nkeypairs, queryoids);

				/* ----------
				 * Now replace the CONST NULL targetlist expressions
				 * in the generated plan by (any) default values found
				 * in the tuple constructor.
				 * ----------
				 */
				spi_plan = (Plan *) lfirst(((_SPI_plan *) qplan)->ptlist);
				if (fk_rel->rd_att->constr != NULL)
					defval = fk_rel->rd_att->constr->defval;
				else
					defval = NULL;
				for (i = 0; i < qkey.nkeypairs && defval != NULL; i++)
				{

					/*
					 * MATCH <unspecified> - only change columns
					 * corresponding to changed columns in pk_rel's key.
					 * This conditional must match the one in the loop
					 * above that built the SET attrn=NULL list.
					 */
					if (match_type == RI_MATCH_TYPE_FULL ||
						!ri_OneKeyEqual(pk_rel, i, old_row,
									  new_row, &qkey, RI_KEYPAIR_PK_IDX))
					{
						/* ----------
						 * For each key attribute lookup the tuple constructor
						 * for a corresponding default value
						 * ----------
						 */
						for (j = 0; j < fk_rel->rd_att->constr->num_defval; j++)
						{
							if (defval[j].adnum ==
								qkey.keypair[i][RI_KEYPAIR_FK_IDX])
							{
								/* ----------
								 * That's the one - push the expression
								 * from defval.adbin into the plan's targetlist
								 * ----------
								 */
								spi_qptle = (TargetEntry *)
									nth(defval[j].adnum - 1,
										spi_plan->targetlist);
								spi_qptle->expr = stringToNode(defval[j].adbin);

								break;
							}
						}
					}
				}
			}

			/* ----------
			 * We have a plan now. Build up the arguments for SPI_execp()
			 * from the key values in the deleted PK tuple.
			 * ----------
			 */
			for (i = 0; i < qkey.nkeypairs; i++)
			{
				upd_values[i] = SPI_getbinval(old_row,
											  pk_rel->rd_att,
									  qkey.keypair[i][RI_KEYPAIR_PK_IDX],
											  &isnull);
				if (isnull)
					upd_nulls[i] = 'n';
				else
					upd_nulls[i] = ' ';
			}
			upd_nulls[i] = '\0';

			/* ----------
			 * Now update the existing references
			 * ----------
			 */
			if (SPI_execp(qplan, upd_values, upd_nulls, 0) != SPI_OK_UPDATE)
				elog(ERROR, "SPI_execp() failed in RI_FKey_setdefault_upd()");

			if (SPI_finish() != SPI_OK_FINISH)
				elog(NOTICE, "SPI_finish() failed in RI_FKey_setdefault_upd()");

			return NULL;

			/* ----------
			 * Handle MATCH PARTIAL set null delete.
			 * ----------
			 */
		case RI_MATCH_TYPE_PARTIAL:
			elog(ERROR, "MATCH PARTIAL not yet supported");
			return NULL;
	}

	/* ----------
	 * Never reached
	 * ----------
	 */
	elog(ERROR, "internal error #11 in ri_triggers.c");
	return NULL;
}


/* ----------
 * RI_FKey_keyequal_upd -
 *
 *	Check if we have a key change on update.
 *
 *	This is no real trigger procedure. It is used by the deferred
 *	trigger queue manager to detect "triggered data change violation".
 * ----------
 */
bool
RI_FKey_keyequal_upd(void)
{
	TriggerData *trigdata;
	int			tgnargs;
	char	  **tgargs;
	Relation	fk_rel;
	Relation	pk_rel;
	HeapTuple	new_row;
	HeapTuple	old_row;
	RI_QueryKey qkey;

	trigdata = GetTriggerData();
	SetTriggerData(NULL);

	/* ----------
	 * Check for the correct # of call arguments
	 * ----------
	 */
	tgnargs = trigdata->tg_trigger->tgnargs;
	tgargs = trigdata->tg_trigger->tgargs;
	if (tgnargs < 4 || (tgnargs % 2) != 0)
		elog(ERROR, "wrong # of arguments in call to RI_FKey_keyequal_upd()");
	if (tgnargs > RI_MAX_ARGUMENTS)
		elog(ERROR, "too many keys (%d max) in call to RI_FKey_keyequal_upd()",
			 RI_MAX_NUMKEYS);

	/* ----------
	 * Nothing to do if no column names to compare given
	 * ----------
	 */
	if (tgnargs == 4)
		return true;

	/* ----------
	 * Get the relation descriptors of the FK and PK tables and
	 * the new and old tuple.
	 * ----------
	 */
	fk_rel = heap_openr(tgargs[RI_FK_RELNAME_ARGNO], NoLock);
	pk_rel = trigdata->tg_relation;
	new_row = trigdata->tg_newtuple;
	old_row = trigdata->tg_trigtuple;

	switch (ri_DetermineMatchType(tgargs[RI_MATCH_TYPE_ARGNO]))
	{
			/* ----------
			 * MATCH <UNSPECIFIED>
			 * ----------
			 */
		case RI_MATCH_TYPE_UNSPECIFIED:
		case RI_MATCH_TYPE_FULL:
			ri_BuildQueryKeyFull(&qkey, trigdata->tg_trigger->tgoid,
								 0,
								 fk_rel, pk_rel,
								 tgnargs, tgargs);
			heap_close(fk_rel, NoLock);

			/* ----------
			 * Return if key's are equal
			 * ----------
			 */
			return ri_KeysEqual(pk_rel, old_row, new_row, &qkey,
								RI_KEYPAIR_PK_IDX);

			/* ----------
			 * Handle MATCH PARTIAL set null delete.
			 * ----------
			 */
		case RI_MATCH_TYPE_PARTIAL:
			elog(ERROR, "MATCH PARTIAL not yet supported");
			break;
	}

	/* ----------
	 * Never reached
	 * ----------
	 */
	elog(ERROR, "internal error #12 in ri_triggers.c");
	return false;
}





/* ----------
 * Local functions below
 * ----------
 */





/* ----------
 * ri_DetermineMatchType -
 *
 *	Convert the MATCH TYPE string into a switchable int
 * ----------
 */
static int
ri_DetermineMatchType(char *str)
{
	if (!strcmp(str, "UNSPECIFIED"))
		return RI_MATCH_TYPE_UNSPECIFIED;
	if (!strcmp(str, "FULL"))
		return RI_MATCH_TYPE_FULL;
	if (!strcmp(str, "PARTIAL"))
		return RI_MATCH_TYPE_PARTIAL;

	elog(ERROR, "unrecognized referential integrity MATCH type '%s'", str);
	return 0;
}


/* ----------
 * ri_BuildQueryKeyFull -
 *
 *	Build up a new hashtable key for a prepared SPI plan of a
 *	constraint trigger of MATCH FULL. The key consists of:
 *
 *		constr_type is FULL
 *		constr_id is the OID of the pg_trigger row that invoked us
 *		constr_queryno is an internal number of the query inside the proc
 *		fk_relid is the OID of referencing relation
 *		pk_relid is the OID of referenced relation
 *		nkeypairs is the number of keypairs
 *		following are the attribute number keypairs of the trigger invocation
 *
 *	At least for MATCH FULL this builds a unique key per plan.
 * ----------
 */
static void
ri_BuildQueryKeyFull(RI_QueryKey *key, Oid constr_id, int32 constr_queryno,
					 Relation fk_rel, Relation pk_rel,
					 int argc, char **argv)
{
	int			i;
	int			j;
	int			fno;

	/* ----------
	 * Initialize the key and fill in type, oid's and number of keypairs
	 * ----------
	 */
	memset((void *) key, 0, sizeof(RI_QueryKey));
	key->constr_type = RI_MATCH_TYPE_FULL;
	key->constr_id = constr_id;
	key->constr_queryno = constr_queryno;
	key->fk_relid = fk_rel->rd_id;
	key->pk_relid = pk_rel->rd_id;
	key->nkeypairs = (argc - RI_FIRST_ATTNAME_ARGNO) / 2;

	/* ----------
	 * Lookup the attribute numbers of the arguments to the trigger call
	 * and fill in the keypairs.
	 * ----------
	 */
	for (i = 0, j = RI_FIRST_ATTNAME_ARGNO; j < argc; i++, j += 2)
	{
		fno = SPI_fnumber(fk_rel->rd_att, argv[j]);
		if (fno == SPI_ERROR_NOATTRIBUTE)
			elog(ERROR, "constraint %s: table %s does not have an attribute %s",
				 argv[RI_CONSTRAINT_NAME_ARGNO],
				 argv[RI_FK_RELNAME_ARGNO],
				 argv[j]);
		key->keypair[i][RI_KEYPAIR_FK_IDX] = fno;

		fno = SPI_fnumber(pk_rel->rd_att, argv[j + 1]);
		if (fno == SPI_ERROR_NOATTRIBUTE)
			elog(ERROR, "constraint %s: table %s does not have an attribute %s",
				 argv[RI_CONSTRAINT_NAME_ARGNO],
				 argv[RI_PK_RELNAME_ARGNO],
				 argv[j + 1]);
		key->keypair[i][RI_KEYPAIR_PK_IDX] = fno;
	}

	return;
}


/* ----------
 * ri_NullCheck -
 *
 *	Determine the NULL state of all key values in a tuple
 *
 *	Returns one of RI_KEYS_ALL_NULL, RI_KEYS_NONE_NULL or RI_KEYS_SOME_NULL.
 * ----------
 */
static int
ri_NullCheck(Relation rel, HeapTuple tup, RI_QueryKey *key, int pairidx)
{
	int			i;
	bool		isnull;
	bool		allnull = true;
	bool		nonenull = true;

	for (i = 0; i < key->nkeypairs; i++)
	{
		isnull = false;
		SPI_getbinval(tup, rel->rd_att, key->keypair[i][pairidx], &isnull);
		if (isnull)
			nonenull = false;
		else
			allnull = false;
	}

	if (allnull)
		return RI_KEYS_ALL_NULL;

	if (nonenull)
		return RI_KEYS_NONE_NULL;

	return RI_KEYS_SOME_NULL;
}


/* ----------
 * ri_InitHashTables -
 *
 *	Initialize our internal hash tables for prepared
 *	query plans and equal operators.
 * ----------
 */
static void
ri_InitHashTables(void)
{
	HASHCTL		ctl;

	memset(&ctl, 0, sizeof(ctl));
	ctl.keysize = sizeof(RI_QueryKey);
	ctl.entrysize = sizeof(RI_QueryHashEntry);
	ri_query_cache = hash_create("ri_queryhash",RI_INIT_QUERYHASHSIZE, &ctl, HASH_ELEM);

	memset(&ctl, 0, sizeof(ctl));
	ctl.keysize = sizeof(Oid);
	ctl.entrysize = sizeof(RI_OpreqHashEntry);
	ctl.hash = tag_hash;
	ri_opreq_cache = hash_create("ri_opreqhash",RI_INIT_OPREQHASHSIZE, &ctl,
								 HASH_ELEM | HASH_FUNCTION);
}


/* ----------
 * ri_FetchPreparedPlan -
 *
 *	Lookup for a query key in our private hash table of prepared
 *	and saved SPI execution plans. Return the plan if found or NULL.
 * ----------
 */
static void *
ri_FetchPreparedPlan(RI_QueryKey *key)
{
	RI_QueryHashEntry *entry;
	bool		found;

	/* ----------
	 * On the first call initialize the hashtable
	 * ----------
	 */
	if (!ri_query_cache)
		ri_InitHashTables();

	/* ----------
	 * Lookup for the key
	 * ----------
	 */
	entry = (RI_QueryHashEntry *) hash_search(ri_query_cache,
										(char *) key, HASH_FIND, &found);
	if (!found)
		return NULL;
	return entry->plan;
}


/* ----------
 * ri_HashPreparedPlan -
 *
 *	Add another plan to our private SPI query plan hashtable.
 * ----------
 */
static void
ri_HashPreparedPlan(RI_QueryKey *key, void *plan)
{
	RI_QueryHashEntry *entry;
	bool		found;

	/* ----------
	 * On the first call initialize the hashtable
	 * ----------
	 */
	if (!ri_query_cache)
		ri_InitHashTables();

	/* ----------
	 * Add the new plan.
	 * ----------
	 */
	entry = (RI_QueryHashEntry *) hash_search(ri_query_cache,
									   (char *) key, HASH_ENTER, &found);

	entry->plan = plan;
}


/* ----------
 * ri_KeysEqual -
 *
 *	Check if all key values in OLD and NEW are equal.
 * ----------
 */
static bool
ri_KeysEqual(Relation rel, HeapTuple oldtup, HeapTuple newtup,
			 RI_QueryKey *key, int pairidx)
{
	int			i;
	Oid			typeid;
	Datum		oldvalue;
	Datum		newvalue;
	bool		isnull;

	for (i = 0; i < key->nkeypairs; i++)
	{
		/* ----------
		 * Get one attributes oldvalue. If it is NULL - they're not equal.
		 * ----------
		 */
		oldvalue = SPI_getbinval(oldtup, rel->rd_att,
								 key->keypair[i][pairidx], &isnull);
		if (isnull)
			return false;

		/* ----------
		 * Get one attributes oldvalue. If it is NULL - they're not equal.
		 * ----------
		 */
		newvalue = SPI_getbinval(newtup, rel->rd_att,
								 key->keypair[i][pairidx], &isnull);
		if (isnull)
			return false;

		/* ----------
		 * Get the attributes type OID and call the '=' operator
		 * to compare the values.
		 * ----------
		 */
		typeid = SPI_gettypeid(rel->rd_att, key->keypair[i][pairidx]);
		if (!ri_AttributesEqual(typeid, oldvalue, newvalue))
			return false;
	}

	return true;
}


/* ----------
 * ri_AllKeysUnequal -
 *
 *	Check if all key values in OLD and NEW are not equal.
 * ----------
 */
static bool
ri_AllKeysUnequal(Relation rel, HeapTuple oldtup, HeapTuple newtup,
				  RI_QueryKey *key, int pairidx)
{
	int			i;
	Oid			typeid;
	Datum		oldvalue;
	Datum		newvalue;
	bool		isnull;
	bool		keys_unequal;

	keys_unequal = true;
	for (i = 0; keys_unequal && i < key->nkeypairs; i++)
	{
		/* ----------
		 * Get one attributes oldvalue. If it is NULL - they're not equal.
		 * ----------
		 */
		oldvalue = SPI_getbinval(oldtup, rel->rd_att,
								 key->keypair[i][pairidx], &isnull);
		if (isnull)
			continue;

		/* ----------
		 * Get one attributes oldvalue. If it is NULL - they're not equal.
		 * ----------
		 */
		newvalue = SPI_getbinval(newtup, rel->rd_att,
								 key->keypair[i][pairidx], &isnull);
		if (isnull)
			continue;

		/* ----------
		 * Get the attributes type OID and call the '=' operator
		 * to compare the values.
		 * ----------
		 */
		typeid = SPI_gettypeid(rel->rd_att, key->keypair[i][pairidx]);
		if (!ri_AttributesEqual(typeid, oldvalue, newvalue))
			continue;
		keys_unequal = false;
	}

	return keys_unequal;
}


/* ----------
 * ri_OneKeyEqual -
 *
 *	Check if one key value in OLD and NEW is equal.
 *
 *	ri_KeysEqual could call this but would run a bit slower.  For
 *	now, let's duplicate the code.
 * ----------
 */
static bool
ri_OneKeyEqual(Relation rel, int column, HeapTuple oldtup, HeapTuple newtup,
			   RI_QueryKey *key, int pairidx)
{
	Oid			typeid;
	Datum		oldvalue;
	Datum		newvalue;
	bool		isnull;

	/* ----------
	 * Get one attributes oldvalue. If it is NULL - they're not equal.
	 * ----------
	 */
	oldvalue = SPI_getbinval(oldtup, rel->rd_att,
							 key->keypair[column][pairidx], &isnull);
	if (isnull)
		return false;

	/* ----------
	 * Get one attributes oldvalue. If it is NULL - they're not equal.
	 * ----------
	 */
	newvalue = SPI_getbinval(newtup, rel->rd_att,
							 key->keypair[column][pairidx], &isnull);
	if (isnull)
		return false;

	/* ----------
	 * Get the attributes type OID and call the '=' operator
	 * to compare the values.
	 * ----------
	 */
	typeid = SPI_gettypeid(rel->rd_att, key->keypair[column][pairidx]);
	if (!ri_AttributesEqual(typeid, oldvalue, newvalue))
		return false;

	return true;
}


/* ----------
 * ri_AttributesEqual -
 *
 *	Call the type specific '=' operator comparision function
 *	for two values.
 * ----------
 */
static bool
ri_AttributesEqual(Oid typeid, Datum oldvalue, Datum newvalue)
{
	RI_OpreqHashEntry *entry;
	bool		found;
	Datum		result;

	/* ----------
	 * On the first call initialize the hashtable
	 * ----------
	 */
	if (!ri_query_cache)
		ri_InitHashTables();

	/* ----------
	 * Try to find the '=' operator for this type in our cache
	 * ----------
	 */
	entry = (RI_OpreqHashEntry *) hash_search(ri_opreq_cache,
									(char *) &typeid, HASH_FIND, &found);

	/* ----------
	 * If not found, lookup the OPERNAME system cache for it
	 * and remember that info.
	 * ----------
	 */
	if (!found)
	{
		HeapTuple	opr_tup;
		Form_pg_operator opr_struct;

		opr_tup = SearchSysCacheTuple(OPERNAME,
									  PointerGetDatum("="),
									  ObjectIdGetDatum(typeid),
									  ObjectIdGetDatum(typeid),
									  CharGetDatum('b'));

		if (!HeapTupleIsValid(opr_tup))
			elog(ERROR, "ri_AttributesEqual(): cannot find '=' operator "
				 "for type %ld", typeid);
		opr_struct = (Form_pg_operator) GETSTRUCT(opr_tup);

		entry = (RI_OpreqHashEntry *) hash_search(ri_opreq_cache,
								   (char *) &typeid, HASH_ENTER, &found);
		if (entry == NULL)
			elog(FATAL, "can't insert into RI operator cache");

		entry->oprfnid = opr_struct->oprcode;
		memset(&(entry->oprfmgrinfo), 0, sizeof(FmgrInfo));
	}

	/* ----------
	 * Call the type specific '=' function
	 * ----------
	 */
	fmgr_info(entry->oprfnid, &(entry->oprfmgrinfo));
	result = PointerGetDatum(FMGR_PTR2(&entry->oprfmgrinfo, oldvalue, newvalue));
	return (bool) result;
}
