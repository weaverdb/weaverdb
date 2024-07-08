/*-------------------------------------------------------------------------
 *
 * relcache.h
 *	  Relation descriptor cache definitions.
 *
 *
 * Portions Copyright (c) 2000-2024, Myron Scott  <myron@weaverdb.org>
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 *
 *-------------------------------------------------------------------------
 */
#ifndef RELCACHE_H
#define RELCACHE_H

#include "utils/rel.h"
#include "lib/hasht.h"


    
typedef struct relidcacheent
{
	Oid	reloid;
	Relation	reldesc;
} RelIdCacheEnt;

typedef struct relnamecacheent
{
	NameData	relname;
	Relation	reldesc;
} RelNameCacheEnt;

#define 	DEFAULTDBOID   (-1)
/*
 * relation lookup routines
 */
PG_EXTERN Relation RelationIdCacheGetRelation(Oid relationId,Oid databaseId);
PG_EXTERN Relation RelationIdGetRelation(Oid relationId,Oid databaseId);
PG_EXTERN Relation RelationNameGetRelation(const char *relationName,Oid dadtabaseId);

PG_EXTERN void RelationClose(Relation relation);
PG_EXTERN void RelationForgetRelation(Oid rid,Oid databaseId);

/*
 * Routines for flushing/rebuilding relcache entries in various scenarios
 */
PG_EXTERN void RelationIdInvalidateRelationCache(Oid relationId,Oid databaseId);

PG_EXTERN void RelationCacheInvalidate(void);
PG_EXTERN void RelationCacheShutdown(void);

PG_EXTERN MemoryContext RelationGetCacheContext(void);

PG_EXTERN void RelationRegisterRelation(Relation relation);
PG_EXTERN void RelationPurgeLocalRelation(bool xactComitted);
PG_EXTERN void InitIndexRelations(void);
PG_EXTERN void RelationInitialize(void);
PG_EXTERN void RelationCacheAbort(void);
PG_EXTERN void RelationCacheCommit(void);
PG_EXTERN void RelationCacheWalk(HashtFunc func, int arg);
PG_EXTERN void ReportTransactionStatus(int level,char* id);

PG_EXTERN void RelationSetTrigger(Relation rel, BufferTrigger* read);
PG_EXTERN void RelationClearTrigger(Relation rel);
PG_EXTERN void PrintRelcacheMemory(void);

#endif	 /* RELCACHE_H */
