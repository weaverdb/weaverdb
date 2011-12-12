

#ifndef _FREESPACE_H_
#define _FREESPACE_H_


#include <pthread.h>
#include <sys/types.h>

#include "c.h"
#include "postgres.h"
#include "config.h"
#include "utils/rel.h"
#include "utils/relcache.h"


#define P_END	-100


#ifdef __cplusplus
extern "C" {
#endif
void InitFreespace(void);
int SetFreespacePending(Oid relid,  Oid dbid);
int RegisterFreespace(Relation rel, int space,
	BlockNumber* index,Size* sa,int* unused_pointer_count,
        Size min,Size max,Size ave,TupleCount live_tuples,TupleCount dead_tuples, bool active);
int ForgetFreespace(Relation rel,bool gone);

double GetUpdateFactor(Oid relid, Oid dbid, char* relname,char* dbname,double last_value, bool * trackable);
BlockNumber GetFreespace(Relation rel,int space,BlockNumber limit);
Size	GetAverageTupleSize(Relation rel);
Size	GetMaximumTupleSize(Relation rel);
Size	GetMinimumTupleSize(Relation rel);
void	GetTupleSizes(Relation rel,Size* min,Size* max,Size* ave);
void	DeactivateFreespace(Relation rel,BlockNumber blk,Size realspace);
void	PrintFreespaceMemory(void);

BlockNumber AllocateMoreSpace(Relation rel);
BlockNumber TruncateHeapRelation(Relation rel, BlockNumber new_size);

void    SetNextExtent(Relation,int blockcount, bool percent);
long    GetNextExtentFactor(Relation rel);
long    GetTotalAvailable(Relation rel);

BlockNumber RelationGetNumberOfBlocks(Relation relation);
#ifdef __cplusplus
}
#endif


#endif
