/*-------------------------------------------------------------------------
 *
 * dynahash.c
 *	  dynamic hash tables
 *
 *
 * Portions Copyright (c) 1996-2001, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  $Header: /cvs/weaver/mtpgsql/src/backend/utils/hash/dynahash.c,v 1.1.1.1 2006/08/12 00:21:59 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */
/*
 *
 * Dynamic hashing, after CACM April 1988 pp 446-457, by Per-Ake Larson.
 * Coded into C, with minor code improvements, and with hsearch(3) interface,
 * by ejp@ausmelb.oz, Jul 26, 1988: 13:16;
 * also, hcreate/hdestroy routines added to simulate hsearch(3).
 *
 * These routines simulate hsearch(3) and family, with the important
 * difference that the hash table is dynamic - can grow indefinitely
 * beyond its original size (as supplied to hcreate()).
 *
 * Performance appears to be comparable to that of hsearch(3).
 * The 'source-code' options referred to in hsearch(3)'s 'man' page
 * are not implemented; otherwise functionality is identical.
 *
 * Compilation controls:
 * DEBUG controls some informative traces, mainly for debugging.
 * HASH_STATISTICS causes HashAccesses and HashCollisions to be maintained;
 * when combined with HASH_DEBUG, these are displayed by hdestroy().
 *
 * Problems & fixes to ejp@ausmelb.oz. WARNING: relies on pre-processor
 * concatenation property, in probably unnecessary code 'optimisation'.
 *
 * Modified margo@postgres.berkeley.edu February 1990
 *		added multiple table interface
 * Modified by sullivan@postgres.berkeley.edu April 1990
 *		changed ctl structure for shared memory
 */

#include "postgres.h"
#include "env/env.h"

#include <sys/types.h>

#include "utils/dynahash.h"
#include "utils/hsearch.h"
#include "utils/memutils.h"

/*
 * Key (also entry) part of a HASHELEMENT
 */
#define ELEMENTKEY(helem)  (((char *)(helem)) + MAXALIGN(sizeof(HASHELEMENT)))

/*
 * Fast MOD arithmetic, assuming that y is a power of 2 !
 */
#define MOD(x,y)			   ((x) & ((y)-1))

typedef struct hash_memory {
    MemoryContext DynaHashCxt;
/*    MemoryContext CurrentDynaHashCxt;   */
} HashMemory;

#ifdef TLS
TLS HashMemory* hash_globals = NULL;
#else
#define hash_globals GetEnv()->hash_globals
#endif

SectionId  hash_mem_id = SECTIONID("HMEM");
/*
 * Private function prototypes
 */
static void *DynaHashAlloc(Size size,void* cxt);
static void DynaHashFree(void* pointer,void* cxt);
static HashMemory* CreateHashContext(void);
static HashMemory* GetHashContext(void);
static void	DestroyHashMemory(void);
static uint32 call_hash(HTAB *hashp, void *k);
static HASHSEGMENT seg_alloc(HTAB *hashp);
static bool element_alloc(HTAB *hashp);
static bool dir_realloc(HTAB *hashp);
static bool expand_table(HTAB *hashp);
static bool hdefault(HTAB *hashp);
static bool init_htab(HTAB *hashp, long nelem);
static void hash_corrupted(HTAB *hashp);


static void *
DynaHashAlloc(Size size,void* cxt)
{
	return MemoryContextAlloc((MemoryContext)cxt, size);
}

static void
DynaHashFree(void* pointer,void* cxt)
{
	pfree(pointer);
}

#define MEM_ALLOC		DynaHashAlloc
#define MEM_FREE		DynaHashFree


#if HASH_STATISTICS
static long hash_accesses,
			hash_collisions,
			hash_expansions;
#endif


/************************** CREATE ROUTINES **********************/

static HashMemory* CreateHashContext(void)
{
    HashMemory*  data = (HashMemory*)AllocateEnvSpace(hash_mem_id,sizeof(HashMemory));
    data->DynaHashCxt = (MemoryContext)AllocSetContextCreate(MemoryContextGetTopContext(),
												   "HashMemoryContext",
												ALLOCSET_DEFAULT_MINSIZE,
											   ALLOCSET_DEFAULT_INITSIZE,
											   ALLOCSET_DEFAULT_MAXSIZE);
                                                                                           
                                                                                           
    hash_globals = data;
    return data;
}

static HashMemory* GetHashContext(void)
{
    return hash_globals;
}

HTAB *
hash_create(const char *tabname, long nelem, HASHCTL *info, int flags)
{
	HTAB	   *hashp;
	HASHHDR    *hctl;
	void	   *(*h_alloc) (Size, void*);
	void	   (*h_free) (void*,void*);
    MemoryContext   ccxt = NULL;
/*  set the allocation method early so all structures 
*  for the hashtable use the designated allocation method 
*  if set.  This is needed to use global environment for
*  thread safety and seems more accurate MKS 2.18.2002
*/
    if (flags & HASH_ALLOC) {
        h_alloc = info->alloc;
        h_free = info->free;
    } else {
        h_alloc = MEM_ALLOC;
        h_free = MEM_FREE;
    }

	/* Select allocation context for this hash table */
	if (flags & HASH_CONTEXT) {
		if ( info->hcxt != NULL ) ccxt  = info->hcxt;  
	} else {
            /* First time through, create a memory context for hash tables */
            if (GetHashContext() == NULL) {
                CreateHashContext();
            }
            ccxt = GetHashContext()->DynaHashCxt;
        }

	/* Initialize the hash header */
	hashp = (HTAB *) h_alloc(sizeof(HTAB),ccxt);
	if (!hashp)
		return NULL;
	MemSet(hashp, 0, sizeof(HTAB));

	hashp->tabname = (char *) h_alloc(strlen(tabname) + 1,ccxt);
	strcpy(hashp->tabname, tabname);

	if (flags & HASH_FUNCTION)
		hashp->hash = info->hash;
	else
		hashp->hash = string_hash;		/* default hash function */

	if (flags & HASH_SHARED_MEM)
	{
		/*
		 * ctl structure is preallocated for shared memory tables. Note
		 * that HASH_DIRSIZE had better be set as well.
		 */
		hashp->hctl = info->hctl;
		hashp->dir = info->dir;
		hashp->alloc = info->alloc;
		hashp->free = info->free;
		hashp->hcxt = NULL;
		hashp->isshared = true;

		/* hash table already exists, we're just attaching to it */
		if (flags & HASH_ATTACH)
			return hashp;
	}
	else
	{
		/* setup hash table defaults */
		hashp->hctl = NULL;
		hashp->dir = NULL;
		hashp->alloc = h_alloc;
		hashp->free = h_free;
		hashp->hcxt = ccxt;
		hashp->isshared = false;
	}

	if (!hashp->hctl)
	{
		hashp->hctl = (HASHHDR *) hashp->alloc(sizeof(HASHHDR),ccxt);
		if (!hashp->hctl)
			return NULL;
	}

	if (!hdefault(hashp))
		return NULL;

	hctl = hashp->hctl;
#ifdef HASH_STATISTICS
	hctl->accesses = hctl->collisions = 0;
#endif

	if (flags & HASH_SEGMENT)
	{
		hctl->ssize = info->ssize;
		hctl->sshift = my_log2(info->ssize);
		/* ssize had better be a power of 2 */
		Assert(hctl->ssize == (1L << hctl->sshift));
	}
	if (flags & HASH_FFACTOR)
		hctl->ffactor = info->ffactor;

	/*
	 * SHM hash tables have fixed directory size passed by the caller.
	 */
	if (flags & HASH_DIRSIZE)
	{
            hctl->max_dsize = info->max_dsize;
            hctl->dsize = info->dsize;
	}

	/*
	 * hash table now allocates space for key and data but you have to say
	 * how much space to allocate
	 */
	if (flags & HASH_ELEM)
	{
            hctl->keysize = info->keysize;
            hctl->entrysize = info->entrysize;
	}

        hashp->hcxt = ccxt;

	if (!init_htab(hashp, nelem))
	{
            hash_destroy(hashp);
            return NULL;
	}
	return hashp;
}

/*
 * Set default HASHHDR parameters.
 */
static bool
hdefault(HTAB *hashp)
{
	HASHHDR    *hctl = hashp->hctl;

	MemSet(hctl, 0, sizeof(HASHHDR));

	hctl->ssize = DEF_SEGSIZE;
	hctl->sshift = DEF_SEGSIZE_SHIFT;
	hctl->dsize = DEF_DIRSIZE;
	hctl->ffactor = DEF_FFACTOR;
	hctl->nentries = 0;
	hctl->nsegs = 0;

	/* I added these MS. */

	/* rather pointless defaults for key & entry size */
	hctl->keysize = sizeof(char *);
	hctl->entrysize = 2 * sizeof(char *);

	/* table has no fixed maximum size */
	hctl->max_dsize = NO_MAX_DSIZE;

	/* garbage collection for HASH_REMOVE */
	hctl->freeList = NULL;

	return true;
}


static bool
init_htab(HTAB *hashp, long nelem)
{
	HASHHDR    *hctl = hashp->hctl;
	HASHSEGMENT *segp;
	int			nbuckets;
	int			nsegs;

	/*
	 * Divide number of elements by the fill factor to determine a desired
	 * number of buckets.  Allocate space for the next greater power of
	 * two number of buckets
	 */
	nelem = (nelem - 1) / hctl->ffactor + 1;

	nbuckets = 1 << my_log2(nelem);

	hctl->max_bucket = hctl->low_mask = nbuckets - 1;
	hctl->high_mask = (nbuckets << 1) - 1;

	/*
	 * Figure number of directory segments needed, round up to a power of
	 * 2
	 */
	nsegs = (nbuckets - 1) / hctl->ssize + 1;
	nsegs = 1 << my_log2(nsegs);

	/*
	 * Make sure directory is big enough. If pre-allocated directory is
	 * too small, choke (caller screwed up).
	 */
	if (nsegs > hctl->dsize)
	{
		if (!(hashp->dir))
			hctl->dsize = nsegs;
		else
			return false;
	}

	/* Allocate a directory */
	if (!(hashp->dir))
	{
		hashp->dir = (HASHSEGMENT *)
			hashp->alloc(hctl->dsize * sizeof(HASHSEGMENT),hashp->hcxt);
		if (!hashp->dir)
			return false;
	}

	/* Allocate initial segments */
	for (segp = hashp->dir; hctl->nsegs < nsegs; hctl->nsegs++, segp++)
	{
		*segp = seg_alloc(hashp);
		if (*segp == NULL)
			return false;
	}

#if HASH_DEBUG
	fprintf(stderr, "%s\n%s%p\n%s%d\n%s%d\n%s%d\n%s%d\n%s%d\n%s%x\n%s%x\n%s%d\n%s%d\n",
			"init_htab:",
			"TABLE POINTER   ", hashp,
			"DIRECTORY SIZE  ", hctl->dsize,
			"SEGMENT SIZE    ", hctl->ssize,
			"SEGMENT SHIFT   ", hctl->sshift,
			"FILL FACTOR     ", hctl->ffactor,
			"MAX BUCKET      ", hctl->max_bucket,
			"HIGH MASK       ", hctl->high_mask,
			"LOW  MASK       ", hctl->low_mask,
			"NSEGS           ", hctl->nsegs,
			"NENTRIES        ", hctl->nentries);
#endif
	return true;
}

/*
 * Estimate the space needed for a hashtable containing the given number
 * of entries of given size.
 * NOTE: this is used to estimate the footprint of hashtables in shared
 * memory; therefore it does not count HTAB which is in local memory.
 * NB: assumes that all hash structure parameters have default values!
 */
long
hash_estimate_size(long num_entries, long entrysize)
{
	long		size = 0;
	long		nBuckets,
				nSegments,
				nDirEntries,
				nElementAllocs,
				elementSize;

	/* estimate number of buckets wanted */
	nBuckets = 1L << my_log2((num_entries - 1) / DEF_FFACTOR + 1);
	/* # of segments needed for nBuckets */
	nSegments = 1L << my_log2((nBuckets - 1) / DEF_SEGSIZE + 1);
	/* directory entries */
	nDirEntries = DEF_DIRSIZE;
	while (nDirEntries < nSegments)
		nDirEntries <<= 1;		/* dir_alloc doubles dsize at each call */

	/* fixed control info */
	size += MAXALIGN(sizeof(HASHHDR));	/* but not HTAB, per above */
	/* directory */
	size += MAXALIGN(nDirEntries * sizeof(HASHSEGMENT));
	/* segments */
	size += nSegments * MAXALIGN(DEF_SEGSIZE * sizeof(HASHBUCKET));
	/* elements --- allocated in groups of HASHELEMENT_ALLOC_INCR */
	elementSize = MAXALIGN(sizeof(HASHELEMENT)) + MAXALIGN(entrysize);
	nElementAllocs = (num_entries - 1) / HASHELEMENT_ALLOC_INCR + 1;
	size += nElementAllocs * HASHELEMENT_ALLOC_INCR * elementSize;

	return size;
}

/*
 * Select an appropriate directory size for a hashtable with the given
 * maximum number of entries.
 * This is only needed for hashtables in shared memory, whose directories
 * cannot be expanded dynamically.
 * NB: assumes that all hash structure parameters have default values!
 *
 * XXX this had better agree with the behavior of init_htab()...
 */
long
hash_select_dirsize(long num_entries)
{
	long		nBuckets,
				nSegments,
				nDirEntries;

	/* estimate number of buckets wanted */
	nBuckets = 1L << my_log2((num_entries - 1) / DEF_FFACTOR + 1);
	/* # of segments needed for nBuckets */
	nSegments = 1L << my_log2((nBuckets - 1) / DEF_SEGSIZE + 1);
	/* directory entries */
	nDirEntries = DEF_DIRSIZE;
	while (nDirEntries < nSegments)
		nDirEntries <<= 1;		/* dir_alloc doubles dsize at each call */

	return nDirEntries;
}


/********************** DESTROY ROUTINES ************************/

void
hash_destroy(HTAB *hashp)
{
	if (hashp != NULL)
	{
        void*          		entry;
        HASHSEGMENT*		segp;
        HASHELEMENT*		dellist = NULL;
        HASHHDR*			hctl = hashp->hctl;
        int 				nsegs = 0;

		hash_stats("destroy", hashp);


/*  free segments  */
        for (segp = hashp->dir;nsegs < hctl->nsegs;nsegs++,segp++)
        {
            HASHSEGMENT segment = *segp;
            HASHBUCKET  bucket = *segment;
            while ( bucket != NULL ) {
                HASHELEMENT*  element = bucket;
                bucket = element->link;
                if ( element->freeable ) {
                    element->link = dellist;
                    dellist = element;
                }
            }
            hashp->free(segment,hashp->hcxt);
        }
/* free freelist  */
        if ( hctl != NULL ) {
                HASHELEMENT*  element;
                HASHELEMENT*  delement;
                element = delement = hctl->freeList;
                while (element != NULL) {
                    delement = element;
                    element = element->link;
                    if ( delement->freeable ) {
                    	delement->link = dellist;
                        dellist = delement;
                    }
                }
        }
        
        while ( dellist != NULL ) {
            HASHELEMENT* hit = dellist;
            dellist = hit->link;
            hashp->free(hit,hashp->hcxt);
        }
        
        hashp->free(hashp->dir,hashp->hcxt);
        hashp->free(hashp->hctl,hashp->hcxt);
        hashp->free(hashp->tabname,hashp->hcxt);
        hashp->free(hashp,hashp->hcxt);
#ifdef FUTURE
		if ( hashp->hcxt != NULL ) MemoryContextDelete(hashp->hcxt);
#endif
	}
}

void
hash_stats(const char *where, HTAB *hashp)
{
#if HASH_STATISTICS

	fprintf(stderr, "%s: this HTAB -- accesses %ld collisions %ld\n",
			where, hashp->hctl->accesses, hashp->hctl->collisions);

	fprintf(stderr, "hash_stats: entries %ld keysize %ld maxp %d segmentcount %d\n",
			hashp->hctl->nentries, hashp->hctl->keysize,
			hashp->hctl->max_bucket, hashp->hctl->nsegs);
	fprintf(stderr, "%s: total accesses %ld total collisions %ld\n",
			where, hash_accesses, hash_collisions);
	fprintf(stderr, "hash_stats: total expansions %ld\n",
			hash_expansions);
#endif

}

/*******************************SEARCH ROUTINES *****************************/

static uint32
call_hash(HTAB *hashp, void *k)
{
	HASHHDR    *hctl = hashp->hctl;
	long		hash_val,
				bucket;

	hash_val = hashp->hash(k, (int) hctl->keysize);

	bucket = hash_val & hctl->high_mask;
	if (bucket > hctl->max_bucket)
		bucket = bucket & hctl->low_mask;

	return (uint32) bucket;
}

/*----------
 * hash_search -- look up key in table and perform action
 *
 * action is one of:
 *		HASH_FIND: look up key in table
 *		HASH_ENTER: look up key in table, creating entry if not present
 *		HASH_REMOVE: look up key in table, remove entry if present
 *		HASH_FIND_SAVE: look up key in table, also save in static var
 *		HASH_REMOVE_SAVED: remove entry saved by HASH_FIND_SAVE
 *
 * Return value is a pointer to the element found/entered/removed if any,
 * or NULL if no match was found.  (NB: in the case of the REMOVE actions,
 * the result is a dangling pointer that shouldn't be dereferenced!)
 * A NULL result for HASH_ENTER implies we ran out of memory.
 *
 * If foundPtr isn't NULL, then *foundPtr is set TRUE if we found an
 * existing entry in the table, FALSE otherwise.  This is needed in the
 * HASH_ENTER case, but is redundant with the return value otherwise.
 *
 * The HASH_FIND_SAVE/HASH_REMOVE_SAVED interface is a hack to save one
 * table lookup in a find/process/remove scenario.	Note that no other
 * addition or removal in the table can safely happen in between.
 *----------
 */
void *
hash_search(HTAB *hashp,
			void *keyPtr,
			HASHACTION action,
			bool *foundPtr)
{
	HASHHDR    *    hctl = hashp->hctl;
	uint32		bucket;
	long		segment_num;
	long		segment_ndx;
	HASHSEGMENT     segp;
	HASHBUCKET	currBucket = NULL;
	HASHBUCKET *    prevBucketPtr = NULL;

        bucket = call_hash(hashp, keyPtr);
        segment_num = bucket >> hctl->sshift;
        segment_ndx = MOD(bucket, hctl->ssize);

        segp = hashp->dir[segment_num];

        if (segp == NULL)
                hash_corrupted(hashp);

        prevBucketPtr = &segp[segment_ndx];
        currBucket = *prevBucketPtr;

        /*
         * Follow collision chain looking for matching key
         */
        while (currBucket != NULL)
        {
                if (memcmp(ELEMENTKEY(currBucket), keyPtr, hctl->keysize) == 0)
                        break;
                prevBucketPtr = &(currBucket->link);
                currBucket = *prevBucketPtr;
        }

	if (foundPtr)
		*foundPtr = (bool) (currBucket != NULL);

	/*
	 * OK, now what?
	 */
	switch (action)
	{
		case HASH_FIND:
			if (currBucket != NULL)
				return (void *) ELEMENTKEY(currBucket);
			return NULL;
		case HASH_REMOVE:
			if (currBucket != NULL)
			{
				Assert(hctl->nentries > 0);
				hctl->nentries--;

				/* remove record from hash bucket's chain. */
				*prevBucketPtr = currBucket->link;

				/* add the record to the freelist for this table.  */
				currBucket->link = hctl->freeList;
				hctl->freeList = currBucket;

				/*
				 * better hope the caller is synchronizing access to this
				 * element, because someone else is going to reuse it the
				 * next time something is added to the table
				 */
				return (void *) ELEMENTKEY(currBucket);
			}
			return NULL;

		case HASH_ENTER:
			/* Return existing element if found, else create one */
			if (currBucket != NULL)
				return (void *) ELEMENTKEY(currBucket);

			/* get the next free element */
			currBucket = hctl->freeList;
			if (currBucket == NULL)
			{
				/* no free elements.  allocate another chunk of buckets */
				if (!element_alloc(hashp))
					return NULL;	/* out of memory */
				currBucket = hctl->freeList;
				Assert(currBucket != NULL);
			}

			hctl->freeList = currBucket->link;

			/* link into hashbucket chain */
			*prevBucketPtr = currBucket;
			currBucket->link = NULL;

			/* copy key into record */
			memcpy(ELEMENTKEY(currBucket), keyPtr, hctl->keysize);

			/* caller is expected to fill the data field on return */

			/* Check if it is time to split the segment */
			if (++hctl->nentries / (hctl->max_bucket + 1) > hctl->ffactor)
			{
				/*
				 * NOTE: failure to expand table is not a fatal error, it
				 * just means we have to run at higher fill factor than we
				 * wanted.
				 */
				expand_table(hashp);
			}

			return (void *) ELEMENTKEY(currBucket);
	}

	elog(ERROR, "hash_search: bogus action %d", (int) action);

	return NULL;				/* keep compiler quiet */
}

/*
 * hash_seq_init/_search
 *			Sequentially search through hash table and return
 *			all the elements one by one, return NULL when no more.
 *
 * NOTE: caller may delete the returned element before continuing the scan.
 * However, deleting any other element while the scan is in progress is
 * UNDEFINED (it might be the one that curIndex is pointing at!).  Also,
 * if elements are added to the table while the scan is in progress, it is
 * unspecified whether they will be visited by the scan or not.
 */
void
hash_seq_init(HASH_SEQ_STATUS *status, HTAB *hashp)
{
	status->hashp = hashp;
	status->curBucket = 0;
	status->curEntry = NULL;
}

void *
hash_seq_search(HASH_SEQ_STATUS *status)
{
	HTAB	   *hashp = status->hashp;
	HASHHDR    *hctl = hashp->hctl;

	while (status->curBucket <= hctl->max_bucket)
	{
		long		segment_num;
		long		segment_ndx;
		HASHSEGMENT segp;

		if (status->curEntry != NULL)
		{
			/* Continuing scan of curBucket... */
			HASHELEMENT *curElem;

			curElem = status->curEntry;
			status->curEntry = curElem->link;
			if (status->curEntry == NULL)		/* end of this bucket */
				++status->curBucket;
			return (void *) ELEMENTKEY(curElem);
		}

		/*
		 * initialize the search within this bucket.
		 */
		segment_num = status->curBucket >> hctl->sshift;
		segment_ndx = MOD(status->curBucket, hctl->ssize);

		/*
		 * first find the right segment in the table directory.
		 */
		segp = hashp->dir[segment_num];
		if (segp == NULL)
			hash_corrupted(hashp);

		/*
		 * now find the right index into the segment for the first item in
		 * this bucket's chain.  if the bucket is not empty (its entry in
		 * the dir is valid), we know this must correspond to a valid
		 * element and not a freed element because it came out of the
		 * directory of valid stuff.  if there are elements in the bucket
		 * chains that point to the freelist we're in big trouble.
		 */
		status->curEntry = segp[segment_ndx];

		if (status->curEntry == NULL)	/* empty bucket */
			++status->curBucket;
	}

	return NULL;				/* out of buckets */
}


/********************************* UTILITIES ************************/

/*
 * Expand the table by adding one more hash bucket.
 */
static bool
expand_table(HTAB *hashp)
{
	HASHHDR    *hctl = hashp->hctl;
	HASHSEGMENT old_seg,
				new_seg;
	long		old_bucket,
				new_bucket;
	long		new_segnum,
				new_segndx;
	long		old_segnum,
				old_segndx;
	HASHBUCKET *oldlink,
			   *newlink;
	HASHBUCKET	currElement,
				nextElement;

#ifdef HASH_STATISTICS
	hash_expansions++;
#endif

	new_bucket = hctl->max_bucket + 1;
	new_segnum = new_bucket >> hctl->sshift;
	new_segndx = MOD(new_bucket, hctl->ssize);

	if (new_segnum >= hctl->nsegs)
	{
		/* Allocate new segment if necessary -- could fail if dir full */
		if (new_segnum >= hctl->dsize)
			if (!dir_realloc(hashp))
				return false;
		if (!(hashp->dir[new_segnum] = seg_alloc(hashp)))
			return false;
		hctl->nsegs++;
	}

	/* OK, we created a new bucket */
	hctl->max_bucket++;

	/*
	 * *Before* changing masks, find old bucket corresponding to same hash
	 * values; values in that bucket may need to be relocated to new
	 * bucket. Note that new_bucket is certainly larger than low_mask at
	 * this point, so we can skip the first step of the regular hash mask
	 * calc.
	 */
	old_bucket = (new_bucket & hctl->low_mask);

	/*
	 * If we crossed a power of 2, readjust masks.
	 */
	if (new_bucket > hctl->high_mask)
	{
		hctl->low_mask = hctl->high_mask;
		hctl->high_mask = new_bucket | hctl->low_mask;
	}

	/*
	 * Relocate records to the new bucket.	NOTE: because of the way the
	 * hash masking is done in call_hash, only one old bucket can need to
	 * be split at this point.	With a different way of reducing the hash
	 * value, that might not be true!
	 */
	old_segnum = old_bucket >> hctl->sshift;
	old_segndx = MOD(old_bucket, hctl->ssize);

	old_seg = hashp->dir[old_segnum];
	new_seg = hashp->dir[new_segnum];

	oldlink = &old_seg[old_segndx];
	newlink = &new_seg[new_segndx];

	for (currElement = *oldlink;
		 currElement != NULL;
		 currElement = nextElement)
	{
		nextElement = currElement->link;
		if ((long) call_hash(hashp, (void *) ELEMENTKEY(currElement))
			== old_bucket)
		{
			*oldlink = currElement;
			oldlink = &currElement->link;
		}
		else
		{
			*newlink = currElement;
			newlink = &currElement->link;
		}
	}
	/* don't forget to terminate the rebuilt hash chains... */
	*oldlink = NULL;
	*newlink = NULL;

	return true;
}


static bool
dir_realloc(HTAB *hashp)
{
	HASHSEGMENT *p;
	HASHSEGMENT *old_p;
	long		new_dsize;
	long		old_dirsize;
	long		new_dirsize;

	if (hashp->hctl->max_dsize != NO_MAX_DSIZE)
		return false;

	/* Reallocate directory */
	new_dsize = hashp->hctl->dsize << 1;
	old_dirsize = hashp->hctl->dsize * sizeof(HASHSEGMENT);
	new_dirsize = new_dsize * sizeof(HASHSEGMENT);

	old_p = hashp->dir;
	p = (HASHSEGMENT *) hashp->alloc((Size) new_dirsize,hashp->hcxt);

	if (p != NULL)
	{
		memcpy(p, old_p, old_dirsize);
		MemSet(((char *) p) + old_dirsize, 0, new_dirsize - old_dirsize);
		hashp->free((char *) old_p,hashp->hcxt);
		hashp->dir = p;
		hashp->hctl->dsize = new_dsize;
		return true;
	}

	return false;
}


static HASHSEGMENT
seg_alloc(HTAB *hashp)
{
	HASHSEGMENT segp;

	segp = (HASHSEGMENT) hashp->alloc(sizeof(HASHBUCKET) * hashp->hctl->ssize,hashp->hcxt);

	if (!segp)
		return NULL;

	MemSet(segp, 0, sizeof(HASHBUCKET) * hashp->hctl->ssize);

	return segp;
}

/*
 * allocate some new elements and link them into the free list
 */
static bool
element_alloc(HTAB *hashp)
{
	HASHHDR    *hctl = hashp->hctl;
	Size		elementSize;
	HASHELEMENT *tmpElement;
	int			i;

	/* Each element has a HASHELEMENT header plus user data. */
	elementSize = MAXALIGN(sizeof(HASHELEMENT)) + MAXALIGN(hctl->entrysize);

	tmpElement = (HASHELEMENT *)
		hashp->alloc(HASHELEMENT_ALLOC_INCR * elementSize,hashp->hcxt);

	if (!tmpElement)
		return false;
        
    tmpElement->freeable = true;

	/* link all the new entries into the freelist */
	for (i = 0; i < HASHELEMENT_ALLOC_INCR; i++)
	{
		tmpElement->link = hctl->freeList;
		hctl->freeList = tmpElement;
		tmpElement = (HASHELEMENT *) (((char *) tmpElement) + elementSize);
/*  these aren't freeable b/c they were part of original allocation  */
        tmpElement->freeable = false;
	}

	return true;
}

/* complain when we have detected a corrupted hashtable */
static void
hash_corrupted(HTAB *hashp)
{
	/*
	 * If the corruption is in a shared hashtable, we'd better force a
	 * systemwide restart.	Otherwise, just shut down this one backend.
	 */
	if (hashp->isshared)
		elog(STOP, "Hash table '%s' corrupted", hashp->tabname);
	else
		elog(FATAL, "Hash table '%s' corrupted", hashp->tabname);
}

/* calculate ceil(log base 2) of num */
int
my_log2(long num)
{
	int			i;
	long		limit;

	for (i = 0, limit = 1; limit < num; i++, limit <<= 1)
		;
	return i;
}
