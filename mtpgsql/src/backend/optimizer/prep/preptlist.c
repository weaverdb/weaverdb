/*-------------------------------------------------------------------------
 *
 * preptlist.c
 *	  Routines to preprocess the parse tree target list
 *
 * This module takes care of altering the query targetlist as needed for
 * INSERT, UPDATE, and DELETE queries.	For INSERT and UPDATE queries,
 * the targetlist must contain an entry for each attribute of the target
 * relation in the correct order.  For both UPDATE and DELETE queries,
 * we need a junk targetlist entry holding the CTID attribute --- the
 * executor relies on this to find the tuple to be replaced/deleted.
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * IDENTIFICATION
 *	  $Header: /cvs/weaver/mtpgsql/src/backend/optimizer/prep/preptlist.c,v 1.1.1.1 2006/08/12 00:20:53 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"
#include "env/env.h"
#include "access/heapam.h"
#include "catalog/pg_type.h"
#include "nodes/makefuncs.h"
#include "optimizer/prep.h"
#include "parser/parsetree.h"
#include "utils/lsyscache.h"


static List *expand_targetlist(List *tlist, int command_type,
				  Index result_relation, List *range_table);


/*
 * preprocess_targetlist
 *	  Driver for preprocessing the parse tree targetlist.
 *
 *	  Returns the new targetlist.
 */
List *
preprocess_targetlist(List *tlist,
					  int command_type,
					  Index result_relation,
					  List *range_table)
{

	/*
	 * for heap_formtuple to work, the targetlist must match the exact
	 * order of the attributes. We also need to fill in any missing
	 * attributes.							-ay 10/94
	 */
	if (command_type == CMD_PUT || command_type == CMD_INSERT || command_type == CMD_UPDATE)
		tlist = expand_targetlist(tlist, command_type,
								  result_relation, range_table);

	/*
	 * for "update" and "delete" queries, add ctid of the result relation
	 * into the target list so that the ctid will propagate through
	 * execution and ExecutePlan() will be able to identify the right
	 * tuple to replace or delete.	This extra field is marked "junk" so
	 * that it is not stored back into the tuple.
	 */
	if (command_type == CMD_UPDATE || command_type == CMD_DELETE)
	{
		Resdom	   *resdom;
		Var		   *var;

		resdom = makeResdom(length(tlist) + 1,
							TIDOID,
							-1,
							pstrdup("ctid"),
							0,
							0,
							true);

		var = makeVar(result_relation, SelfItemPointerAttributeNumber,
					  TIDOID, -1, 0);

		/*
		 * For an UPDATE, expand_targetlist already created a fresh tlist.
		 * For DELETE, better do a listCopy so that we don't destructively
		 * modify the original tlist (is this really necessary?).
		 */
		if (command_type == CMD_DELETE)
			tlist = listCopy(tlist);

		tlist = lappend(tlist, makeTargetEntry(resdom, (Node *) var));
	}

	return tlist;
}

/*****************************************************************************
 *
 *		TARGETLIST EXPANSION
 *
 *****************************************************************************/

/*
 * expand_targetlist
 *	  Given a target list as generated by the parser and a result relation,
 *	  add targetlist entries for any missing attributes, and order the
 *	  non-junk attributes in proper field order.
 */
static List *
expand_targetlist(List *tlist, int command_type,
				  Index result_relation, List *range_table)
{
	int			old_tlist_len = length(tlist);
	List	   *new_tlist = NIL;
	bool	   *tlistentry_used;
	Relation	rel;
	int			attrno,
				numattrs,
				old_tlist_index;
	List	   *temp;

	/*
	 * Keep a map of which tlist items we have transferred to new list. +1
	 * here keeps palloc from complaining if old_tlist_len=0.
	 */
	tlistentry_used = (bool *) palloc((old_tlist_len + 1) * sizeof(bool));
	memset(tlistentry_used, 0, (old_tlist_len + 1) * sizeof(bool));

	/*
	 * Scan the tuple description in the relation's relcache entry to make
	 * sure we have all the user attributes in the right order.
	 */
	rel = heap_open(getrelid(result_relation, range_table), AccessShareLock);

	numattrs = RelationGetNumberOfAttributes(rel);

	for (attrno = 1; attrno <= numattrs; attrno++)
	{
		Form_pg_attribute att_tup = rel->rd_att->attrs[attrno - 1];
		char	   *attrname = NameStr(att_tup->attname);
		TargetEntry *new_tle = NULL;

		/*
		 * We match targetlist entries to attributes using the resname.
		 */
		old_tlist_index = 0;
		foreach(temp, tlist)
		{
			TargetEntry *old_tle = (TargetEntry *) lfirst(temp);
			Resdom	   *resdom = old_tle->resdom;

			if (!tlistentry_used[old_tlist_index] &&
				strcmp(resdom->resname, attrname) == 0 &&
				!resdom->resjunk)
			{

				/*
				 * We can recycle the old TLE+resdom if right resno; else
				 * make a new one to avoid modifying the old tlist
				 * structure. (Is preserving old tlist actually
				 * necessary?)
				 */
				if (resdom->resno == attrno)
					new_tle = old_tle;
				else
				{
					resdom = (Resdom *) copyObject((Node *) resdom);
					resdom->resno = attrno;
					new_tle = makeTargetEntry(resdom, old_tle->expr);
				}
				tlistentry_used[old_tlist_index] = true;
				break;
			}
			old_tlist_index++;
		}

		if (new_tle == NULL)
		{

			/*
			 * Didn't find a matching tlist entry, so make one.
			 *
			 * For INSERT, generate a constant of the default value for the
			 * attribute type, or NULL if no default value.
			 *
			 * For UPDATE, generate a Var reference to the existing value of
			 * the attribute, so that it gets copied to the new tuple.
			 */
			Oid			atttype = att_tup->atttypid;
			int32		atttypmod = att_tup->atttypmod;

			switch (command_type)
			{
				case CMD_PUT:
				case CMD_INSERT:
					{
#ifdef	_DROP_COLUMN_HACK__
						Datum		typedefault;

#else
						Datum		typedefault = get_typdefault(atttype);

#endif	 /* _DROP_COLUMN_HACK__ */
						int			typlen;
						Const	   *temp_const;

#ifdef	_DROP_COLUMN_HACK__
						if (COLUMN_IS_DROPPED(att_tup))
							typedefault = PointerGetDatum(NULL);
						else
							typedefault = get_typdefault(atttype);
#endif	 /* _DROP_COLUMN_HACK__ */
						if (typedefault == PointerGetDatum(NULL))
							typlen = 0;
						else
						{

							/*
							 * Since this is an append or replace, the
							 * size of any set attribute is the size of
							 * the OID used to represent it.
							 */
							if (att_tup->attisset)
								typlen = get_typlen(OIDOID);
							else
								typlen = get_typlen(atttype);
						}

						temp_const = makeConst(atttype,
											   typlen,
											   typedefault,
								  (typedefault == PointerGetDatum(NULL)),
											   false,
											   false,	/* not a set */
											   false);

						new_tle = makeTargetEntry(makeResdom(attrno,
															 atttype,
															 -1,
													   pstrdup(attrname),
															 0,
															 (Oid) 0,
															 false),
												  (Node *) temp_const);
						break;
					}
				case CMD_UPDATE:
					{
						Var		   *temp_var;

#ifdef	_DROP_COLUMN_HACK__
						Node	   *temp_node = (Node *) NULL;

						if (COLUMN_IS_DROPPED(att_tup))
						{
							temp_node = (Node *) makeConst(atttype, 0,
												   PointerGetDatum(NULL),
														   true,
														   false,
														   false,		/* not a set */
														   false);
						}
						else
#endif	 /* _DROP_COLUMN_HACK__ */
							temp_var = makeVar(result_relation, attrno, atttype,
											   atttypmod, 0);
#ifdef	_DROP_COLUMN_HACK__
						if (!temp_node)
							temp_node = (Node *) temp_var;
#endif	 /* _DROP_COLUMN_HACK__ */

						new_tle = makeTargetEntry(makeResdom(attrno,
															 atttype,
															 atttypmod,
													   pstrdup(attrname),
															 0,
															 (Oid) 0,
															 false),
#ifdef	_DROP_COLUMN_HACK__
												  temp_node);
#else
												  (Node *) temp_var);
#endif	 /* _DROP_COLUMN_HACK__ */
						break;
					}
				default:
					elog(ERROR, "expand_targetlist: unexpected command_type");
					break;
			}
		}

		new_tlist = lappend(new_tlist, new_tle);
	}

	/*
	 * Copy all unprocessed tlist entries to the end of the new tlist,
	 * making sure they are marked resjunk = true.	Typical junk entries
	 * include ORDER BY or GROUP BY expressions (are these actually
	 * possible in an INSERT or UPDATE?), system attribute references,
	 * etc.
	 */
	old_tlist_index = 0;
	foreach(temp, tlist)
	{
		TargetEntry *old_tle = (TargetEntry *) lfirst(temp);

		if (!tlistentry_used[old_tlist_index])
		{
			Resdom	   *resdom;

			resdom = (Resdom *) copyObject((Node *) old_tle->resdom);
			resdom->resno = attrno++;
			resdom->resjunk = true;
			new_tlist = lappend(new_tlist,
								makeTargetEntry(resdom, old_tle->expr));
		}
		old_tlist_index++;
	}

	heap_close(rel, AccessShareLock);

	pfree(tlistentry_used);

	return new_tlist;
}
