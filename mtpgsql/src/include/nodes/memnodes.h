/*-------------------------------------------------------------------------
 *
 * memnodes.h
 *	  POSTGRES memory context node definitions.
 *
 *
 * Portions Copyright (c) 1996-2001, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 *
 *-------------------------------------------------------------------------
 */
#ifndef MEMNODES_H
#define MEMNODES_H

#include "nodes/nodes.h"

/*
 * MemoryContext
 *		A logical context in which memory allocations occur.
 *
 * MemoryContext itself is an abstract type that can have multiple
 * implementations, though for now we have only AllocSetContext.
 * The function pointers in MemoryContextMethods define one specific
 * implementation of MemoryContext --- they are a virtual function table
 * in C++ terms.
 *
 * Node types that are actual implementations of memory contexts must
 * begin with the same fields as MemoryContext.
 *
 * Note: for largely historical reasons, typedef MemoryContext is a pointer
 * to the context struct rather than the struct type itself.
 */

typedef struct MemoryContextMethods
{
#ifdef HAVE_ALLOCINFO
	void	   *(*alloc) (MemoryContext context, Size size,const char* file, int line, const char* func);
	/* call this free_p in case someone #define's free() */
	void		(*free_p) (MemoryContext context, void *pointerconst,const char* file, int line, const char* func);
#else
	void	   *(*alloc) (MemoryContext context, Size size);
	/* call this free_p in case someone #define's free() */
	void		(*free_p) (MemoryContext context, void *pointer);
#endif
	void	   *(*realloc) (MemoryContext context, void *pointer, Size size);
	void		(*init) (MemoryContext context);
	void		(*reset) (MemoryContext context);
	void		(*delete) (MemoryContext context);
#ifdef MEMORY_CONTEXT_CHECKING
	void		(*check) (MemoryContext context);
#endif
	size_t		(*stats) (MemoryContext context, char* describe, int size);
} MemoryContextMethods;


typedef struct MemoryContextData
{
	NodeTag		type;			/* identifies exact kind of context */
	MemoryContextMethods *methods;		/* virtual function table */
	MemoryContext parent;		/* NULL if no parent (toplevel context) */
	MemoryContext firstchild;	/* head of linked list of children */
	MemoryContext nextchild;	/* next child of same parent */
	char	   *name;			/* context name (just for debugging) */
} MemoryContextData;

/* utils/palloc.h contains typedef struct MemoryContextData *MemoryContext */


/*
 * MemoryContextIsValid
 *		True iff memory context is valid.
 *
 * Add new context types to the set accepted by this macro.
 */
#define MemoryContextIsValid(context) \
	((context) != NULL)

#endif   /* MEMNODES_H */
