/*-------------------------------------------------------------------------
 *
 * rel.h
 *	  POSTGRES relation descriptor (a/k/a relcache entry) definitions.
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * $Id: rel.h,v 1.2 2007/02/11 00:43:33 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */
#ifndef REL_H
#define REL_H

#include "access/strat.h"
#include "access/tupdesc.h"
#include "catalog/pg_am.h"
#include "catalog/pg_class.h"
#include "rewrite/prs2lock.h"
#include "storage/fd.h"
#include "storage/block.h"
#include "storage/smgr.h"


/* added to prevent circular dependency.  bjm 1999/11/15 */
PG_EXTERN char *get_temp_rel_by_physicalname(const char *relname);

/*
 * LockRelId and LockInfo really belong to lmgr.h, but it's more convenient
 * to declare them here so we can have a LockInfoData field in a Relation.
 */

typedef struct bufenv           *BufferCxt;
typedef struct SnapshotHolder   *SnapshotCxt;

typedef struct LockRelId
{
	Oid			relId;			/* a relation identifier */
	Oid			dbId;			/* a database identifier */
} LockRelId;

typedef struct LockInfoData
{
	LockRelId	lockRelId;
} LockInfoData;

typedef LockInfoData *LockInfo;

/*
 * Likewise, this struct really belongs to trigger.h, but for convenience
 * we put it here.
 */

typedef struct Trigger
{
	Oid			tgoid;
	char	   *tgname;
	Oid			tgfoid;
	FmgrInfo	tgfunc;
	int16		tgtype;
	bool		tgenabled;
	bool		tgisconstraint;
	bool		tgdeferrable;
	bool		tginitdeferred;
	int16		tgnargs;
	int16		tgattr[FUNC_MAX_ARGS];
	char	  **tgargs;
} Trigger;

typedef struct TriggerDesc
{
	/* index data to identify which triggers are which */
	uint16		n_before_statement[4];
	uint16		n_before_row[4];
	uint16		n_after_row[4];
	uint16		n_after_statement[4];
	Trigger   **tg_before_statement[4];
	Trigger   **tg_before_row[4];
	Trigger   **tg_after_row[4];
	Trigger   **tg_after_statement[4];
	/* the actual array of triggers is here */
	Trigger    *triggers;
	int			numtriggers;
} TriggerDesc;

/*
 * Here are the contents of a relation cache entry.
 */

typedef struct RelationData* Relation;

typedef int (*trigger_func)(Relation, void*);

typedef enum when {
    TRIGGER_READ,
    TRIGGER_COMMIT
} TriggerWhen;

typedef struct BufferTrigger {
    TriggerWhen         when;
    trigger_func        call;
    void*               args;
} BufferTrigger;

typedef struct RelationData
{
	SmgrInfo        rd_smgr;		/* open file descriptor */
        long            rd_nblocks;
	uint16		rd_refcnt;		/* reference count */
	bool		rd_myxactonly;	/* rel uses the local buffer mgr */
	bool		rd_isnailed;	/* rel is nailed in cache */
	bool		rd_isnoname;	/* rel has no name */
	bool		rd_unlinked;	/* rel already unlinked or not created yet */
	bool		rd_indexfound;	/*  cached copy of the index list */
	Form_pg_am	rd_am;			/* AM tuple */
	Form_pg_class   rd_rel;		/* RELATION tuple */
	Oid		rd_id;			/* relation's object id */
	LockInfoData    rd_lockInfo;	/* lock manager's info for locking
								 * relation */
	TupleDesc	rd_att;			/* tuple descriptor */
        BufferCxt       buffer_cxt;             /*  cache of the buffer context  */
        SnapshotCxt     snapshot_cxt;             /*  cache of the snapshot context  */
	RuleLock   *    rd_rules;		/* rewrite rules */
	List*		rd_indexlist;
	IndexStrategy   rd_istrat;
	RegProcedure *  rd_support;
	TriggerDesc *   trigdesc;		/* Trigger info, or NULL if rel has none */
        BufferTrigger * readtrigger;
} RelationData;



/* ----------------
 *		RelationPtr is used in the executor to support index scans
 *		where we have to keep track of several index relations in an
 *		array.	-cim 9/10/89
 * ----------------
 */
typedef Relation *RelationPtr;


/*
 * RelationIsValid
 *		True iff relation descriptor is valid.
 */
#define RelationIsValid(relation) PointerIsValid(relation)

#define InvalidRelation ((Relation) NULL)

/*
 * RelationHasReferenceCountZero
 *		True iff relation reference count is zero.
 *
 * Note:
 *		Assumes relation descriptor is valid.
 */
#define RelationHasReferenceCountZero(relation) \
		((bool)((relation)->rd_refcnt == 0))

/*
 * RelationSetReferenceCount
 *		Sets relation reference count.
 */
#define RelationSetReferenceCount(relation,count) ((relation)->rd_refcnt = (count))

/*
 * RelationIncrementReferenceCount
 *		Increments relation reference count.
 */
#define RelationIncrementReferenceCount(relation) ((relation)->rd_refcnt += 1)

/*
 * RelationDecrementReferenceCount
 *		Decrements relation reference count.
 */
#define RelationDecrementReferenceCount(relation) \
	(AssertMacro((relation)->rd_refcnt > 0), \
	 (relation)->rd_refcnt -= 1)

/*
 * RelationGetForm
 *		Returns pg_class tuple for a relation.
 *
 * Note:
 *		Assumes relation descriptor is valid.
 */
#define RelationGetForm(relation) ((relation)->rd_rel)

/*
 * RelationGetRelid
 *
 *	returns the OID of the relation
 */
#define RelationGetRelid(relation) ((relation)->rd_id)

/*
 * RelationGetRelationName
 *
 *	  Returns a Relation Name
 */
#define RelationGetRelationName(relation) \
(\
	(strncmp(RelationGetPhysicalRelationName(relation), \
	 "pg_temp.", strlen("pg_temp.")) != 0) \
	? \
		RelationGetPhysicalRelationName(relation) \
	: \
		get_temp_rel_by_physicalname( \
			RelationGetPhysicalRelationName(relation)) \
)

/*
 * RelationGetPhysicalRelationName
 *
 *	  Returns a Relation Name
 */
#define RelationGetPhysicalRelationName(relation) (NameStr((relation)->rd_rel->relname))

/*
 * RelationGetNumberOfAttributes
 *
 *	  Returns the number of attributes.
 */
#define RelationGetNumberOfAttributes(relation) ((relation)->rd_rel->relnatts)

/*
 * RelationGetDescr
 *		Returns tuple descriptor for a relation.
 */
#define RelationGetDescr(relation) ((relation)->rd_att)

/*
 * RelationGetIndexStrategy
 *		Returns index strategy for a relation.
 *
 * Note:
 *		Assumes relation descriptor is valid.
 *		Assumes relation descriptor is for an index relation.
 */
#define RelationGetIndexStrategy(relation) ((relation)->rd_istrat)

#define RelationGetBufferCxt(relation) ((relation)->buffer_cxt)
#define RelationGetSnapshotCxt(relation) ((relation)->snapshot_cxt)


PG_EXTERN void RelationSetIndexSupport(Relation relation,
						IndexStrategy strategy,
						RegProcedure *support);

#endif	 /* REL_H */
