/*-------------------------------------------------------------------------
 *
 * mcxt.h
 *	  POSTGRES memory context definitions.
 *
 *
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * $Id: mcxt.h,v 1.1.1.1 2006/08/12 00:22:27 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */
#ifndef MCXT_H
#define MCXT_H

#include "c.h"

/* These types are declared in nodes/memnodes.h, but most users of memory
 * allocation should just treat them as abstract types, so we do not provide
 * the struct contents here.
 */

typedef struct MemoryContextData *MemoryContext;
typedef struct GlobalMemoryData *GlobalMemory;

typedef struct mem_manager_globals {
	MemoryContext ErrorContext;
	MemoryContext PostmasterContext;
	MemoryContext CacheMemoryContext;
	MemoryContext QueryContext;
	MemoryContext TopTransactionContext;
	MemoryContext TransactionCommandContext;
/*  temp for command */
	MemoryContext PortalExecutorHeapMemory;
} MemoryContextGlobals;

/*
 * MaxAllocSize
 *		Arbitrary limit on size of allocations.
 *
 * Note:
 *		There is no guarantee that allocations smaller than MaxAllocSize
 *		will succeed.  Allocation requests larger than MaxAllocSize will
 *		be summarily denied.
 *
 *		This value should not be referenced except in one place in the code.
 *
 * XXX This should be defined in a file of tunable constants.
 */
 /*#define MaxAllocSize	(0xfffffff)*/		/* 16G - 1 */

/*
 * prototypes for functions in mcxt.c
 */
 #ifdef __cplusplus
extern "C" {
#endif
PG_EXTERN MemoryContextGlobals* MemoryContextGetEnv(void);
PG_EXTERN void EnableMemoryContext(bool on);
#ifdef HAVE_ALLOCINFO
#define MemoryContextAlloc(cxt, size)  CallMemoryContextAlloc(cxt,size, __FILE__, __LINE__, __FUNCTION__)
PG_EXTERN void* CallMemoryContextAlloc(MemoryContext context, Size size, const char* filename, int lineno, const char* function); 
#else
PG_EXTERN void* MemoryContextAlloc(MemoryContext context, Size size);
#endif

PG_EXTERN MemoryContext MemoryContextSameContext(Pointer pointer);
PG_EXTERN void MemoryContextFree(MemoryContext context, Pointer pointer);
PG_EXTERN MemoryContext MemoryContextSwitchTo(MemoryContext context);
PG_EXTERN MemoryContext MemoryContextGetCurrentContext(void);
PG_EXTERN GlobalMemory CreateGlobalMemory(char *name);
PG_EXTERN void GlobalMemoryDestroy(GlobalMemory context);
PG_EXTERN void GlobalMemoryStats(void);


#ifdef __cplusplus
}
#endif




#endif	 /* MCXT_H */
