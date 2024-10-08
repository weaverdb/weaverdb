/*-------------------------------------------------------------------------
 *
 * inval.c
 *	  POSTGRES cache invalidation dispatcher code.
 *
 * Portions Copyright (c) 2000-2024, Myron Scott  <myron@weaverdb.org>
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *
 *
 * Note - this code is real crufty...
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"
#include "env/env.h"

#include "catalog/catalog.h"
#include "catalog/catname.h"
#include "catalog/heap.h"
#include "catalog/pg_class.h"
#include "miscadmin.h"
#include "storage/sinval.h"
#include "utils/catcache.h"
#include "utils/inval.h"
#include "utils/relcache.h"


/* ----------------
 *		private invalidation structures
 * ----------------
 */
typedef struct InvalidationUserData
{
	struct InvalidationUserData *dataP[1];		/* VARIABLE LENGTH */
} InvalidationUserData;			/* VARIABLE LENGTH STRUCTURE */

typedef struct InvalidationEntryData
{
	InvalidationUserData *nextP;
	InvalidationUserData userData;		/* VARIABLE LENGTH ARRAY */
} InvalidationEntryData;		/* VARIABLE LENGTH STRUCTURE */

typedef Pointer InvalidationEntry;

typedef InvalidationEntry LocalInvalid;

#define EmptyLocalInvalid		NULL

typedef struct CatalogInvalidationData
{
	Index		cacheId;
	Index		hashIndex;
	ItemPointerData pointerData;
} CatalogInvalidationData;

typedef struct RelationInvalidationData
{
	Oid			relationId;
	Oid			objectId;
} RelationInvalidationData;

typedef union AnyInvalidation
{
	CatalogInvalidationData catalog;
	RelationInvalidationData relation;
} AnyInvalidation;

typedef struct InvalidationMessageData
{
	char		kind;
	AnyInvalidation any;
} InvalidationMessageData;

typedef InvalidationMessageData *InvalidationMessage;

/*  invalidation states   */
typedef struct InvalidationInfo {
	LocalInvalid 		InvalidForall;
	LocalInvalid 		InvalidLocal;
	LocalInvalid 		RollbackStack;
} InvalidationInfo;

static InvalidationInfo* GetInvalidationInfo();

static SectionId inval_id = SECTIONID("INVL");

#ifdef TLS
TLS InvalidationInfo* invalidation_globals = NULL;
#else
#define invalidation_globals GetEnv()->invalidation_globals
#endif
/* ----------------
 *		variables and macros
 * ----------------
 */

static InvalidationInfo*
GetInvalidationInfo(void)
{
    InvalidationInfo* info = invalidation_globals;
    if ( info == NULL ) {
        info = AllocateEnvSpace(inval_id,sizeof(InvalidationInfo));
        info->InvalidForall = EmptyLocalInvalid;
        info->InvalidLocal = EmptyLocalInvalid;
        info->RollbackStack = EmptyLocalInvalid;
        invalidation_globals = info;
    }
    return info;
}

/*
 * ----------------
 *	Invalidation info was devided into three parts.
 *	1) shared invalidation to be registerd for all backends
 *	2) local invalidation for the transaction itself
 *	3) rollback information for the transaction itself
 * ----------------
 */

/*
 * head of invalidation linked list for all backends
 * eaten by AtCommit_Cache() in CommitTransaction()
 */
 /*
static LocalInvalid InvalidForall = EmptyLocalInvalid;
*/
/*
 * head of invalidation linked list for the backend itself
 * eaten by AtCommit_LocalCache() in CommandCounterIncrement()
 */
 /*
static LocalInvalid InvalidLocal = EmptyLocalInvalid;
*/
/*
 * head of rollback linked list for the backend itself
 * eaten by AtAbort_Cache() in AbortTransaction()
 */
 /*
static LocalInvalid RollbackStack = EmptyLocalInvalid;
*/

static InvalidationEntry InvalidationEntryAllocate(uint16 size);
static void LocalInvalidInvalidate(LocalInvalid invalid, void (*function) (void*), bool freemember);
static LocalInvalid LocalInvalidRegister(LocalInvalid invalid,
					 InvalidationEntry entry);
static void DiscardInvalidStack(LocalInvalid *invalid);
static void InvalidationMessageRegisterSharedInvalid(void* message);


/* ----------------------------------------------------------------
 *				"local" invalidation support functions
 * ----------------------------------------------------------------
 */

/* --------------------------------
 *		InvalidationEntryAllocate
 *				Allocates an invalidation entry.
 * --------------------------------
 */
static InvalidationEntry
InvalidationEntryAllocate(uint16 size)
{
	InvalidationEntryData *entryDataP;

	entryDataP = (InvalidationEntryData *)
		MemoryContextAlloc(MemoryContextGetEnv()->TopTransactionContext,(sizeof(char *) + size));	/* XXX alignment */
        if ( entryDataP == NULL ) {
                        GetEnv()->errorcode = 747;
			elog(ERROR, "Memory exhausted");       
        }             
	entryDataP->nextP = NULL;
	return (Pointer) &entryDataP->userData;
}

/* --------------------------------
 *		LocalInvalidRegister
 *		   Link an invalidation entry into a chain of them.  Really ugly
 *		   coding here.
 * --------------------------------
 */
static LocalInvalid
LocalInvalidRegister(LocalInvalid invalid,
					 InvalidationEntry entry)
{
	Assert(PointerIsValid(entry));

	((InvalidationUserData *) entry)->dataP[-1] =
		(InvalidationUserData *) invalid;

	return entry;
}

/* --------------------------------
 *		LocalInvalidInvalidate
 *				Processes, then frees all entries in a local cache
 *				invalidation list unless freemember parameter is false.
 * --------------------------------
 */
static void
LocalInvalidInvalidate(LocalInvalid invalid, void (*function) (void*), bool freemember)
{
	InvalidationEntryData *entryDataP;

	while (PointerIsValid(invalid))
	{
		entryDataP = (InvalidationEntryData *)
			&((InvalidationUserData *) invalid)->dataP[-1];

		if (PointerIsValid(function))
			(*function) ((Pointer) &entryDataP->userData);

		invalid = (Pointer) entryDataP->nextP;

		if (!freemember)
			continue;
		/* help catch errors */
		entryDataP->nextP = (InvalidationUserData *) NULL;

		pfree((Pointer) entryDataP);
	}
}

static void
DiscardInvalidStack(LocalInvalid *invalid)
{
	LocalInvalid locinv;

	locinv = *invalid;
	*invalid = EmptyLocalInvalid;
	if (locinv)
		LocalInvalidInvalidate(locinv, (void (*) (void*)) NULL, true);
}

/* ----------------------------------------------------------------
 *					  private support functions
 * ----------------------------------------------------------------
 */
/* --------------------------------
 *		CacheIdRegister.......
 *		RelationIdRegister....
 * --------------------------------
 */
#ifdef	INVALIDDEBUG
#define CacheIdRegisterSpecifiedLocalInvalid_DEBUG1 \
elog(DEBUG, "CacheIdRegisterSpecifiedLocalInvalid(%d, %d, [%d, %d])", \
	 cacheId, hashIndex, ItemPointerGetBlockNumber(pointer), \
	 ItemPointerGetOffsetNumber(pointer))
#define CacheIdRegisterLocalInvalid_DEBUG1 \
elog(DEBUG, "CacheIdRegisterLocalInvalid(%d, %d, [%d, %d])", \
	 cacheId, hashIndex, ItemPointerGetBlockNumber(pointer), \
	 ItemPointerGetOffsetNumber(pointer))
#define CacheIdRegisterLocalRollback_DEBUG1 \
elog(DEBUG, "CacheIdRegisterLocalRollback(%d, %d, [%d, %d])", \
	 cacheId, hashIndex, ItemPointerGetBlockNumber(pointer), \
	 ItemPointerGetOffsetNumber(pointer))
#define CacheIdImmediateRegisterSharedInvalid_DEBUG1 \
elog(DEBUG, "CacheIdImmediateRegisterSharedInvalid(%d, %d, [%d, %d])", \
	 cacheId, hashIndex, ItemPointerGetBlockNumber(pointer), \
	 ItemPointerGetOffsetNumber(pointer))
#else
#define CacheIdRegisterSpecifiedLocalInvalid_DEBUG1
#define CacheIdRegisterLocalInvalid_DEBUG1
#define CacheIdRegisterLocalRollback_DEBUG1
#define CacheIdImmediateRegisterSharedInvalid_DEBUG1
#endif	 /* INVALIDDEBUG */

/* --------------------------------
 *		CacheIdRegisterSpecifiedLocalInvalid
 * --------------------------------
 */
static LocalInvalid
CacheIdRegisterSpecifiedLocalInvalid(LocalInvalid invalid,
					 Index cacheId, Index hashIndex, ItemPointer pointer)
{
	InvalidationMessage message;

	/* ----------------
	 *	debugging stuff
	 * ----------------
	 */
	CacheIdRegisterSpecifiedLocalInvalid_DEBUG1;

	/* ----------------
	 *	create a message describing the system catalog tuple
	 *	we wish to invalidate.
	 * ----------------
	 */
	message = (InvalidationMessage)
		InvalidationEntryAllocate(sizeof(InvalidationMessageData));

	message->kind = 'c';
	message->any.catalog.cacheId = cacheId;
	message->any.catalog.hashIndex = hashIndex;

	ItemPointerCopy(pointer, &message->any.catalog.pointerData);

	/* ----------------
	 *	Add message to linked list of unprocessed messages.
	 * ----------------
	 */
	invalid = LocalInvalidRegister(invalid, (InvalidationEntry) message);
	return invalid;
}

/* --------------------------------
 *		CacheIdRegisterLocalInvalid
 * --------------------------------
 */
static void
CacheIdRegisterLocalInvalid(Index cacheId,
							Index hashIndex,
							ItemPointer pointer)
{
        InvalidationInfo* info = GetInvalidationInfo();
	/* ----------------
	 *	debugging stuff
	 * ----------------
	 */
	CacheIdRegisterLocalInvalid_DEBUG1;

	/* ----------------
	 *	Add message to InvalidForall linked list.
	 * ----------------
	 */
	info->InvalidForall = CacheIdRegisterSpecifiedLocalInvalid(info->InvalidForall,
											cacheId, hashIndex, pointer);
	/* ----------------
	 *	Add message to InvalidLocal linked list.
	 * ----------------
	 */
	info->InvalidLocal = CacheIdRegisterSpecifiedLocalInvalid(info->InvalidLocal,
											cacheId, hashIndex, pointer);
}

/* --------------------------------
 *		CacheIdRegisterLocalRollback
 * --------------------------------
 */
static void
CacheIdRegisterLocalRollback(Index cacheId, Index hashIndex,
							 ItemPointer pointer)
{
        InvalidationInfo* info = GetInvalidationInfo();
	/* ----------------
	 *	debugging stuff
	 * ----------------
	 */
	CacheIdRegisterLocalRollback_DEBUG1;

	/* ----------------
	 *	Add message to RollbackStack linked list.
	 * ----------------
	 */
	info->RollbackStack = CacheIdRegisterSpecifiedLocalInvalid(
							 info->RollbackStack, cacheId, hashIndex, pointer);
}

/* --------------------------------
 *		CacheIdImmediateRegisterSharedInvalid
 * --------------------------------
 */
static void
CacheIdImmediateRegisterSharedInvalid(Index cacheId, Index hashIndex,
									  ItemPointer pointer)
{
	InvalidationMessage message;

	/* ----------------
	 *	debugging stuff
	 * ----------------
	 */
	CacheIdImmediateRegisterSharedInvalid_DEBUG1;

	/* ----------------
	 *	create a message describing the system catalog tuple
	 *	we wish to invalidate.
	 * ----------------
	 */
	message = (InvalidationMessage)
		InvalidationEntryAllocate(sizeof(InvalidationMessageData));

	message->kind = 'c';
	message->any.catalog.cacheId = cacheId;
	message->any.catalog.hashIndex = hashIndex;

	ItemPointerCopy(pointer, &message->any.catalog.pointerData);
	/* ----------------
	 *	Register a shared catalog cache invalidation.
	 * ----------------
	 */
	InvalidationMessageRegisterSharedInvalid(message);
	pfree((Pointer) &((InvalidationUserData *) message)->dataP[-1]);
}

/* --------------------------------
 *		RelationIdRegisterSpecifiedLocalInvalid
 * --------------------------------
 */
static LocalInvalid
RelationIdRegisterSpecifiedLocalInvalid(LocalInvalid invalid,
										Oid relationId, Oid objectId)
{
	InvalidationMessage message;

	/* ----------------
	 *	debugging stuff
	 * ----------------
	 */
#ifdef	INVALIDDEBUG
	elog(DEBUG, "RelationRegisterSpecifiedLocalInvalid(%u, %u)", relationId,
		 objectId);
#endif	 /* defined(INVALIDDEBUG) */

	/* ----------------
	 *	create a message describing the relation descriptor
	 *	we wish to invalidate.
	 * ----------------
	 */
	message = (InvalidationMessage)
		InvalidationEntryAllocate(sizeof(InvalidationMessageData));

	message->kind = 'r';
	message->any.relation.relationId = relationId;
	message->any.relation.objectId = objectId;

	/* ----------------
	 *	Add message to linked list of unprocessed messages.
	 * ----------------
	 */
	invalid = LocalInvalidRegister(invalid, (InvalidationEntry) message);
	return invalid;
}

/* --------------------------------
 *		RelationIdRegisterLocalInvalid
 * --------------------------------
 */
static void
RelationIdRegisterLocalInvalid(Oid relationId, Oid objectId)
{
        InvalidationInfo* info = GetInvalidationInfo();
	/* ----------------
	 *	debugging stuff
	 * ----------------
	 */
#ifdef	INVALIDDEBUG
	elog(DEBUG, "RelationRegisterLocalInvalid(%u, %u)", relationId,
		 objectId);
#endif	 /* defined(INVALIDDEBUG) */

	/* ----------------
	 *	Add message to InvalidForall linked list.
	 * ----------------
	 */
	info->InvalidForall = RelationIdRegisterSpecifiedLocalInvalid(info->InvalidForall,
												   relationId, objectId);
	/* ----------------
	 *	Add message to InvalidLocal linked list.
	 * ----------------
	 */
	info->InvalidLocal = RelationIdRegisterSpecifiedLocalInvalid(info->InvalidLocal,
												   relationId, objectId);
}

/* --------------------------------
 *		RelationIdRegisterLocalRollback
 * --------------------------------
 */
static void
RelationIdRegisterLocalRollback(Oid relationId, Oid objectId)
{
        InvalidationInfo* info = GetInvalidationInfo();
	/* ----------------
	 *	debugging stuff
	 * ----------------
	 */
#ifdef	INVALIDDEBUG
	elog(DEBUG, "RelationRegisterLocalRollback(%u, %u)", relationId,
		 objectId);
#endif	 /* defined(INVALIDDEBUG) */

	/* ----------------
	 *	Add message to RollbackStack linked list.
	 * ----------------
	 */
	info->RollbackStack = RelationIdRegisterSpecifiedLocalInvalid(info->RollbackStack, relationId, objectId);
}

/* --------------------------------
 *		RelationIdImmediateRegisterSharedInvalid
 * --------------------------------
 */
static void
RelationIdImmediateRegisterSharedInvalid(Oid relationId, Oid objectId)
{
	InvalidationMessage message;

	/* ----------------
	 *	debugging stuff
	 * ----------------
	 */
#ifdef	INVALIDDEBUG
	elog(DEBUG, "RelationImmediateRegisterSharedInvalid(%u, %u)", relationId,
		 objectId);
#endif	 /* defined(INVALIDDEBUG) */

	/* ----------------
	 *	create a message describing the relation descriptor
	 *	we wish to invalidate.
	 * ----------------
	 */
	message = (InvalidationMessage)
		InvalidationEntryAllocate(sizeof(InvalidationMessageData));

	message->kind = 'r';
	message->any.relation.relationId = relationId;
	message->any.relation.objectId = objectId;

	/* ----------------
	 *	Register a shared catalog cache invalidation.
	 * ----------------
	 */
	InvalidationMessageRegisterSharedInvalid(message);
	pfree((Pointer) &((InvalidationUserData *) message)->dataP[-1]);
}

/* --------------------------------
 *		CacheIdInvalidate
 *
 *		This routine can invalidate a tuple in a system catalog cache
 *		or a cached relation descriptor.  You pay your money and you
 *		take your chances...
 * --------------------------------
 */
#ifdef	INVALIDDEBUG
#define CacheIdInvalidate_DEBUG1 \
elog(DEBUG, "CacheIdInvalidate(%d, %d, 0x%x[%d])", cacheId, hashIndex,\
	 pointer, ItemPointerIsValid(pointer))
#else
#define CacheIdInvalidate_DEBUG1
#endif	 /* defined(INVALIDDEBUG) */

void
CacheIdInvalidate(Index cacheId,
				  Index hashIndex,
				  ItemPointer pointer)
{
	bool processed = false;
	/* ----------------
	 *	assume that if the item pointer is valid, then we are
	 *	invalidating an item in the specified system catalog cache.
	 * ----------------
	 */
	if (ItemPointerIsValid(pointer))
	{
		CatalogCacheIdInvalidate(cacheId, hashIndex, pointer);
		processed = true;
	}

	CacheIdInvalidate_DEBUG1;

	/* ----------------
	 *	if the cacheId is the oid of any of the following system relations,
	 *	then assume we are invalidating a relation descriptor
	 * ----------------
	 */
	if (cacheId == RelOid_pg_class)
	{
		RelationIdInvalidateRelationCache(hashIndex,DEFAULTDBOID);
		processed = true;
	}

	if (cacheId == RelOid_pg_attribute)
	{
		RelationIdInvalidateRelationCache(hashIndex,DEFAULTDBOID);
		processed = true;
	}

	/* ----------------
	 *	Yow! the caller asked us to invalidate something else.
	 * ----------------
	 */
	if (!processed) elog(FATAL, "CacheIdInvalidate: cacheId=%d relation id?", cacheId);
}

/* --------------------------------
 *		InvalidationMessageRegisterSharedInvalid
 * --------------------------------
 */
#ifdef	INVALIDDEBUG
#define InvalidationMessageRegisterSharedInvalid_DEBUG1 \
elog(DEBUG,\
	 "InvalidationMessageRegisterSharedInvalid(c, %d, %d, [%d, %d])",\
	 message->any.catalog.cacheId,\
	 message->any.catalog.hashIndex,\
	 ItemPointerGetBlockNumber(&message->any.catalog.pointerData),\
	 ItemPointerGetOffsetNumber(&message->any.catalog.pointerData))
#define InvalidationMessageRegisterSharedInvalid_DEBUG2 \
	 elog(DEBUG, \
		  "InvalidationMessageRegisterSharedInvalid(r, %u, %u)", \
		  message->any.relation.relationId, \
		  message->any.relation.objectId)
#else
#define InvalidationMessageRegisterSharedInvalid_DEBUG1
#define InvalidationMessageRegisterSharedInvalid_DEBUG2
#endif	 /* INVALIDDEBUG */

static void
InvalidationMessageRegisterSharedInvalid(void* pointer)
{
        InvalidationMessage message = (InvalidationMessage)pointer;

	Assert(PointerIsValid(message));
	switch (message->kind)
	{
		case 'c':				/* cached system catalog tuple */
			InvalidationMessageRegisterSharedInvalid_DEBUG1;

			RegisterSharedInvalid(message->any.catalog.cacheId,
								  message->any.catalog.hashIndex,
								  &message->any.catalog.pointerData);
			break;

		case 'r':				/* cached relation descriptor */
			InvalidationMessageRegisterSharedInvalid_DEBUG2;

			RegisterSharedInvalid(message->any.relation.relationId,
								  message->any.relation.objectId,
								  (ItemPointer) NULL);
			break;

		default:
			elog(FATAL,
				 "InvalidationMessageRegisterSharedInvalid: `%c' kind",
				 message->kind);
	}
}

/* --------------------------------
 *		InvalidationMessageCacheInvalidate
 * --------------------------------
 */
#ifdef	INVALIDDEBUG
#define InvalidationMessageCacheInvalidate_DEBUG1 \
elog(DEBUG, "InvalidationMessageCacheInvalidate(c, %d, %d, [%d, %d])",\
	 message->any.catalog.cacheId,\
	 message->any.catalog.hashIndex,\
	 ItemPointerGetBlockNumber(&message->any.catalog.pointerData),\
	 ItemPointerGetOffsetNumber(&message->any.catalog.pointerData))
#define InvalidationMessageCacheInvalidate_DEBUG2 \
	 elog(DEBUG, "InvalidationMessageCacheInvalidate(r, %u, %u)", \
		  message->any.relation.relationId, \
		  message->any.relation.objectId)
#else
#define InvalidationMessageCacheInvalidate_DEBUG1
#define InvalidationMessageCacheInvalidate_DEBUG2
#endif	 /* defined(INVALIDDEBUG) */

static void
InvalidationMessageCacheInvalidate(void* pointer)
{
        InvalidationMessage message = (InvalidationMessage)pointer;

	Assert(PointerIsValid(message));
	switch (message->kind)
	{
		case 'c':				/* cached system catalog tuple */
			InvalidationMessageCacheInvalidate_DEBUG1;

			CacheIdInvalidate(message->any.catalog.cacheId,
							  message->any.catalog.hashIndex,
							  &message->any.catalog.pointerData);
			break;

		case 'r':				/* cached relation descriptor */
			InvalidationMessageCacheInvalidate_DEBUG2;

			CacheIdInvalidate(message->any.relation.relationId,
							  message->any.relation.objectId,
							  (ItemPointer) NULL);
			break;

		default:
			elog(FATAL, "InvalidationMessageCacheInvalidate: `%c' kind",
				 message->kind);
	}
}

/* --------------------------------
 *		RelationInvalidateRelationCache
 * --------------------------------
 */
static void
RelationInvalidateRelationCache(Relation relation,
								HeapTuple tuple,
								void (*function) (Oid, Oid))
{
	Oid			relationId;
	Oid			objectId;

	/* ----------------
	 *	get the relation object id
	 * ----------------
	 */
	relationId = RelationGetRelid(relation);

	/* ----------------
	 *	is it one of the ones we need to send an SI message for?
	 * ----------------
	 */
	if (relationId == RelOid_pg_class)
		objectId = tuple->t_data->t_oid;
	else if (relationId == RelOid_pg_attribute)
		objectId = ((Form_pg_attribute) GETSTRUCT(tuple))->attrelid;
	else
		return;

	/* ----------------
	 *	  can't handle immediate relation descriptor invalidation
	 * ----------------
	 */
	Assert(PointerIsValid(function));

	(*function) (relationId, objectId);
}


/*
 * DiscardInvalid
 *		Causes the invalidated cache state to be discarded.
 *
 * Note:
 *		This should be called as the first step in processing a transaction.
 *		This should be called while waiting for a query from the front end
 *		when other backends are active.
 */
void
DiscardInvalid(void)
{
	/* ----------------
	 *	debugging stuff
	 * ----------------
	 */
#ifdef	INVALIDDEBUG
	elog(DEBUG, "DiscardInvalid called");
#endif	 /* defined(INVALIDDEBUG) */

	InvalidateSharedInvalid();
}

/*
 * RegisterInvalid
 *		Causes registration of invalidated state with other backends iff true.
 *
 * Note:
 *		This should be called as the last step in processing a transaction.
 */
void
RegisterInvalid(bool send)
{
        InvalidationInfo* info = GetInvalidationInfo();
	LocalInvalid invalid;

	/* ----------------
	 *	debugging stuff
	 * ----------------
	 */
#ifdef	INVALIDDEBUG
	elog(DEBUG, "RegisterInvalid(%d) called", send);
#endif	 /* defined(INVALIDDEBUG) */

	/* ----------------
	 *	Process and free the current list of inval messages.
	 * ----------------
	 */

	DiscardInvalidStack(&info->InvalidLocal);
	if (send)
	{
		DiscardInvalidStack(&info->RollbackStack);
		invalid = info->InvalidForall;
		info->InvalidForall = EmptyLocalInvalid;		/* clear InvalidForall */
		LocalInvalidInvalidate(invalid, InvalidationMessageRegisterSharedInvalid, true);
	}
	else
	{
		DiscardInvalidStack(&info->InvalidForall);
		invalid = info->RollbackStack;
		info->RollbackStack = EmptyLocalInvalid;		/* clear RollbackStack */
		LocalInvalidInvalidate(invalid, InvalidationMessageCacheInvalidate, true);
	}

}

/*
 * ImmediateLocalInvalidation
 *		Causes invalidation immediately for the next command of the transaction.
 *
 * Note:
 *		This should be called in time of CommandCounterIncrement().
 */
void
ImmediateLocalInvalidation(bool send)
{
	LocalInvalid invalid;
        InvalidationInfo* info = GetInvalidationInfo();
	/* ----------------
	 *	debugging stuff
	 * ----------------
	 */
#ifdef	INVALIDDEBUG
	elog(DEBUG, "ImmediateLocalInvalidation(%d) called", send);
#endif	 /* defined(INVALIDDEBUG) */

	/* ----------------
	 *	Process and free the local list of inval messages.
	 * ----------------
	 */

	if (send)
	{
		invalid = info->InvalidLocal;
		info->InvalidLocal = EmptyLocalInvalid;		/* clear InvalidLocal */
		LocalInvalidInvalidate(invalid, InvalidationMessageCacheInvalidate, true);
	}
	else
	{

		/*
		 * This may be used for rollback to a savepoint. Don't clear
		 * InvalidForall and RollbackStack here.
		 */
		DiscardInvalidStack(&info->InvalidLocal);
		invalid = info->RollbackStack;
		LocalInvalidInvalidate(invalid, InvalidationMessageCacheInvalidate, false);
	}

}

/*
 * InvokeHeapTupleInvalidation
 *		Invoke functions for the tuple which register invalidation
 *		of catalog/relation cache.
 *	Note:
 *		Assumes object id is valid.
 *		Assumes tuple is valid.
 */
#ifdef	INVALIDDEBUG
#define InvokeHeapTupleInvalidation_DEBUG1 \
elog(DEBUG, "%s(%s, [%d,%d])", \
	 funcname,\
	 RelationGetPhysicalRelationName(relation), \
	 ItemPointerGetBlockNumber(&tuple->t_self), \
	 ItemPointerGetOffsetNumber(&tuple->t_self))
#else
#define InvokeHeapTupleInvalidation_DEBUG1
#endif	 /* defined(INVALIDDEBUG) */

static void
InvokeHeapTupleInvalidation(Relation relation, HeapTuple tuple,
							void (*CacheIdRegisterFunc) (),
							void (*RelationIdRegisterFunc) (),
							const char *funcname)
{
	/* ----------------
	 *	sanity checks
	 * ----------------
	 */
	Assert(RelationIsValid(relation));
	Assert(HeapTupleIsValid(tuple));

	if (IsBootstrapProcessingMode())
		return;
	/* ----------------
	 *	this only works for system relations now
	 * ----------------
	 */
	if (!IsSystemRelationName(NameStr(RelationGetForm(relation)->relname)))
		return;

	/* ----------------
	 *	debugging stuff
	 * ----------------
	 */
	InvokeHeapTupleInvalidation_DEBUG1;

	if ( CacheIdRegisterFunc != NULL ) 
		RelationInvalidateCatalogCacheTuple(relation, tuple,CacheIdRegisterFunc);

	if ( RelationIdRegisterFunc != NULL ) 
		RelationInvalidateRelationCache(relation, tuple,RelationIdRegisterFunc);
}
/*
 * RelationInvalidateHeapTuple
 *		Causes the given tuple in a relation to be invalidated.
 */
void
RelationInvalidateHeapTuple(Relation relation, HeapTuple tuple)
{
	InvokeHeapTupleInvalidation(relation, tuple,
								CacheIdRegisterLocalInvalid,
								RelationIdRegisterLocalInvalid,
								"RelationInvalidateHeapTuple");
}

/*
 * RelationMark4RollbackHeapTuple
 *		keep the given tuple in a relation to be invalidated
 *		in case of abort.
 */
void
RelationMark4RollbackHeapTuple(Relation relation, HeapTuple tuple)
{
	InvokeHeapTupleInvalidation(relation, tuple,
								CacheIdRegisterLocalRollback,
								RelationIdRegisterLocalRollback,
								"RelationMark4RollbackHeapTuple");
}

/*
 * ImmediateInvalidateSharedHeapTuple
 *		Different from RelationInvalidateHeapTuple()
 *		this function queues shared invalidation info immediately.
 */
void
ImmediateInvalidateSharedHeapTuple(Relation relation, HeapTuple tuple)
{
	InvokeHeapTupleInvalidation(relation, tuple,
								CacheIdImmediateRegisterSharedInvalid,
								RelationIdImmediateRegisterSharedInvalid,
								"ImmediateInvalidateSharedHeapTuple");
}

/*
 * ImmediateSharedRelationCacheInvalidate
 *	Register shared relation cache invalidation immediately
 *
 *	This is needed for smgrunlink()/smgrtruncate().
 *	Those functions unlink/truncate the base file immediately
 *	and couldn't be rollbacked in case of abort/crash.
 *	So relation cache invalidation must be registerd immediately.
 *	Note:
 *		Assumes Relation is valid.
 */
void
ImmediateSharedRelationCacheInvalidate(Relation relation)
{
	/* ----------------
	 *	sanity checks
	 * ----------------
	 */
	Assert(RelationIsValid(relation));

	if (IsBootstrapProcessingMode())
		return;

	/* ----------------
	 *	debugging stuff
	 * ----------------
	 */
#ifdef	INVALIDDEBUG
	elog(DEBUG, "ImmediateSharedRelationCacheInvalidate(%s)", \
		 RelationGetPhysicalRelationName(relation));
#endif	 /* defined(INVALIDDEBUG) */

	RelationIdImmediateRegisterSharedInvalid(RelOid_pg_class, RelationGetRelid(relation));
}
