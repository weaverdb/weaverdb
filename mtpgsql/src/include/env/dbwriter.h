

#ifndef _DBWRITER_H_
#define _DBWRITER_H_


#include "c.h"

#include "storage/buf_internals.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum mode {
    SYNC_MODE,
    LOG_MODE
} DBMode;

void DBWriterInit(void);

void DBCreateWriterThread(DBMode mode);

void CommitDBBufferWrites(TransactionId xid,int state);

bool FlushAllDirtyBuffers(bool wait);
long RegisterBufferWrite(BufferDesc * bufHdr,bool release);

long GetBufferGeneration(void);

void ClearAllDBWrites(BufferDesc* bufHdr);
void ShutdownDBWriter(void);
bool IsDBWriter(void);

void ResetAccessCounts(Oid relid,Oid dbid);

char* RequestSnapshot(char* cmd);

long GetFlushTime(void);

#ifdef __cplusplus
}
#endif


#endif
