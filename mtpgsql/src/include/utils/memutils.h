/*-------------------------------------------------------------------------
 *
 * memutils.h
 *	  This file contains declarations for memory allocation utility
 *	  functions.  These are functions that are not quite widely used
 *	  enough to justify going in utils/palloc.h, but are still part
 *	  of the API of the memory management subsystem.
 *
 *
 * Portions Copyright (c) 1996-2001, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * $Id: memutils.h,v 1.1.1.1 2006/08/12 00:22:27 synmscott Exp $
 *
 *-------------------------------------------------------------------------
 */
#ifndef MEMUTILS_H
#define MEMUTILS_H

#include "nodes/memnodes.h"
#include "tcop/dest.h"


/*
 * MaxAllocSize
 *		Quasi-arbitrary limit on size of allocations.
 *
 * Note:
 *		There is no guarantee that allocations smaller than MaxAllocSize
 *		will succeed.  Allocation requests larger than MaxAllocSize will
 *		be summarily denied.
 *
 * XXX This is deliberately chosen to correspond to the limiting size
 * of varlena objects under TOAST.	See VARATT_MASK_SIZE in postgres.h.
 */
#define MaxAllocSize	((Size) 0x3fffffff)		/* 1 gigabyte - 1 */

#define AllocSizeIsValid(size)	(0 < (size) && (size) <= MaxAllocSize)

/*
 * All chunks allocated by any memory context manager are required to be
 * preceded by a StandardChunkHeader at a spacing of STANDARDCHUNKHEADERSIZE.
 * A currently-allocated chunk must contain a backpointer to its owning
 * context as well as the allocated size of the chunk.	The backpointer is
 * used by pfree() and repalloc() to find the context to call.	The allocated
 * size is not absolutely essential, but it's expected to be needed by any
 * reasonable implementation.
 */
typedef struct StandardChunkHeader
{
	MemoryContext context;		/* owning context */
	Size		size;			/* size of data space allocated in chunk */
#ifdef MEMORY_CONTEXT_CHECKING
	/* when debugging memory usage, also store actual requested size */
	Size		requested_size;
#endif
} StandardChunkHeader;

#define STANDARDCHUNKHEADERSIZE  MAXALIGN(sizeof(StandardChunkHeader))

#define GetMemoryContext(pointer) ( ((StandardChunkHeader *) \
		((char *) pointer - STANDARDCHUNKHEADERSIZE))->context )
		
#define GetMemorySize(pointer) ( ((StandardChunkHeader *) \
		((char *) pointer - STANDARDCHUNKHEADERSIZE))->size )
/*
 * Standard top-level memory contexts.
 *
 * Only TopMemoryContext and ErrorContext are initialized by
 * MemoryContextInit() itself.
 */
 #ifndef USE_GLOBAL_ENVIRONMENT
extern DLLIMPORT MemoryContext TopMemoryContext;
extern DLLIMPORT MemoryContext ErrorContext;
extern DLLIMPORT MemoryContext PostmasterContext;
extern DLLIMPORT MemoryContext CacheMemoryContext;
extern DLLIMPORT MemoryContext QueryContext;
extern DLLIMPORT MemoryContext TopTransactionContext;
extern DLLIMPORT MemoryContext TransactionCommandContext;
#else 


#endif

/*
 * Memory-context-type-independent functions in mcxt.c
 */
PG_EXTERN void MemoryContextInit(void);

PG_EXTERN void MemoryContextInitEnv(void);
PG_EXTERN void MemoryContextDestroyEnv(void);

PG_EXTERN MemoryContext MemoryContextGetTopContext(void);

PG_EXTERN void MemoryContextDelete(MemoryContext context);
PG_EXTERN void MemoryContextResetChildren(MemoryContext context);
PG_EXTERN void MemoryContextDeleteChildren(MemoryContext context);
PG_EXTERN void MemoryContextResetAndDeleteChildren(MemoryContext context);
PG_EXTERN size_t MemoryContextStats(MemoryContext context);
PG_EXTERN size_t PrintMemoryContextStats(MemoryContext context, CommandDest dest, int depth);
PG_EXTERN void MemoryContextCheck(MemoryContext context);
PG_EXTERN bool MemoryContextContains(MemoryContext context, void *pointer);

/*
 * This routine handles the context-type-independent part of memory
 * context creation.  It's intended to be called from context-type-
 * specific creation routines, and noplace else.
 */
PG_EXTERN MemoryContext MemoryContextCreate(NodeTag tag, Size size,
					MemoryContextMethods *methods,
					MemoryContext parent,
					const char *name);


/*
 * Memory-context-type-specific functions
 */

/* aset.c */
PG_EXTERN MemoryContext AllocSetContextCreate(MemoryContext parent,
					  const char *name,
					  Size minContextSize,
					  Size initBlockSize,
					  Size maxBlockSize);
/* subset.c */
PG_EXTERN MemoryContext SubSetContextCreate(MemoryContext parent,
					  const char *name);
/*umemset.c*/
/* not ready yet
 * PG_EXTERN MemoryContext UmemSetContextCreate(MemoryContext parent,
					  const char *name);
*/
 /*
 * Recommended default alloc parameters, suitable for "ordinary" contexts
 * that might hold quite a lot of data.
 */
#define ALLOCSET_DEFAULT_MINSIZE   (8 * 1024)
#define ALLOCSET_DEFAULT_INITSIZE  (8 * 1024)
#define ALLOCSET_DEFAULT_MAXSIZE   (8 * 1024 * 1024)

#endif   /* MEMUTILS_H */
