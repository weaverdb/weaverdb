

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

typedef enum mode {
    SYNC_MODE,
    LOG_MODE
} DBMode;

void DBWriterInit();

void DBCreateWriterThread(DBMode mode);

void CommitDBBufferWrites(TransactionId xid,int state);

void FlushAllDirtyBuffers(bool wait);
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
