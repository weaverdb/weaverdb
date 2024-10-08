/*-------------------------------------------------------------------------
 *
 * bufmgr.h
 *	  POSTGRES buffer manager definitions.
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 *
 *-------------------------------------------------------------------------
 */
#ifndef LOCALBUF_H
#define LOCALBUF_H


#include "storage/block.h"
#include "storage/buf.h"
#include "storage/buf_internals.h"
#ifdef __cplusplus
extern "C" {
#endif
 
PG_EXTERN void IncrLocalBufferRefCount(Buffer buffer);
PG_EXTERN BufferDesc* GetLocalBufferDescriptor(Buffer buffer);
PG_EXTERN void DecrLocalRefCount(Buffer buffer);
PG_EXTERN void ReleaseLocalBuffer(Buffer buffer);
PG_EXTERN int GetLocalRefCount(Buffer buffer);
PG_EXTERN BufferDesc* LocalBufferSpecialAlloc(Relation reln, BlockNumber blockNum);
PG_EXTERN void LocalBufferSync(void);

#ifdef __cplusplus
}
#endif


#endif
