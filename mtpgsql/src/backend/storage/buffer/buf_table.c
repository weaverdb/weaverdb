/*-------------------------------------------------------------------------
 *
 * buf_table.c
 *	  routines for finding buffers in the buffer pool.
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  $Header: /cvs/weaver/mtpgsql/src/backend/storage/buffer/buf_table.c,v 1.5 2007/05/23 15:39:23 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */
/*
 * OLD COMMENTS
 *
 * Data Structures:
 *
 *		Buffers are identified by their BufferTag (buf.h).	This
 * file contains routines for allocating a shmem hash table to
 * map buffer tags to buffer descriptors.
 *
 * Synchronization:
 *
 *	All routines in this file assume buffer manager spinlock is
 *	held by their caller.
 */
#include <sys/sdt.h>
#include <signal.h>
#include <time.h>
#include <pthread.h>

#include "postgres.h"

#include "env/env.h"

#include "storage/bufmgr.h"
#ifdef SPIN_IS_MUTEX
#include "storage/m_lock.h"
#else
#include "storage/s_lock.h"
#endif


static BufferLookupEnt* LockedHashSearch(char kind, BufferTag *keyPtr, Buffer insert, HASHACTION action, bool *foundPtr);

static BufferTable*   tables;
static int table_count;
/*
 * Initialize shmem hash table for mapping buffers
 */
void
InitBufTable(int count)
{
	HASHCTL		info;
	int		idx;
        char            name[255];
        bool            found;
        
        table_count = count;
        tables = (BufferTable*)ShmemInitStruct("Buffer Tables",sizeof(BufferTable) * count,&found);
        
  	info.keysize = sizeof(BufferTag);
	info.entrysize = sizeof(BufferLookupEnt);
	info.hash = tag_hash;      
        for (idx=0;idx<table_count;idx++) {
            pthread_mutex_init(&tables[idx].lock,&process_mutex_attr);
            snprintf(name,255,"Buffer Lookup Table #%d",idx);
            tables[idx].table = (HTAB *) ShmemInitHash(name,NBuffers, NBuffers,
						&info, (HASH_ELEM | HASH_FUNCTION));
            if ( tables[idx].table == NULL ) {
		elog(FATAL, "couldn't initialize shared buffer pool Hash Tbl");
            }
        }
}

BufferDesc *
BufTableLookup(char kind, BufferTag *tagPtr)
{
        BufferLookupEnt  *result;
        bool		found = false;
        BufferDesc* 	buf;

        if (tagPtr->blockNum == P_NEW)
                return NULL;

        result = LockedHashSearch(kind, tagPtr, 0, HASH_FIND, &found);

        if (!found) {
            return NULL;
        } else {
             return &(BufferDescriptors[result->id]);
        }
}

/*
 * BufTableDelete
 */
bool
BufTableDelete(BufferDesc *buf)
{
	BufferLookupEnt  *result;
	bool		found;

	/*
	 * buffer not initialized or has been removed from table already.
	 * BM_DELETED keeps us from removing buffer twice.
	 */
        pthread_mutex_lock(&(buf->cntx_lock.guard));
	if (buf->locflags & BM_DELETED) {
                pthread_mutex_unlock(&(buf->cntx_lock.guard));
		return TRUE;
        }

	buf->locflags |= (BM_DELETED);

	result = LockedHashSearch (buf->kind, &(buf->tag), 0, HASH_REMOVE, &found);

	if (!(result && found)) {
                elog(FATAL,"BufTableDelete:buffer not in table %d\n",buf->buf_id);
	}

        pthread_mutex_unlock(&(buf->cntx_lock.guard));
	return TRUE;
}

bool
BufTableInsert(BufferDesc *buf)
{
	BufferLookupEnt  *result;
	bool		found;

        pthread_mutex_lock(&(buf->cntx_lock.guard));
	
	buf->locflags |= (BM_VALID);
	buf->locflags &= ~(BM_DELETED);

	result = LockedHashSearch(buf->kind, &(buf->tag),buf->buf_id, HASH_ENTER, &found);

	if (!result || found)
	{
                elog(FATAL,"BufTableInsert:bad result %d\n",buf->buf_id);
	}

        pthread_mutex_unlock(&(buf->cntx_lock.guard));
	return TRUE;
}

bool 
BufTableReplace(BufferDesc *buf, Relation rel, BlockNumber block) {
	BufferLookupEnt  *result;
	bool		found;
        bool            used = false;
	/*
	 * buffer not initialized or has been removed from table already.
	 * BM_DELETED keeps us from removing buffer twice.
	 */
        pthread_mutex_lock(&(buf->cntx_lock.guard));
	
        if (!(buf->locflags & BM_DELETED) ) {
            result = LockedHashSearch(buf->kind, &(buf->tag), 0, HASH_REMOVE, &found);
	    buf->locflags |= BM_DELETED;	
            if (!(result && found)) {
                elog(FATAL,"BufTableReplace:buffer not in table %d %d",buf->buf_id, found);
            }
        }

        if ( buf->refCount != 1 ) {
            elog(DEBUG,"this should not happen, freelist invalidated the buffer and an invalid buffer cannot be pinned");
        } 

	CLEAR_BUFFERTAG(&buf->tag);
        INIT_BUFFERTAG(&buf->tag,rel,block);
        buf->kind = rel->rd_rel->relkind;
/* 
    now that the buffer is deleted from table and initialized with a new 
    tag, it's valid and inbound even if entry fails
*/

	result = LockedHashSearch(buf->kind, &(buf->tag), buf->buf_id, HASH_ENTER, &found);
/*  we can go ahead and say this is a valid insert when 
 *  the buffer is not found or replacing itself  
 **/
	if (!found)
	{
            used = true;
	    buf->locflags |= (BM_VALID);
	    buf->locflags &= ~(BM_DELETED);	
	} else {
            used = false;
        }

        pthread_mutex_unlock(&(buf->cntx_lock.guard));

	return used;
}


static BufferLookupEnt* LockedHashSearch(char kind, BufferTag *keyPtr, Buffer insert, HASHACTION action,bool *foundPtr) 
{
    BufferLookupEnt *  entry;         
    int table = keyPtr->relId.relId % table_count;
    
    pthread_mutex_lock(&tables[table].lock);
    entry = (BufferLookupEnt *)hash_search(tables[table].table, (char*)keyPtr, action, foundPtr);
    if ( action == HASH_ENTER && !(*foundPtr) ) {
        entry->id = insert;
    }
    pthread_mutex_unlock(&tables[table].lock);

    return entry;
}
