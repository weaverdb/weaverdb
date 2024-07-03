/*
 * Copyright (c) 2024 Myron Scott <myron@weaverdb.org> All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 */



#ifndef _FREESPACE_H_
#define _FREESPACE_H_


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

BlockNumber AllocateMoreSpace(Relation rel,char* sdata, int ssize);
BlockNumber TruncateHeapRelation(Relation rel, BlockNumber new_size);

void    SetNextExtent(Relation,int blockcount, bool percent);
long    GetNextExtentFactor(Relation rel);
long    GetTotalAvailable(Relation rel);

BlockNumber RelationGetNumberOfBlocks(Relation relation);
#ifdef __cplusplus
}
#endif


#endif
