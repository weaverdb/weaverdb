#include <pthread.h>
#include <sys/types.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>

#include "c.h"
#include "postgres.h"

#include "env/env.h"
#include "env/freespace.h"
#include "access/blobstorage.h"
#include "env/poolsweep.h"
#include "env/dbwriter.h"
#include "access/genam.h"
#include "access/heapam.h"
#include "catalog/pg_extent.h"
#include "catalog/indexing.h"
#include "catalog/catalog.h"

#include "config.h"
#include "miscadmin.h"
#include "utils/rel.h"
#include "utils/relcache.h"
#include "utils/memutils.h"
#include "storage/smgr.h"
#include "storage/buffile.h"


static HTAB*  			freetable;
static pthread_mutex_t          freespace_access;
static bool			inited = false;
static bool			print_freespace_memory = false;
static MemoryContext            free_cxt;

#define DEFAULT_MINLIVE  (BLCKSZ / 10)  /*  default if the available space is only 10% don't bother checking anymore */
#define INDEX_SIZE          8


typedef struct freekey {
	Oid		 relid;
	Oid		 dbid;
} FreeKey;

typedef struct freerun {
    BlockNumber         tryblock;
    Size                avail;
    int                 misses;
    int                 unused_pointers;
    bool                live;
} FreeRun;

typedef struct freespace {
    FreeKey				key;
    long 				size;
    long 				pointer;
    BlockNumber                         index[INDEX_SIZE];
    Size                                index_size[INDEX_SIZE];
    
    int                                 min_request;
    int                                 max_request;
    int                                 extent;
    
    bool                                extent_percentage;
    char				relkind;
    bool				active;
    bool                                end_scanned;
    
    BlockNumber                         relsize;
    double 				last_live_tuple_count;
    double				last_dead_tuple_count;
    Size				min_tuple_size;
    Size 				max_tuple_size;
    Size				ave_tuple_size;
    Size				total_available;

    FreeRun*                            blocks;
    pthread_cond_t                      creator;
    pthread_mutex_t                     accessor;
    pthread_t                           extender;
    MemoryContext                       context;
} FreeSpace;


static void* FreespaceAlloc(Size size,void* cxt);
static void FreespaceFree(void* pointer,void* cxt);
static FreeSpace* FindFreespace(Relation rel,char* dbname,bool create);
static BlockNumber FindEndSpace(Relation rel, FreeSpace* entry);
static int AllocatePagesViaSmgr(Relation rel,int count);
static int RecommendAllocation(Relation rel,FreeSpace* space);
static BlockNumber PerformAllocation(Relation rel, FreeSpace* free, int size);
static bool LookupExtentForRelation(Relation rel, FreeSpace* free);
static void SetExtentForRelation(Relation rel, int count,bool percentage);
static void RemoveExtentForRelation(Relation rel);
static void InitIndex(FreeSpace* space);

static int cmp_freeruns(const void *left, const void *right);

static void* FreespaceAlloc(Size size,void* cxt)
{
    return MemoryContextAlloc(cxt,size);
}

static void FreespaceFree(void* pointer,void* cxt)
{
	pfree(pointer);
}

void
InitFreespace()
{
	HASHCTL ctl;
	HTAB* temptable;
    MemoryContext  hash_cxt,old;
	
    free_cxt = AllocSetContextCreate((MemoryContext) NULL,
                                                 "FreespaceMemoryContext",
                                                ALLOCSET_DEFAULT_MINSIZE,
                                                ALLOCSET_DEFAULT_INITSIZE,
                                                ALLOCSET_DEFAULT_MAXSIZE);
                                             
    hash_cxt = AllocSetContextCreate((MemoryContext)free_cxt,
                                                 "FreespaceHashCxt",
                                                ALLOCSET_DEFAULT_MINSIZE,
                                                ALLOCSET_DEFAULT_INITSIZE,
                                                ALLOCSET_DEFAULT_MAXSIZE);
                                             
    old = MemoryContextSwitchTo(free_cxt);
    
	memset(&ctl,0,sizeof(HASHCTL));
	ctl.keysize = sizeof(FreeKey);
	ctl.entrysize = sizeof(FreeSpace);
	ctl.hash = tag_hash;
	ctl.alloc = FreespaceAlloc;
	ctl.free = FreespaceFree;
	ctl.hcxt = hash_cxt;
	
	freetable = hash_create("freespace hash",100,&ctl,HASH_ELEM | HASH_ALLOC | HASH_FUNCTION | HASH_CONTEXT);
	
	MemoryContextSwitchTo(old);
	pthread_mutex_init(&freespace_access,&process_mutex_attr);
	inited = true;
}

double 
GetUpdateFactor(Oid relid, Oid dbid, char* relname, char* dbname, double last_value, bool * trackable)
{
        FreeKey    key;
	FreeSpace* entry;
        bool found;
		
	if ( !inited) return 100.0;

        key.relid = relid;
        key.dbid = dbid;
        
	pthread_mutex_lock(&freespace_access);

	entry = hash_search(freetable,(char*)&key,HASH_FIND,&found);
        
        pthread_mutex_unlock(&freespace_access);
        
        if ( !found ) {
                *trackable = false;
        } else {
		double stats = 1.0;
		double mellow = 1.0;
                pthread_mutex_lock(&entry->accessor);
                
                *trackable = (entry->relkind == RELKIND_RELATION);
/*  results aren't ready yet so return an negative stating don't set value */
		 if ( !entry->active ) {
                     stats = -10.0;
                 } else if ( entry->relkind == RELKIND_RELATION) {
/*  start with one % of the live tuple count  */
                     stats = (entry->last_live_tuple_count * 0.01);
/*  add 10% of the dead tuple count, we want more frequent updates 
the more deletions there are  */          
                     stats += (entry->last_dead_tuple_count * 0.1);
/*  add a special factor for small tuples which we want to keep trim  */
                     stats += 100.0;
/*  divide by the total number of tuples + 1.0 to prevent divide by zero  */
                     stats /= (entry->last_live_tuple_count + 1.0); 
                     mellow = stats/last_value;
/*  prevent wild swings in the numbers   */
                     if ( last_value <= 0.0 ) {
                                 /*  max at 100% increase  */
                     } else if ( mellow > 3.0 ) {
                           stats = ( last_value * 3.0);
                      /*  min at 80% decrease  */
                     } else if ( mellow < 0.2 ) {
                           stats = ( last_value * 0.2 );
                     }
                     if ( stats < 0.00000001 ) stats = 1.0;
                     
                } else {
                     stats = 0.0;
                }
               pthread_mutex_unlock(&entry->accessor);
               return stats;
       }
       return 100.0;
}

int 
RegisterFreespace(Relation rel, int space,BlockNumber* index,
        Size* sa,int* unused_pointers,
        Size min,Size max,Size ave,
        TupleCount live_count,TupleCount dead_count,bool active)
{
	FreeSpace* entry;
        int c;
	
	if ( !inited ) return -1;

	entry = FindFreespace(rel,NULL,true);

	pthread_mutex_lock(&entry->accessor);
        MemoryContextResetAndDeleteChildren(entry->context);

	entry->size = space;
        entry->active = active;
        
	entry->pointer = 0;
        entry->index[0] = 0;
        entry->index_size[0] = sizeof_max_tuple_blob();
        for (c=1;c<INDEX_SIZE;c++) {
            entry->index[c] = 0;
            entry->index_size[c] = 0;
        }
        entry->min_request = MaxTupleSize;
        entry->max_request = MinTupleSize;
        entry->min_tuple_size = min;
	entry->max_tuple_size = max;
	entry->ave_tuple_size = ave;
        entry->last_live_tuple_count = live_count;
        entry->last_dead_tuple_count = dead_count;
    
	if ( space != 0 ) {
            FreeRun*    run;
            MemoryContext old = MemoryContextSwitchTo(entry->context);
            
            entry->total_available = 0;
            
            run = palloc(sizeof(FreeRun) * space);
            for (c=0;c<space;c++) {
                run[c].live = true;
                run[c].tryblock = index[c];
                run[c].avail = sa[c];
                run[c].misses = 0;
                run[c].unused_pointers = unused_pointers[c];
 
                entry->total_available += sa[c];         
            }
            
            qsort(run,space,sizeof(FreeRun),cmp_freeruns);
            
            entry->blocks = run;
            MemoryContextSwitchTo(old);
        } else {
            entry->blocks = NULL;
        }
/*  new statistics available */
	entry->active = true;
	pthread_mutex_unlock(&entry->accessor);
}

int 
SetFreespacePending(Oid relid, Oid dbid) {
        FreeKey     key;
	FreeSpace* entry;
        bool found;
		
	if ( !inited) return -1;

        key.relid = relid;
        key.dbid = dbid;
        
	pthread_mutex_lock(&freespace_access);

	entry = hash_search(freetable,(char*)&key,HASH_FIND,&found);
        
        pthread_mutex_unlock(&freespace_access);

	if ( entry ) {
		pthread_mutex_lock(&entry->accessor);
		entry->active = false;
		pthread_mutex_unlock(&entry->accessor);
	}

	return 0;
}

Size GetAverageTupleSize(Relation rel) {

	FreeSpace* entry = NULL;
	
	if ( !inited ) return 0;

	entry = FindFreespace(rel,NULL,false);

	if ( entry ) return entry->ave_tuple_size;
	else return 0;
}

Size GetMaximumTupleSize(Relation rel) {

	FreeSpace* entry = NULL;
	
	if ( !inited ) return 0;

	entry = FindFreespace(rel,NULL,false);

	if ( entry ) return entry->max_tuple_size;
	else return 0;
}

Size GetMinimumTupleSize(Relation rel)  {

	FreeSpace* entry = NULL;
	
	if ( !inited ) return 0;

	entry = FindFreespace(rel,NULL,false);

	if ( entry ) return entry->min_tuple_size;
	else return 0;
}

void GetTupleSizes(Relation rel,Size* min,Size* max,Size* ave) {

	FreeSpace* entry;
	
	if ( !inited ) return;
	
	entry = FindFreespace(rel,NULL,true);

	if ( !entry ) {
		return;
	}

	*min = entry->min_tuple_size;
	*max = entry->max_tuple_size;
	*ave = entry->ave_tuple_size;
}

BlockNumber
GetFreespace(Relation rel,int request,BlockNumber limit)
{
    FreeSpace*  entry;
    BlockNumber check = InvalidBlockNumber;
    bool        allocate = false;
    bool        scan = false;
    int         recommend   = 0;
    int 	space = request;
	
    if (!inited) {
        elog(FATAL,"Freespace not initialized");
    }

    entry = FindFreespace(rel,NULL,true);	
    if ( entry ) {
        int p = 0;
        int run = 0;
        int idx = 0;
        FreeRun*    alloc;

        pthread_mutex_lock(&entry->accessor);
    
        while ( entry->extender != 0 ) {
            pthread_cond_wait(&entry->creator,&entry->accessor);
        }
/*  
    this runs the length of a blob so that the full amount is covered by 
    the block allocation  
*/
        if ( space > MaxTupleSize ) {
            pthread_mutex_unlock(&entry->accessor);
            elog(ERROR,"requesting freespace greater than page size");
        } else {
           int start = 0;

            for (idx=0;idx < INDEX_SIZE-1;idx++) {
/*  prev is used below to see if we want to add an index entry */
                if (request > entry->index_size[idx+1]) break;
            }

            start =  entry->index[idx];
            alloc = entry->blocks;

            for (p = start; p < entry->size; p++) {
                run++;
                if ( !alloc[p].live ) {
                    if ( entry->pointer == p ) entry->pointer++;
                    continue;
                }

                if ( alloc[p].tryblock >= limit ) {
                    if ( alloc[p].avail > space ) {
                        check = alloc[p].tryblock;
                        break;
                    } else if ( alloc[p].misses++ > (1024) ) {
                        alloc[p].live = false;
                    }
                }
                    
                if ( alloc[p].misses > 10 && alloc[p].avail < entry->min_request ) {
                    alloc[p].live = false;
                }
            }
        }

//        if ( entry->size - p < RecommendAllocation(rel,entry) ) scan = true;
        if ( entry->min_request > request ) entry->min_request = request;
        if ( entry->max_request < request ) entry->max_request = request;
            
        if ( !BlockNumberIsValid(check) ) {
            entry->extender = pthread_self();
            recommend = RecommendAllocation(rel,entry);
            allocate = true;
        } else {
            DTRACE_PROBE4(mtpg, freespace__reservation,RelationGetRelationName(rel),alloc[p].avail,run,alloc[p].misses);
            Size remove_space = space;
            if ( alloc[p].unused_pointers == 0 ) {
                remove_space += sizeof(ItemIdData);
            } else {
                alloc[p].unused_pointers -= 1;
            }
            if ( remove_space > alloc[p].avail ) remove_space = alloc[p].avail;
            alloc[p].misses = 0;
            alloc[p].avail -= remove_space;

            if ( space >= sizeof_max_tuple_blob() || ( alloc[p].misses > 10 && alloc[p].avail < entry->min_request ) ) {
                alloc[p].live = false;
            }
                        
            entry->total_available -= remove_space;

            if ( entry->index_size[idx] > request * 2 && entry->index_size[idx+1] * 2 < request ) {
        /*  if the request is less than half the current index, make an entry  */
                memmove(entry->index + (idx+2),entry->index + (idx+1),(INDEX_SIZE - idx - 2) * sizeof(BlockNumber));
                memmove(entry->index_size + (idx+2),entry->index_size + (idx+1),(INDEX_SIZE - idx - 2) * sizeof(Size));
                entry->index[idx+1] = p;
                entry->index_size[idx+1] = request;
            } else {
                entry->index[idx] = p;
            }
        }
     
        pthread_mutex_unlock(&entry->accessor);

        if ( allocate ) {                        
            check = PerformAllocation(rel,entry,recommend);
        } else if ( scan ) {
            AddFreespaceScanRequest(RelationGetRelationName(rel),GetDatabaseName(),RelationGetRelid(rel),GetDatabaseId());
	}
    }

    return check;
}

long
GetTotalAvailable(Relation rel) {
    FreeSpace*  entry;
    long        avail = 0;
	
    if (!inited) {
        elog(FATAL,"Freespace not initialized");
    }

    entry = FindFreespace(rel,NULL,true);	
    if ( entry ) {
	pthread_mutex_lock(&entry->accessor);
        avail = entry->total_available;
	pthread_mutex_unlock(&entry->accessor);
    }
    return avail;
}

void 
DeactivateFreespace(Relation rel, BlockNumber blk,Size realspace)
{
	FreeSpace* entry = NULL;
	
	if ( !inited ) return;

 	entry = FindFreespace(rel,NULL,false);

	if ( entry ) {
		int p = entry->pointer;
		pthread_mutex_lock(&entry->accessor);
		for (p = entry->pointer;p<entry->size;p++) {
                    FreeRun*  free = &entry->blocks[p];
                    if ( free->tryblock == blk ) {
                        if ( realspace < entry->min_request ) {
                            free->live = false;
                        }
                        entry->total_available -= free->avail;
                        free->avail = realspace;
                        entry->total_available += realspace;
                        break;
                    }
		}
		pthread_mutex_unlock(&entry->accessor);
	}
}

void PrintFreespaceMemory( ) 
{
	pthread_mutex_lock(&freespace_access);

        size_t total = MemoryContextStats(free_cxt);
        user_log("Total freespace memory: %d",total);
                
	pthread_mutex_unlock(&freespace_access);

}

/*  rely on locking at the relation level to protect from 
    removing a referenced freespace
*/
int ForgetFreespace(Relation rel) {
	FreeKey tag;
	bool found;
        FreeSpace* entry;

        if ( !inited ) return 0;

	pthread_mutex_lock(&freespace_access);

	memset(&tag,0,sizeof(tag));
	tag.relid = rel->rd_lockInfo.lockRelId.relId;
	tag.dbid = rel->rd_lockInfo.lockRelId.dbId;

	entry = hash_search(freetable,(char*)&tag,HASH_REMOVE,&found);

        if ( !found ) {
            /*
            elog(NOTICE,"de-referencing unknown freespace %s-%s",RelationGetRelationName(rel),GetDatabaseName());
            */
        } else {
		pthread_cond_destroy(&entry->creator);
		pthread_mutex_destroy(&entry->accessor);
                MemoryContextDelete(entry->context);
        }

	pthread_mutex_unlock(&freespace_access);

        RemoveExtentForRelation(rel);

        return 0;
}


FreeSpace* FindFreespace(Relation rel,char* dbname,bool create) {

	FreeKey tag;
	bool	found;
	FreeSpace* entry;
	int type = ( create ) ? HASH_ENTER : HASH_FIND;
	
	if ( !inited ) return 0;

	memset(&tag,0,sizeof(tag));
	tag.relid = rel->rd_lockInfo.lockRelId.relId;
	tag.dbid = rel->rd_lockInfo.lockRelId.dbId;
	
	pthread_mutex_lock(&freespace_access);

	entry = (FreeSpace*)hash_search(freetable,(char*)&tag,type,&found);
	
	if ( found ) {
		pthread_mutex_unlock(&freespace_access);
		return entry;
 	} else if ( create ) {          
            int c=0;
                char*		db = ( dbname == NULL ) ? GetDatabaseName() : dbname;
		char mem_name[128];
		sprintf(mem_name,"FreespaceInstance-rel:%s-dbname:%s",RelationGetRelationName(rel),db);
		pthread_cond_init(&entry->creator,&process_cond_attr);
		pthread_mutex_init(&entry->accessor,&process_mutex_attr);
                entry->context = AllocSetContextCreate(freetable->hcxt,
						mem_name,
						1024,
                                                1024,
                                                5 * 1024 * 1024);
/*  get an accurate count of blocks 
    all heap relations have their block count managed
    here
*/
		entry->relkind = rel->rd_rel->relkind;
                entry->size = 0;
                entry->pointer = 0;
                entry->index[0] = 0;
                entry->index_size[0] = sizeof_max_tuple_blob();
                for (c=1;c<INDEX_SIZE;c++) {
                    entry->index[c] = 0;
                    entry->index_size[c] = 0;
                }                
		entry->relsize = smgrnblocks(rel->rd_smgr);
		entry->extender = 0;
		entry->active = false;
		entry->end_scanned = false;
                entry->min_tuple_size = 0;
                entry->max_tuple_size = 0;
		entry->ave_tuple_size = 0;
                entry->extent = 0;
                entry->extent_percentage = false;
                entry->blocks = NULL;
                
		pthread_mutex_unlock(&freespace_access);
                        
                if ( !rel->rd_myxactonly ) {
                    AddFreespaceScanRequest(RelationGetRelationName(rel),GetDatabaseName(),rel->rd_id,GetDatabaseId());
                }
                
		return entry;
	} else {
		pthread_mutex_unlock(&freespace_access);
                return NULL;
	}
}

BlockNumber
AllocateMoreSpace(Relation rel) {
    FreeSpace*  freespace = FindFreespace(rel,NULL,false);
    int recommend = 0;

    if (!inited) {
        elog(FATAL,"Freespace not initialized");
    }

/*  if not recommending a value, then the extender is not set and need to get an extension value  */
    pthread_mutex_lock(&freespace->accessor);
    if ( freespace->extender != 0 ) {
/*  forget it someone else is already extending the relation  */
        pthread_mutex_unlock(&freespace->accessor);
        return InvalidBlockNumber;
    }
    recommend = RecommendAllocation(rel,freespace);
    freespace->extender = pthread_self();
    pthread_mutex_unlock(&freespace->accessor);

    return PerformAllocation(rel, freespace, recommend);
}


BlockNumber
TruncateHeapRelation(Relation rel, BlockNumber new_rel_pages) {
    FreeSpace*  entry = FindFreespace(rel,NULL,TRUE);
    int count = 0;

    if (!inited) {
        elog(FATAL,"Freespace not initialized");
    }

    pthread_mutex_lock(&entry->accessor);

    while ( entry->extender != 0 ) {
        pthread_cond_wait(&entry->creator,&entry->accessor);
    }

    entry->extender = pthread_self();
        
    pthread_mutex_unlock(&entry->accessor);
    
    new_rel_pages = smgrtruncate(rel->rd_smgr, new_rel_pages);
    
    pthread_mutex_lock(&entry->accessor);
    
    entry->extender = 0;
    entry->relsize = new_rel_pages;
    for ( count =0;count < entry->size; count++) {
        if ( entry->blocks[count].tryblock >= new_rel_pages ) {
            entry->blocks[count].live = false;
            entry->blocks[count].avail = 0;
        }
    }
        
    pthread_cond_broadcast(&entry->creator);
    
    pthread_mutex_unlock(&entry->accessor);

    return new_rel_pages;
}

BlockNumber PerformAllocation(Relation rel, FreeSpace* freespace, int size) {
    
    BlockNumber     firstfree = FindEndSpace(rel,freespace);
    
    if ( BlockNumberIsValid(firstfree) ) return firstfree;
    
    int create = AllocatePagesViaSmgr(rel, size);

    pthread_mutex_lock(&freespace->accessor);

    if ( create + freespace->relsize != rel->rd_nblocks ) {
        elog(DEBUG,"file extension inconsistent %d %d",create + freespace->relsize,rel->rd_nblocks);
        freespace->relsize = rel->rd_nblocks - create;
    }

    if ( create > 0 ) {
        Size                total = (BLCKSZ - sizeof(PageHeaderData));
        int                 counter = 0;
        int                 oldsize,newsize;
        MemoryContext       old;

        firstfree = freespace->relsize;
        old = MemoryContextSwitchTo(freespace->context);

        oldsize = freespace->size;
        newsize = freespace->size + create;
        
        freespace->blocks = repalloc(freespace->blocks,sizeof(FreeRun) * newsize);
        for (counter=oldsize;counter<newsize;counter++) {
            freespace->blocks[counter].live = true;
            freespace->blocks[counter].tryblock = freespace->relsize++;
            freespace->blocks[counter].avail = total;
            freespace->blocks[counter].misses = 0;
            freespace->blocks[counter].unused_pointers = 0;
        }        
        freespace->total_available += (total*create);         
        freespace->size = newsize;
         
        MemoryContextSwitchTo(old);

        freespace->active = true;
        freespace->extender = 0;

        pthread_cond_broadcast(&freespace->creator);
    }

    pthread_mutex_unlock(&freespace->accessor);

    return firstfree;
}

int AllocatePagesViaSmgr(Relation rel,int create) {

    BlockNumber test = rel->rd_nblocks;
    union {
	double align;
	char	data[BLCKSZ];
    } buffer;

    memset(buffer.data,0x00,BLCKSZ);

    PageInit((Page)buffer.data,BLCKSZ, 0);
    PageInsertChecksum((Page)buffer.data);

    rel->rd_nblocks = smgrextend(rel->rd_smgr,buffer.data, create);

    return create;
}

void    SetNextExtent(Relation rel,int blockcount, bool percent) {
    FreeSpace*  freespace = FindFreespace(rel,NULL,true);
    pthread_mutex_lock(&freespace->accessor);
    if ( blockcount <= 0 ) blockcount = 0;
    freespace->extent = blockcount;
    freespace->extent_percentage = percent;
    pthread_mutex_unlock(&freespace->accessor);

    SetExtentForRelation(rel,blockcount,percent);
}


long GetNextExtentFactor(Relation rel) {
    FreeSpace*  freespace = FindFreespace(rel,NULL,false);
    long next_extent = 1;
    
    if ( freespace != NULL ) {
        pthread_mutex_lock(&freespace->accessor);
        next_extent = RecommendAllocation(rel,freespace);
        pthread_mutex_unlock(&freespace->accessor);
    }
    return next_extent;
}

/*
 * RelationGetNumberOfBlocks
 *		Returns the buffer descriptor associated with a page in a relation.
 *
 * Note:
 *		XXX may fail for huge relations.
 *		XXX should be elsewhere.
 *		XXX maybe should be hidden
 */
BlockNumber
RelationGetNumberOfBlocks(Relation relation)
{
    if ( inited && 
        ( relation->rd_rel->relkind == RELKIND_RELATION || relation->rd_rel->relkind == RELKIND_UNCATALOGED ) ) {
	FreeSpace* freespace = FindFreespace(relation,NULL,false);
        
        if ( freespace == NULL ) {
            relation->rd_nblocks = smgrnblocks(relation->rd_smgr);
        } else {
            pthread_mutex_lock(&freespace->accessor);
            relation->rd_nblocks = freespace->relsize;
            pthread_mutex_unlock(&freespace->accessor);
        }
    } else if ( relation->rd_rel->relkind == RELKIND_SEQUENCE || relation->rd_rel->relkind == RELKIND_INDEX ) {
        if ( relation->rd_nblocks == 0 ) {
            relation->rd_nblocks = smgrnblocks(relation->rd_smgr);
        }
    } else {
        if (!relation->rd_myxactonly) {
            relation->rd_nblocks = smgrnblocks(relation->rd_smgr);
        }
    }

    return relation->rd_nblocks;
}

int
RecommendAllocation(Relation rel,FreeSpace* freespace) {
    int create = 0;
    /*  assume freespace is already locked */
    double dead_per = ((double)freespace->last_dead_tuple_count /(double)freespace->last_live_tuple_count);

/*  if Bootstrap go one block at a time */
    if ( IsBootstrapProcessingMode() ) {
        return 1;
    }

    if ( freespace->extent == 0 ) {
/*  system relations should only be increased by 1 block, they are not accessed that much  */
/*  also, because Lookup does scans, double locking and deadlocks are possible if you try to lookup */
/*  extents for system relations */
        if ( IsSystemRelationName(RelationGetRelationName(rel)) || !LookupExtentForRelation(rel,freespace) ) {
            return 1;
        }
    }

    if ( freespace->extent_percentage ) {
        create = (int)(freespace->relsize * (((double)freespace->extent)/100.0));
    } else {
        create = freespace->extent;
    }
    
/*  introduce some sanity  */
    if ( create < 3 ) create = 3;
    if ( create > (NBuffers) ) create = (NBuffers) / 10;

    return create;
}

BlockNumber
FindEndSpace(Relation rel, FreeSpace* entry) {
    long         count = 0;
    BlockNumber         firstfree = InvalidBlockNumber;
    BlockNumber nblocks = entry->relsize;
    long         free_pages = 0;
    long         next_extent = 0;
    Size        total = BLCKSZ - sizeof(PageHeaderData);
    
     if ( entry->end_scanned ) return firstfree;
    
    pthread_mutex_lock(&entry->accessor);
    next_extent = RecommendAllocation(rel,entry);
    pthread_mutex_unlock(&entry->accessor);
            
    entry->end_scanned = true;

     for (count=nblocks;count>0;count--) {
        Buffer buf = ReadBuffer(rel,count-1);
        Page page = BufferGetPage(buf);
        bool empty = PageIsEmpty(page);
        ReleaseBuffer(rel,buf);
        if ( !empty ) break;
        else free_pages++;
        if ( free_pages > next_extent * 10 ) break;
    }

    if ( free_pages > 0 ) {
        pthread_mutex_lock(&entry->accessor);
        firstfree = nblocks - free_pages;
        entry->size = free_pages;
        entry->blocks = MemoryContextAlloc(entry->context,sizeof(FreeRun) * free_pages);
        for (count=0;count<free_pages;count++) {
            entry->blocks[count].live = true;
            entry->blocks[count].tryblock = (entry->relsize - free_pages + count);
            entry->blocks[count].avail = total;
            entry->blocks[count].misses = 0;
            entry->blocks[count].unused_pointers = 0;    
            entry->total_available += total;
        }
        entry->active = true;
        entry->extender = 0;
        pthread_cond_broadcast(&entry->creator);
        pthread_mutex_unlock(&entry->accessor);
    } else {
        return firstfree;
    }
    
    return firstfree;
}

bool 
LookupExtentForRelation(Relation rel, FreeSpace* freespace) {
    if ( !IsNormalProcessingMode() ) {
        return false;
    } else {
        Relation erel;
        Relation irel;
        IndexScanDesc scan;
        ScanKeyData    skey;
        Buffer          release;
        bool         handled = false;

        erel = heap_openr("pg_extent", AccessShareLock);
        irel = index_openr("pg_extent_index");

        ScanKeyEntryInitialize(&skey,
                               (bits16) 0x0,
                               (AttrNumber) 1,
                               (RegProcedure) F_OIDEQ,
                  ObjectIdGetDatum(RelationGetRelid(rel)));
        scan = index_beginscan(irel, false, 1, &skey);    

        while (index_getnext(scan, ForwardScanDirection)) {
            HeapTupleData   tuple;
            bool isnull = false;
            tuple.t_self = scan->xs_ctup.t_self;

            if (heap_fetch(erel, SnapshotNow, &tuple, &release)) {
                freespace->extent = DatumGetUInt16(heap_getattr(&tuple,Anum_pg_extent_allocation,RelationGetDescr(erel),&isnull));
                freespace->extent_percentage = DatumGetChar(heap_getattr(&tuple,Anum_pg_extent_percentage,RelationGetDescr(erel),&isnull));
                ReleaseBuffer(erel,release);
                handled = true;
                break;
            }
        }

        if ( !handled ) {
            freespace->extent = 5;
            freespace->extent_percentage = true;
        }

        index_endscan(scan);

        index_close(irel);

        heap_close(erel, AccessShareLock);

        return true;
    }
}


void 
SetExtentForRelation(Relation rel, int amount,bool percentage) {
    if ( !IsNormalProcessingMode() ) {
        return;
    } else {
        Relation erel = heap_openr("pg_extent", RowExclusiveLock);
        Relation irel = index_openr("pg_extent_index");
        IndexScanDesc scan;
        ScanKeyData    skey;
        Buffer          release;
        HeapTuple   newtup = NULL;
        bool        set = false;

        ScanKeyEntryInitialize(&skey,
                               (bits16) 0x0,
                               (AttrNumber) 1,
                               (RegProcedure) F_OIDEQ,
                  ObjectIdGetDatum(RelationGetRelid(rel)));
        scan = index_beginscan(irel, false, 1, &skey);    

        while (index_getnext(scan, ForwardScanDirection)) {
            HeapTupleData   tuple;
            bool isnull = false;
            tuple.t_self = scan->xs_ctup.t_self;

            if (heap_fetch(erel, SnapshotNow, &tuple, &release)) {
                if ( amount <= 0 ) {
                    ReleaseBuffer(erel,release);
                    heap_delete(erel,&scan->xs_ctup.t_self,NULL,NULL);
                } else {
                    Datum values[Natts_pg_extent];
                    char replace[Natts_pg_extent];
                    char nulls[Natts_pg_extent];

                    values[1] = Int16GetDatum(amount);
                    values[2] = CharGetDatum(percentage);

                    nulls[0] = ' ';
                    nulls[1] = ' ';
                    nulls[2] = ' '; 

                    replace[0] = ' ';
                    replace[1] = 'r';
                    replace[2] = 'r'; 

                    newtup = heap_modifytuple(&tuple,erel,values,nulls,replace);
                    ReleaseBuffer(erel,release);

                    heap_update(erel,&newtup->t_self,newtup,NULL,NULL);
                }
                set = true;
                break;
            }
        } 
       
        if ( !set ) {
            Datum values[Natts_pg_extent];
            char nulls[Natts_pg_extent];

            values[0] = ObjectIdGetDatum(RelationGetRelid(rel));
            values[1] = Int16GetDatum(amount);
            values[2] = CharGetDatum(percentage);

            nulls[0] = ' ';
            nulls[1] = ' ';
            nulls[2] = ' ';   

            newtup = heap_formtuple(RelationGetDescr(erel),values,nulls);
            heap_insert(erel,newtup);
        }

        if ( HeapTupleIsValid(newtup) ) {
            if (RelationGetForm(erel)->relhasindex)
            {
                    Relation	idescs[1];
                    char*       names[] = {"pg_extent_index"};

                    CatalogOpenIndices(1, names, idescs);
                    CatalogIndexInsert(idescs, 1, erel, newtup);
                    CatalogCloseIndices(1, idescs);
            }
            heap_freetuple(newtup);
         }

        index_endscan(scan);

        index_close(irel);

        heap_close(erel, RowExclusiveLock);
    }
}

void
RemoveExtentForRelation(Relation rel) {
    if ( !IsNormalProcessingMode() ) {

    } else {
        Relation erel;
        Relation irel;
        IndexScanDesc scan;
        ScanKeyData    skey;
        Buffer          release;
        bool         handled = false;

        erel = heap_openr("pg_extent", RowExclusiveLock);
        irel = index_openr("pg_extent_index");

        ScanKeyEntryInitialize(&skey,
                               (bits16) 0x0,
                               (AttrNumber) 1,
                               (RegProcedure) F_OIDEQ,
                  ObjectIdGetDatum(RelationGetRelid(rel)));
        scan = index_beginscan(irel, false, 1, &skey);    

        while (index_getnext(scan, ForwardScanDirection)) {
            HeapTupleData   tuple;
            bool isnull = false;
            tuple.t_self = scan->xs_ctup.t_self;

            if (heap_fetch(erel, SnapshotNow, &tuple, &release)) {
                ReleaseBuffer(erel,release);
                heap_delete(erel,&scan->xs_ctup.t_self,NULL,NULL);
                handled = true;
                break;
            }
        }

        index_endscan(scan);

        index_close(irel);

        heap_close(erel, RowExclusiveLock);
    }
}

int cmp_freeruns(const void *left, const void *right) {
	FreeRun*      lrun = (FreeRun*)left;
	FreeRun*       rrun = (FreeRun*)right;

	if (lrun->tryblock < rrun->tryblock)
		return -1;
	if (lrun->tryblock > rrun->tryblock)
		return 1;

	return 0;
}


void  freespace_log(FreeSpace* rel, char* pattern, ...) {
    char            msg[256];
    va_list         args;

    va_start(args, pattern);
    vsprintf(msg,pattern,args);
#ifdef SUNOS
    DTRACE_PROBE3(mtpg,freespace__msg,msg,rel->key.relid,rel->key.dbid);  
#endif
#ifdef DEBUGLOGS
    elog(DEBUG,"freespace:%d/%d %s",rel->key.relid,rel->key.dbid,msg);  
#endif
    va_end(args);
}
