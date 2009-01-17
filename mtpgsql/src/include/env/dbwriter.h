

#ifndef _DBWRITER_H_
#define _DBWRITER_H_


#include <pthread.h>
#include <sys/types.h>
#include <unistd.h>

#include "c.h"


#include "storage/buf_internals.h"

#ifdef __cplusplus
extern "C" {
#endif


void DBWriterInit(int maxcount,int timeout,int hgc_threshold,int hgc_updatewt,int hgc_factor);

void DBCreateWriterThread(void);

void CommitDBBufferWrites(TransactionId xid,int state);

void FlushAllDirtyBuffers(void);
void RegisterBufferWrite(BufferDesc * bufHdr,bool release);

void ClearAllDBWrites(BufferDesc* bufHdr);
void ShutdownDBWriter(void);
bool IsDBWriter(void);

void ResetAccessCounts(Oid relid,Oid dbid);

char* RequestSnapshot(char* cmd);

#ifdef __cplusplus
}
#endif


#endif
