/*-------------------------------------------------------------------------
 *
 * temprel.c
 *	  POSTGRES temporary relation handling
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  $Header: /cvs/weaver/mtpgsql/src/backend/utils/cache/temprel.c,v 1.1.1.1 2006/08/12 00:21:58 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */

/*
 * This implements temp tables by modifying the relname cache lookups
 * of pg_class.
 * When a temp table is created, a linked list of temp table tuples is
 * stored here.  When a relname cache lookup is done, references to user-named
 * temp tables are converted to the internal temp table names.
 */

#include <sys/types.h>
#include "postgres.h"
#include "env/env.h"
#include "access/heapam.h"
#include "access/xact.h"
#include "catalog/heap.h"
#include "catalog/index.h"
#include "utils/catcache.h"
#include "utils/relcache.h"
#include "utils/temprel.h"

#ifdef GLOBALCACHE
extern MemoryContext CacheCxt
#endif



/* ----------------
 *		global variables
 * ----------------
 */

typedef struct TempTable
{
	char	   *user_relname;
	char	   *relname;
	Oid			relid;
	char		relkind;
	TransactionId xid;
} TempTable;

typedef struct TempGlobals {
/*  from temprel.c  */
	List* 			temp_rels;
} TempGlobals;

static SectionId temp_id = SECTIONID("TMPR");

#ifdef TLS
TLS TempGlobals*  temp_globals = NULL;
#else
#define temp_globals GetEnv()->temp_globals
#endif

static TempGlobals* GetTempGlobals(void);





void
create_temp_relation(const char *relname, HeapTuple pg_class_tuple)
{
	MemoryContext oldcxt;
	TempTable  *temp_rel;
	TempGlobals* temps = GetTempGlobals();

	oldcxt = MemoryContextSwitchTo(RelationGetCacheContext());

	temp_rel = palloc(sizeof(TempTable));
	temp_rel->user_relname = palloc(NAMEDATALEN);
	temp_rel->relname = palloc(NAMEDATALEN);

	/* save user-supplied name */
	strcpy(temp_rel->user_relname, relname);
	StrNCpy(temp_rel->relname, NameStr(((Form_pg_class)
					  GETSTRUCT(pg_class_tuple))->relname), NAMEDATALEN);
	temp_rel->relid = pg_class_tuple->t_data->t_oid;
	temp_rel->relkind = ((Form_pg_class) GETSTRUCT(pg_class_tuple))->relkind;
	temp_rel->xid = GetCurrentTransactionId();

        MemoryContextSwitchTo(MemoryContextGetEnv()->TopTransactionContext);

	temps->temp_rels = lcons(temp_rel, temps->temp_rels);

	MemoryContextSwitchTo(oldcxt);
}

void
remove_all_temp_relations(void)
{
	List	   *l,
			   *next;
	TempGlobals* temps = GetTempGlobals();

	if (temps->temp_rels == NIL)
		return;
/*
	SetAbortOnly();
	CommitTransactionCommand();
	StartTransactionCommand();
*/
	l = temps->temp_rels;
	while (l != NIL)
	{
		TempTable  *temp_rel = lfirst(l);

		next = lnext(l);		/* do this first, l is deallocated */

		if (temp_rel->relkind != RELKIND_INDEX)
		{
			char		relname[NAMEDATALEN];

			/* safe from deallocation */
			strcpy(relname, temp_rel->user_relname);
			heap_drop_with_catalog(relname);
		}
		else
			index_drop(temp_rel->relid);

		l = next;
	}
	temps->temp_rels = NIL;
/*
	CommitTransactionCommand();
*/
}

/* we don't have the relname for indexes, so we just pass the oid */
void
remove_temp_relation(Oid relid)
{

	MemoryContext oldcxt;
	List	   *l,
			   *prev;
	TempGlobals* temps = GetTempGlobals();

	prev = NIL;
	l = temps->temp_rels;
	while (l != NIL)
	{
		TempTable  *temp_rel = lfirst(l);

		if (temp_rel->relid == relid)
		{
			pfree(temp_rel->user_relname);
			pfree(temp_rel->relname);
			pfree(temp_rel);
			/* remove from linked list */
			if (prev != NIL)
			{
				lnext(prev) = lnext(l);
				pfree(l);
				l = lnext(prev);
			}
			else
			{
				temps->temp_rels = lnext(l);
				pfree(l);
				l = temps->temp_rels;
			}
		}
		else
		{
			prev = l;
			l = lnext(l);
		}
	}

}

/* remove entries from aborted transactions */
void
invalidate_temp_relations(void)
{
	MemoryContext oldcxt;
	List	   *l,
			   *prev;
	TempGlobals* temps = GetTempGlobals();

	TransactionId 	xid = GetCurrentTransactionId();

	prev = NIL;
	l = temps->temp_rels;
	while (l != NIL)
	{
		TempTable  *temp_rel = lfirst(l);

		if (temp_rel->xid == xid)
		{
			pfree(temp_rel->user_relname);
			pfree(temp_rel->relname);
			pfree(temp_rel);
			/* remove from linked list */
			if (prev != NIL)
			{
				lnext(prev) = lnext(l);
				pfree(l);
				l = lnext(prev);
			}
			else
			{
				temps->temp_rels = lnext(l);
				pfree(l);
				l = temps->temp_rels;
			}
		}
		else
		{
			prev = l;
			l = lnext(l);
		}

	}
}

char *
get_temp_rel_by_username(const char *user_relname)
{
	List	   *l;
	TempGlobals* temps = GetTempGlobals();

	foreach(l, temps->temp_rels)
	{
		TempTable  *temp_rel = lfirst(l);

		if (strcmp(temp_rel->user_relname, user_relname) == 0)
			return pstrdup(temp_rel->relname);
	}
	return NULL;
}

char *
get_temp_rel_by_physicalname(const char *relname)
{
	List	   *l;
	TempGlobals* temps = GetTempGlobals();

	foreach(l, temps->temp_rels)
	{
		TempTable  *temp_rel = lfirst(l);

		if (strcmp(temp_rel->relname, relname) == 0)
			return temp_rel->user_relname;
	}
	/* needed for bootstrapping temp tables */
	return pstrdup(relname);
}

 
TempGlobals*
GetTempGlobals(void)
{
    TempGlobals* info = temp_globals;
    if ( info == NULL ) {
        info = AllocateEnvSpace(temp_id,sizeof(TempGlobals));
	memset(info,0x00,sizeof(TempGlobals));
        temp_globals = info;
    }
    return info;
}
