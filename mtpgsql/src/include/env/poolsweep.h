

#ifndef _POOLSWEEP_H_
#define _POOLSWEEP_H_


#include "c.h"
#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

void PoolsweepInit(int priority);
void PoolsweepDestroy(void);

void AddRecoverRequest(char* dbname, Oid dbid);
void AddRelinkBlobsRequest(char* relname,char* dbname,Oid relid,Oid dbid);
void AddScanRequest(char* relname,char* dbname,Oid relid,Oid dbid);
void AddAnalyzeRequest(char* relname,char* dbname,Oid relid,Oid dbid);
void AddReindexRequest(char* relname,char* dbname,Oid relid,Oid dbid);
void AddVacuumRequest(char* rname,char* dbname,Oid relid,Oid dbid);
void AddFreespaceScanRequest(char *relname, char *dbname, Oid relid, Oid dbid);
void AddDefragRequest(char* rname,char* dbname,Oid relid,Oid dbid,bool blobs,int max);
void AddCompactRequest(char* rname,char* dbname,Oid relid,Oid dbid,bool blobs,int max);
void AddVacuumDatabaseRequest(char* rname,char* dbname,Oid relid,Oid dbid);
void AddTrimRequest(char* rname,char* dbname,Oid relid,Oid dbid);
void AddRespanRequest(char* rname,char* dbname,Oid relid,Oid dbid);
void AddMoveRequest(char* rname,char* dbname,Oid relid,Oid dbid);
void AddAllocateSpaceRequest(char* rname,char* dbname,Oid relid,Oid dbid);
void AddWaitRequest(char* dbname, Oid dbid);
void StopPoolsweepsForDB(Oid dbid);
bool ContainsVacuumRequest(Oid relid,Oid dbid);
void DropVacuumRequests(Oid relid,Oid dbid);
bool IsPoolsweep(void);
void PausePoolsweep(void);
bool IsPoolsweepPaused(void);
void ResumePoolsweep(void);
void PrintPoolsweepMemory(void);

#ifdef __cplusplus
}
#endif


#endif
