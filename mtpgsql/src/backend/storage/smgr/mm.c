/*-------------------------------------------------------------------------
 *
 * mm.c
 *	  main memory storage manager
 *
 *	  This code manages relations that reside in (presumably stable)
 *	  main memory.
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

#ifdef STABLE_MEMORY_STORAGE

#include <math.h>


/*
 *	MMCacheTag -- Unique triplet for blocks stored by the main memory
 *				  storage manager.
 */

typedef struct MMCacheTag
{
	Oid			mmct_dbid;
	Oid			mmct_relid;
	BlockNumber mmct_blkno;
}			MMCacheTag;

/*
 *	Shared-memory hash table for main memory relations contains
 *	entries of this form.
 */

typedef struct MMHashEntry
{
	MMCacheTag	mmhe_tag;
	int			mmhe_bufno;
}			MMHashEntry;

/*
 * MMRelTag -- Unique identifier for each relation that is stored in the
 *					main-memory storage manager.
 */

typedef struct MMRelTag
{
	Oid			mmrt_dbid;
	Oid			mmrt_relid;
}			MMRelTag;

/*
 *	Shared-memory hash table for # blocks in main memory relations contains
 *	entries of this form.
 */

typedef struct MMRelHashEntry
{
	MMRelTag	mmrhe_tag;
	int			mmrhe_nblocks;
}			MMRelHashEntry;

#define MMNBUFFERS		10
#define MMNRELATIONS	2

SPINLOCK	MMCacheLock;
extern Oid	MyDatabaseId;

static int *MMCurTop;
static int *MMCurRelno;
static MMCacheTag *MMBlockTags;
static char *MMBlockCache;
static HTAB *MMCacheHT;
static HTAB *MMRelCacheHT;

int
mminit()
{
	char	   *mmcacheblk;
	int			mmsize = 0;
	bool		found;
	HASHCTL		info;

	SpinAcquire(MMCacheLock);

	mmsize += MAXALIGN(BLCKSZ * MMNBUFFERS);
	mmsize += MAXALIGN(sizeof(*MMCurTop));
	mmsize += MAXALIGN(sizeof(*MMCurRelno));
	mmsize += MAXALIGN((MMNBUFFERS * sizeof(MMCacheTag)));
	mmcacheblk = (char *) ShmemInitStruct("Main memory smgr", mmsize, &found);

	if (mmcacheblk == (char *) NULL)
	{
		SpinRelease(MMCacheLock);
		return SM_FAIL;
	}

	info.keysize = sizeof(MMCacheTag);
	info.datasize = sizeof(MMHashEntry) - sizeof(MMCacheTag);
	info.hash = tag_hash;

	MMCacheHT = (HTAB *) ShmemInitHash("Main memory store HT",
									   MMNBUFFERS, MMNBUFFERS,
									 &info, (HASH_ELEM | HASH_FUNCTION));

	if (MMCacheHT == (HTAB *) NULL)
	{
		SpinRelease(MMCacheLock);
		return SM_FAIL;
	}

	info.keysize = sizeof(MMRelTag);
	info.datasize = sizeof(MMRelHashEntry) - sizeof(MMRelTag);
	info.hash = tag_hash;

	MMRelCacheHT = (HTAB *) ShmemInitHash("Main memory rel HT",
										  MMNRELATIONS, MMNRELATIONS,
									 &info, (HASH_ELEM | HASH_FUNCTION));

	if (MMRelCacheHT == (HTAB *) NULL)
	{
		SpinRelease(MMCacheLock);
		return SM_FAIL;
	}


	SpinRelease(MMCacheLock);

	MMCurTop = (int *) mmcacheblk;
	mmcacheblk += sizeof(int);
	MMCurRelno = (int *) mmcacheblk;
	mmcacheblk += sizeof(int);
	MMBlockTags = (MMCacheTag *) mmcacheblk;
	mmcacheblk += (MMNBUFFERS * sizeof(MMCacheTag));
	MMBlockCache = mmcacheblk;

	return SM_SUCCESS;
}

int
mmshutdown()
{
	return SM_SUCCESS;
}

int
mmcreate(Relation reln)
{
	MMRelHashEntry *entry;
	bool		found;
	MMRelTag	tag;

	SpinAcquire(MMCacheLock);

	if (*MMCurRelno == MMNRELATIONS)
	{
		SpinRelease(MMCacheLock);
		return SM_FAIL;
	}

	(*MMCurRelno)++;

	tag.mmrt_relid = RelationGetRelid(reln);
	if (reln->rd_rel->relisshared)
		tag.mmrt_dbid = (Oid) 0;
	else
		tag.mmrt_dbid = MyDatabaseId;

	entry = (MMRelHashEntry *) hash_search(MMRelCacheHT,
									  (char *) &tag, HASH_ENTER, &found);


	if (found)
	{
		/* already exists */
		SpinRelease(MMCacheLock);
		return SM_FAIL;
	}

	entry->mmrhe_nblocks = 0;

	SpinRelease(MMCacheLock);

	return SM_SUCCESS;
}

/*
 *	mmunlink() -- Unlink a relation.
 */
int
mmunlink(Relation reln)
{
	int			i;
	Oid			reldbid;
	MMHashEntry *entry;
	MMRelHashEntry *rentry;
	bool		found;
	MMRelTag	rtag;

	if (reln->rd_rel->relisshared)
		reldbid = (Oid) 0;
	else
		reldbid = MyDatabaseId;

	SpinAcquire(MMCacheLock);

	for (i = 0; i < MMNBUFFERS; i++)
	{
		if (MMBlockTags[i].mmct_dbid == reldbid
			&& MMBlockTags[i].mmct_relid == RelationGetRelid(reln))
		{
			entry = (MMHashEntry *) hash_search(MMCacheHT,
												(char *) &MMBlockTags[i],
												HASH_REMOVE, &found);
			if (!found)
			{
				SpinRelease(MMCacheLock);
				elog(FATAL, "mmunlink: cache hash table corrupted");
			}
			MMBlockTags[i].mmct_dbid = (Oid) 0;
			MMBlockTags[i].mmct_relid = (Oid) 0;
			MMBlockTags[i].mmct_blkno = (BlockNumber) 0;
		}
	}
	rtag.mmrt_dbid = reldbid;
	rtag.mmrt_relid = RelationGetRelid(reln);

	rentry = (MMRelHashEntry *) hash_search(MMRelCacheHT, (char *) &rtag,
											HASH_REMOVE, &found);

	if (!found)
	{
		SpinRelease(MMCacheLock);
		elog(FATAL, "mmunlink: rel cache hash table corrupted");
	}

	(*MMCurRelno)--;

	SpinRelease(MMCacheLock);
	return 1;
}

/*
 *	mmextend() -- Add a block to the specified relation.
 *
 *		This routine returns SM_FAIL or SM_SUCCESS, with errno set as
 *		appropriate.
 */
int
mmextend(Relation reln, char *buffer)
{
	MMRelHashEntry *rentry;
	MMHashEntry *entry;
	int			i;
	Oid			reldbid;
	int			offset;
	bool		found;
	MMRelTag	rtag;
	MMCacheTag	tag;

	if (reln->rd_rel->relisshared)
		reldbid = (Oid) 0;
	else
		reldbid = MyDatabaseId;

	tag.mmct_dbid = rtag.mmrt_dbid = reldbid;
	tag.mmct_relid = rtag.mmrt_relid = RelationGetRelid(reln);

	SpinAcquire(MMCacheLock);

	if (*MMCurTop == MMNBUFFERS)
	{
		for (i = 0; i < MMNBUFFERS; i++)
		{
			if (MMBlockTags[i].mmct_dbid == 0 &&
				MMBlockTags[i].mmct_relid == 0)
				break;
		}
		if (i == MMNBUFFERS)
		{
			SpinRelease(MMCacheLock);
			return SM_FAIL;
		}
	}
	else
	{
		i = *MMCurTop;
		(*MMCurTop)++;
	}

	rentry = (MMRelHashEntry *) hash_search(MMRelCacheHT, (char *) &rtag,
											HASH_FIND, &found);
	if (!found)
	{
		SpinRelease(MMCacheLock);
		elog(FATAL, "mmextend: rel cache hash table corrupt");
	}

	tag.mmct_blkno = rentry->mmrhe_nblocks;

	entry = (MMHashEntry *) hash_search(MMCacheHT, (char *) &tag,
										HASH_ENTER, &found);
	if (found)
	{
		SpinRelease(MMCacheLock);
		elog(FATAL, "mmextend: cache hash table corrupt");
	}

	entry->mmhe_bufno = i;
	MMBlockTags[i].mmct_dbid = reldbid;
	MMBlockTags[i].mmct_relid = RelationGetRelid(reln);
	MMBlockTags[i].mmct_blkno = rentry->mmrhe_nblocks;

	/* page numbers are zero-based, so we increment this at the end */
	(rentry->mmrhe_nblocks)++;

	/* write the extended page */
	offset = (i * BLCKSZ);
	memmove(&(MMBlockCache[offset]), buffer, BLCKSZ);

	SpinRelease(MMCacheLock);

	return SM_SUCCESS;
}

/*
 *	mmopen() -- Open the specified relation.
 */
int
mmopen(Relation reln)
{
	/* automatically successful */
	return 0;
}

/*
 *	mmclose() -- Close the specified relation.
 *
 *		Returns SM_SUCCESS or SM_FAIL with errno set as appropriate.
 */
int
mmclose(Relation reln)
{
	/* automatically successful */
	return SM_SUCCESS;
}

/*
 *	mmread() -- Read the specified block from a relation.
 *
 *		Returns SM_SUCCESS or SM_FAIL.
 */
int
mmread(Relation reln, BlockNumber blocknum, char *buffer)
{
	MMHashEntry *entry;
	bool		found;
	int			offset;
	MMCacheTag	tag;

	if (reln->rd_rel->relisshared)
		tag.mmct_dbid = (Oid) 0;
	else
		tag.mmct_dbid = MyDatabaseId;

	tag.mmct_relid = RelationGetRelid(reln);
	tag.mmct_blkno = blocknum;

	SpinAcquire(MMCacheLock);
	entry = (MMHashEntry *) hash_search(MMCacheHT, (char *) &tag,
										HASH_FIND, &found);


	if (!found)
	{
		/* reading nonexistent pages is defined to fill them with zeroes */
		SpinRelease(MMCacheLock);
		MemSet(buffer, 0, BLCKSZ);
		return SM_SUCCESS;
	}

	offset = (entry->mmhe_bufno * BLCKSZ);
	memmove(buffer, &MMBlockCache[offset], BLCKSZ);

	SpinRelease(MMCacheLock);

	return SM_SUCCESS;
}

/*
 *	mmwrite() -- Write the supplied block at the appropriate location.
 *
 *		Returns SM_SUCCESS or SM_FAIL.
 */
int
mmwrite(Relation reln, BlockNumber blocknum, char *buffer)
{
	MMHashEntry *entry;
	bool		found;
	int			offset;
	MMCacheTag	tag;

	if (reln->rd_rel->relisshared)
		tag.mmct_dbid = (Oid) 0;
	else
		tag.mmct_dbid = MyDatabaseId;

	tag.mmct_relid = RelationGetRelid(reln);
	tag.mmct_blkno = blocknum;

	SpinAcquire(MMCacheLock);
	entry = (MMHashEntry *) hash_search(MMCacheHT, (char *) &tag,
										HASH_FIND, &found);


	if (!found)
	{
		SpinRelease(MMCacheLock);
		elog(FATAL, "mmwrite: hash table missing requested page");
	}

	offset = (entry->mmhe_bufno * BLCKSZ);
	memmove(&MMBlockCache[offset], buffer, BLCKSZ);

	SpinRelease(MMCacheLock);

	return SM_SUCCESS;
}

/*
 *	mmflush() -- Synchronously write a block to stable storage.
 *
 *		For main-memory relations, this is exactly equivalent to mmwrite().
 */
int
mmflush(Relation reln, BlockNumber blocknum, char *buffer)
{
	return mmwrite(reln, blocknum, buffer);
}

/*
 *	mmblindwrt() -- Write a block to stable storage blind.
 *
 *		We have to be able to do this using only the name and OID of
 *		the database and relation in which the block belongs.
 */
int
mmblindwrt(char *dbstr,
		   char *relstr,
		   Oid dbid,
		   Oid relid,
		   BlockNumber blkno,
		   char *buffer,
		   bool dofsync)
{
	return SM_FAIL;
}

/*
 *	mmnblocks() -- Get the number of blocks stored in a relation.
 *
 *		Returns # of blocks or -1 on error.
 */
int
mmnblocks(Relation reln)
{
	MMRelTag	rtag;
	MMRelHashEntry *rentry;
	bool		found;
	int			nblocks;

	if (reln->rd_rel->relisshared)
		rtag.mmrt_dbid = (Oid) 0;
	else
		rtag.mmrt_dbid = MyDatabaseId;

	rtag.mmrt_relid = RelationGetRelid(reln);

	SpinAcquire(MMCacheLock);

	rentry = (MMRelHashEntry *) hash_search(MMRelCacheHT, (char *) &rtag,
											HASH_FIND, &found);


	if (found)
		nblocks = rentry->mmrhe_nblocks;
	else
		nblocks = -1;

	SpinRelease(MMCacheLock);

	return nblocks;
}

/*
 *	mmcommit() -- Commit a transaction.
 *
 *		Returns SM_SUCCESS or SM_FAIL with errno set as appropriate.
 */
int
mmcommit()
{
	return SM_SUCCESS;
}

/*
 *	mmabort() -- Abort a transaction.
 */

int
mmabort()
{
	return SM_SUCCESS;
}

/*
 *	MMShmemSize() -- Declare amount of shared memory we require.
 *
 *		The shared memory initialization code creates a block of shared
 *		memory exactly big enough to hold all the structures it needs to.
 *		This routine declares how much space the main memory storage
 *		manager will use.
 */
size_t
MMShmemSize()
{
	size_t			size = 0;

	/*
	 * first compute space occupied by the (dbid,relid,blkno) hash table
	 */
	size += hash_estimate_size(MMNBUFFERS,
							   0,		/* MMHashEntry includes key */
							   sizeof(MMHashEntry));

	/*
	 * now do the same for the rel hash table
	 */
	size += hash_estimate_size(MMNRELATIONS,
							   0,		/* MMRelHashEntry includes key */
							   sizeof(MMRelHashEntry));

	/*
	 * finally, add in the memory block we use directly
	 */

	size += MAXALIGN(BLCKSZ * MMNBUFFERS);
	size += MAXALIGN(sizeof(*MMCurTop));
	size += MAXALIGN(sizeof(*MMCurRelno));
	size += MAXALIGN(MMNBUFFERS * sizeof(MMCacheTag));

	return size;
}

#endif	 /* STABLE_MEMORY_STORAGE */
