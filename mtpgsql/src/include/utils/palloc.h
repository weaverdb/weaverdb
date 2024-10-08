/*-------------------------------------------------------------------------
 *
 * palloc.h
 *	  POSTGRES memory allocator definitions.
 *
 * This file contains the basic memory allocation interface that is
 * needed by almost every backend module.  It is included directly by
 * postgres.h, so the definitions here are automatically available
 * everywhere.	Keep it lean!
 *
 * Memory allocation occurs within "contexts".	Every chunk obtained from
 * palloc()/MemoryContextAlloc() is allocated within a specific context.
 * The entire contents of a context can be freed easily and quickly by
 * resetting or deleting the context --- this is both faster and less
 * prone to memory-leakage bugs than releasing chunks individually.
 * We organize contexts into context trees to allow fine-grain control
 * over chunk lifetime while preserving the certainty that we will free
 * everything that should be freed.  See utils/mmgr/README for more info.
 *
 *
 * Portions Copyright (c) 2000-2024, Myron Scott  <myron@weaverdb.org>
 * Portions Copyright (c) 1996-2001, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 *
 *-------------------------------------------------------------------------
 */
#ifndef PALLOC_H
#define PALLOC_H


#include "utils/mcxt.h"

/*
 * Type MemoryContextData is declared in nodes/memnodes.h.	Most users
 * of memory allocation should just treat it as an abstract type, so we
 * do not provide the struct contents here.
 */
 /*
typedef struct MemoryContextData *MemoryContext;
*/
/*
 * CurrentMemoryContext is the default allocation context for palloc().
 * We declare it here so that palloc() can be a macro.	Avoid accessing it
 * directly!  Instead, use MemoryContextSwitchTo() to change the setting.
 */
/*
 * Fundamental memory-allocation operations (more are in utils/memutils.h)
 */
 /*
extern void *MemoryContextAlloc(MemoryContext context, Size size);
*/

#ifdef HAVE_ALLOCINFO
#define palloc(sz)	CallMemoryContextAlloc(MemoryContextGetCurrentContext(), (sz), __FILE__, __LINE__, __FUNCTION__)
#define pfree(pointer)   call_pfree(pointer,  __FILE__, __LINE__, __FUNCTION__)
#else
#define palloc(sz)	MemoryContextAlloc(MemoryContextGetCurrentContext(), (sz))
#endif

PG_EXTERN void* pmerge(void* first,int fl,void* second,int sl);
PG_EXTERN void pclear(void *pointer);
#ifdef HAVE_ALLOCINFO
PG_EXTERN void call_pfree(void *pointer, const char* file, int line, const char* func);
#else
PG_EXTERN void pfree(void *pointer);
#endif

PG_EXTERN void *repalloc(void *pointer, Size size);

/*
 * These are like standard strdup() except the copied string is
 * allocated in a context, not with malloc().
 */
extern char *MemoryContextStrdup(MemoryContext context, const char *string);

#define pstrdup(str)  MemoryContextStrdup(MemoryContextGetCurrentContext(), (str))

#endif   /* PALLOC_H */
