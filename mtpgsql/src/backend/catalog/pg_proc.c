/*-------------------------------------------------------------------------
 *
 * pg_proc.c
 *	  routines to support manipulation of the pg_proc relation
 *
 * Portions Copyright (c) 2000-2024, Myron Scott  <myron@weaverdb.org>
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "nodes/pg_list.h"
#include "access/heapam.h"
#include "catalog/catname.h"
#include "catalog/indexing.h"
#include "catalog/pg_proc.h"
#include "catalog/pg_type.h"
#include "miscadmin.h"
#include "optimizer/planner.h"
#include "parser/parse_type.h"
#include "tcop/tcopprot.h"
#include "utils/builtins.h"
#include "utils/fmgrtab.h"
#include "utils/lsyscache.h"
#include "utils/sets.h"
#include "utils/syscache.h"


/* ----------------------------------------------------------------
 *		ProcedureCreate
 * ----------------------------------------------------------------
 */
Oid
ProcedureCreate(char *procedureName,
				bool returnsSet,
				char *returnTypeName,
				char *languageName,
				char *prosrc,
				char *probin,
				bool canCache,
				bool trusted,
				int32 byte_pct,
				int32 perbyte_cpu,
				int32 percall_cpu,
				int32 outin_ratio,
				List *argList)
{
	int			i;
	Relation	rel;
	HeapTuple	tup;
	bool		defined;
	uint16		parameterCount;
	char		nulls[Natts_pg_proc];
	Datum		values[Natts_pg_proc];
	Oid			languageObjectId;
	Oid			typeObjectId;
	List	   *x;
	List	   *querytree_list;
	Oid			typev[FUNC_MAX_ARGS];
	Oid			relid;
	Oid			toid;
	NameData	procname;
	TupleDesc	tupDesc;


	/* ----------------
	 *	sanity checks
	 * ----------------
	 */
	Assert(PointerIsValid(prosrc));
	Assert(PointerIsValid(probin));

	parameterCount = 0;
	MemSet(typev, 0, FUNC_MAX_ARGS * sizeof(Oid));
	foreach(x, argList)
	{
		Value	   *t = lfirst(x);

		if (parameterCount >= FUNC_MAX_ARGS)
			elog(ERROR, "Procedures cannot take more than %d arguments",
				 FUNC_MAX_ARGS);

		if (strcmp(strVal(t), "opaque") == 0)
		{
			if (strcmp(languageName, "sql") == 0)
				elog(ERROR, "ProcedureCreate: sql functions cannot take type \"opaque\"");
			toid = 0;
		}
		else
		{
			toid = TypeGet(strVal(t), &defined);

			if (!OidIsValid(toid))
			{
				elog(ERROR, "ProcedureCreate: arg type '%s' is not defined",
					 strVal(t));
			}

			if (!defined)
			{
				elog(NOTICE, "ProcedureCreate: arg type '%s' is only a shell",
					 strVal(t));
			}
		}

		typev[parameterCount++] = toid;
	}

	tup = SearchSysCacheTuple(PROCNAME,
							  PointerGetDatum(procedureName),
							  UInt16GetDatum(parameterCount),
							  PointerGetDatum(typev),
							  0);

	if (HeapTupleIsValid(tup))
		elog(ERROR, "ProcedureCreate: procedure %s already exists with same arguments",
			 procedureName);

	if (!strcmp(languageName, "sql"))
	{

		/*
		 * If this call is defining a set, check if the set is already
		 * defined by looking to see whether this call's function text
		 * matches a function already in pg_proc.  If so just return the
		 * OID of the existing set.
		 */
		if (!strcmp(procedureName, GENERICSETNAME))
		{
#ifdef SETS_FIXED
			/* ----------
			 * The code below doesn't work any more because the
			 * PROSRC system cache and the pg_proc_prosrc_index
			 * have been removed. Instead a sequential heap scan
			 * or something better must get implemented. The reason
			 * for removing is that nbtree index crashes if sources
			 * exceed 2K what's likely for procedural languages.
			 *
			 * 1999/09/30 Jan
			 * ----------
			 */
			text	   *prosrctext;

			prosrctext = textin(prosrc);
			tup = SearchSysCacheTuple(PROSRC,
									  PointerGetDatum(prosrctext),
									  0, 0, 0);
			pfree(prosrctext);
			if (HeapTupleIsValid(tup))
				return tup->t_data->t_oid;
#else
			elog(ERROR, "lookup for procedure by source needs fix (Jan)");
#endif	 /* SETS_FIXED */
		}
	}

	tup = SearchSysCacheTuple(LANGNAME,
							  PointerGetDatum(languageName),
							  0, 0, 0);

	if (!HeapTupleIsValid(tup))
		elog(ERROR, "ProcedureCreate: no such language %s", languageName);

	languageObjectId = tup->t_data->t_oid;

	if (strcmp(returnTypeName, "opaque") == 0)
	{
		if (strcmp(languageName, "sql") == 0)
			elog(ERROR, "ProcedureCreate: sql functions cannot return type \"opaque\"");
		typeObjectId = 0;
	}
	else
	{
		typeObjectId = TypeGet(returnTypeName, &defined);

		if (!OidIsValid(typeObjectId))
		{
			elog(NOTICE, "ProcedureCreate: type '%s' is not yet defined",
				 returnTypeName);
#ifdef NOT_USED
			elog(NOTICE, "ProcedureCreate: creating a shell for type '%s'",
				 returnTypeName);
#endif
			typeObjectId = TypeShellMake(returnTypeName);
			if (!OidIsValid(typeObjectId))
			{
				elog(ERROR, "ProcedureCreate: could not create type '%s'",
					 returnTypeName);
			}
		}
		else if (!defined)
		{
			elog(NOTICE, "ProcedureCreate: return type '%s' is only a shell",
				 returnTypeName);
		}
	}

	/*
	 * don't allow functions of complex types that have the same name as
	 * existing attributes of the type
	 */
	if (parameterCount == 1 &&
		(toid = TypeGet(strVal(lfirst(argList)), &defined)) &&
		defined &&
		(relid = typeidTypeRelid(toid)) != 0 &&
		get_attnum(relid, procedureName) != InvalidAttrNumber)
		elog(ERROR, "method %s already an attribute of type %s",
			 procedureName, strVal(lfirst(argList)));


	/*
	 * If this is a postquel procedure, we parse it here in order to be
	 * sure that it contains no syntax errors.	We should store the plan
	 * in an Inversion file for use later, but for now, we just store the
	 * procedure's text in the prosrc attribute.
	 */

	if (strcmp(languageName, "sql") == 0)
	{
		querytree_list = pg_parse_and_rewrite(prosrc, typev,NULL, parameterCount,
											  FALSE);
		/* typecheck return value */
		pg_checkretval(typeObjectId, querytree_list);
	}

	/*
	 * If this is an internal procedure, check that the given internal
	 * function name (the 'prosrc' value) is a known builtin function.
	 *
	 * NOTE: in Postgres versions before 6.5, the SQL name of the created
	 * function could not be different from the internal name, and
	 * 'prosrc' wasn't used.  So there is code out there that does CREATE
	 * FUNCTION xyz AS '' LANGUAGE 'internal'.	To preserve some modicum
	 * of backwards compatibility, accept an empty 'prosrc' value as
	 * meaning the supplied SQL function name.
	 */

	if (strcmp(languageName, "internal") == 0)
	{
		if (strlen(prosrc) == 0)
			prosrc = procedureName;
		if (fmgr_lookupByName(prosrc) == (func_ptr) NULL)
			elog(ERROR,
			"ProcedureCreate: there is no builtin function named \"%s\"",
				 prosrc);
	}

	/*
	 * All seems OK; prepare the tuple to be inserted into pg_proc.
	 */

	for (i = 0; i < Natts_pg_proc; ++i)
	{
		nulls[i] = ' ';
		values[i] = (Datum) NULL;
	}

	i = 0;
	namestrcpy(&procname, procedureName);
	values[i++] = NameGetDatum(&procname);
	values[i++] = Int32GetDatum(GetUserId());
	values[i++] = ObjectIdGetDatum(languageObjectId);
	/* XXX isinherited is always false for now */
	values[i++] = Int8GetDatum((bool) 0);
	values[i++] = Int8GetDatum(trusted);
	values[i++] = Int8GetDatum(canCache);
	values[i++] = UInt16GetDatum(parameterCount);
	values[i++] = Int8GetDatum(returnsSet);
	values[i++] = ObjectIdGetDatum(typeObjectId);
	values[i++] = (Datum) typev;
	values[i++] = Int32GetDatum(byte_pct);		/* probyte_pct */
	values[i++] = Int32GetDatum(perbyte_cpu);	/* properbyte_cpu */
	values[i++] = Int32GetDatum(percall_cpu);	/* propercall_cpu */
	values[i++] = Int32GetDatum(outin_ratio);	/* prooutin_ratio */
	values[i++] = (Datum) fmgr(F_TEXTIN, prosrc);		/* prosrc */
	values[i++] = (Datum) fmgr(F_TEXTIN, probin);		/* probin */

	rel = heap_openr(ProcedureRelationName, RowExclusiveLock);

	tupDesc = rel->rd_att;
	tup = heap_formtuple(tupDesc,
						 values,
						 nulls);

	heap_insert(rel, tup);

	if (RelationGetForm(rel)->relhasindex)
	{
		Relation	idescs[Num_pg_proc_indices];

		CatalogOpenIndices(Num_pg_proc_indices, Name_pg_proc_indices, idescs);
		CatalogIndexInsert(idescs, Num_pg_proc_indices, rel, tup);
		CatalogCloseIndices(Num_pg_proc_indices, idescs);
	}
	heap_close(rel, RowExclusiveLock);
	return tup->t_data->t_oid;
}
