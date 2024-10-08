/*-------------------------------------------------------------------------
 *
 * subset.c
 *	  memory context piggybacking a parent context
 *
 * Portions Copyright (c) 2000-2024, Myron Scott  <myron@weaverdb.org>
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *
 *
 *-------------------------------------------------------------------------
 */




#include "postgres.h"

#include "env/env.h"
#include "utils/memutils.h"


/*
 * AllocSetContext is our standard implementation of MemoryContext.
 */
typedef struct SubSetContext
{
	MemoryContextData header;	/* Standard memory-context fields */
	/* Info about storage allocated in this context: */
	void**		alloced_pointers;
	int			map_size;
	int 		highmark;
} SubSetContext;


/*
 * These functions implement the MemoryContext API for AllocSet contexts.
 */
#ifdef HAVE_ALLOCINFO
static void *SubSetAlloc(MemoryContext context, Size size, const char* file, int line, const char* func);
static void SubSetFree(MemoryContext context, void *pointer, const char* file, int line, const char* func);
#else
static void *SubSetAlloc(MemoryContext context, Size size);
static void SubSetFree(MemoryContext context, void *pointer);
#endif
static void *SubSetRealloc(MemoryContext context, void *pointer, Size size);
static void SubSetInit(MemoryContext context);
static void SubSetReset(MemoryContext context);
static void SubSetDelete(MemoryContext context);

#ifdef MEMORY_CONTEXT_CHECKING
static void SubSetCheck(MemoryContext context);
#endif
static size_t SubSetStats(MemoryContext context, char* describe, int size);

/*
 * This is the virtual function table for AllocSet contexts.
 */
static MemoryContextMethods SubSetMethods = {
	SubSetAlloc,
	SubSetFree,
	SubSetRealloc,
	SubSetInit,
	SubSetReset,
	SubSetDelete,
#ifdef MEMORY_CONTEXT_CHECKING
	SubSetCheck,
#endif
	SubSetStats
};

/*
 * Public routines
 */


/*
 * AllocSetContextCreate
 *		Create a new AllocSet context.
 *
 * parent: parent context, or NULL if top-level context
 * name: name of context (for debugging --- string will be copied)
 * minContextSize: minimum context size
 * initBlockSize: initial allocation block size
 * maxBlockSize: maximum allocation block size
 */
MemoryContext
SubSetContextCreate(MemoryContext parent,const char *name)
{
#ifdef SUBSETISALLOC
    return AllocSetContextCreate(parent,name,            
            ALLOCSET_DEFAULT_MINSIZE,
            ALLOCSET_DEFAULT_INITSIZE,
            ALLOCSET_DEFAULT_MAXSIZE);
#else
	SubSetContext*	context;
        Assert( parent->type != T_SubSetContext );
	/* Do the type-independent part of context creation */
        MemoryContext old = MemoryContextSwitchTo(parent);
	context = (SubSetContext*) MemoryContextCreate(T_SubSetContext,
										sizeof(SubSetContext),
										&SubSetMethods,
										parent,
										name);
                                     
	context->alloced_pointers = palloc(10 * sizeof(void*));
	memset(context->alloced_pointers,0x00,sizeof(void*) * 10);
	context->map_size = 10;
	context->highmark = 1;
        MemoryContextSwitchTo(old);
	return (MemoryContext) context;
#endif
}

/*
 * AllocSetInit
 *		Context-type-specific initialization routine.
 *
 * This is called by MemoryContextCreate() after setting up the
 * generic MemoryContext fields and before linking the new context
 * into the context tree.  We must do whatever is needed to make the
 * new context minimally valid for deletion.  We must *not* risk
 * failure --- thus, for example, allocating more memory is not cool.
 * (AllocSetContextCreate can allocate memory when it gets control
 * back, however.)
 */
static void
SubSetInit(MemoryContext context)
{
	/*
	 * Since MemoryContextCreate already zeroed the context node, we don't
	 * have to do anything here: it's already OK.
	 */
}

/*
 * AllocSetReset
 *		Frees all memory which is allocated in the given set.
 *
 * Actually, this routine has some discretion about what to do.
 * It should mark all allocated chunks freed, but it need not
 * necessarily give back all the resources the set owns.  Our
 * actual implementation is that we hang on to any "keeper"
 * block specified for the set.
 */
static void
SubSetReset(MemoryContext context)
{
    SubSetContext*  sub = (SubSetContext*)context;
	int x = 0;
	void**   pointer = sub->alloced_pointers;
	for (x=0;x<sub->map_size;x++) {
		if ( *pointer != NULL ) {
			GetMemoryContext(*pointer) = sub->header.parent;	
			pfree(*pointer);
			*pointer = NULL;
		}
		pointer++;
	}
	pfree(sub->alloced_pointers);
        MemoryContext old = MemoryContextSwitchTo(sub->header.parent);

	sub->alloced_pointers = palloc(sizeof(void*) * sub->highmark);
	memset(sub->alloced_pointers,0x00,sizeof(void*) * sub->highmark);
	sub->map_size = sub->highmark;

        MemoryContextSwitchTo(old);
}

/*
 * SetDelete
 *		Frees all memory which is allocated in the given set,
 *		in preparation for deletion of the set.
 *
 * Unlike AllocSetReset, this *must* free all resources of the set.
 * But note we are not responsible for deleting the context node itself.
 */
static void
SubSetDelete(MemoryContext context)
{
    SubSetContext*  sub = (SubSetContext*)context;
	int x = 0;
	void**   pointer = sub->alloced_pointers;
	for (x=0;x<sub->map_size;x++) {
		if ( *pointer != NULL ) {
			GetMemoryContext(*pointer) = sub->header.parent;
			pfree(*pointer);
			*pointer = NULL;
		}
		pointer++;
	}
	pfree(sub->alloced_pointers);
	sub->map_size = 0;
}

/*
 * AllocSetAlloc
 *		Returns pointer to allocated memory of given size; memory is added
 *		to the set.
 */
#ifdef HAVE_ALLOCINFO
static void *SubSetAlloc(MemoryContext context, Size size, const char* file, int line, const char* func)
#else
static void *
SubSetAlloc(MemoryContext context, Size size)
#endif
{
    SubSetContext*  sub = (SubSetContext*)context;
    MemoryContext old = MemoryContextSwitchTo(sub->header.parent);
	void* pointer = palloc(size);
	int x;
	void** store = sub->alloced_pointers;
	for ( x=0;x<sub->map_size;x++ ) {
		if ( *store == NULL ) break;
		store++;
	}
	if ( x == sub->map_size ) {
		void** save = sub->alloced_pointers;
		sub->alloced_pointers = palloc(sizeof(void*) * (sub->map_size * 2));
		memset(sub->alloced_pointers,0x00,sizeof(void*) * sub->map_size * 2);
		memmove(sub->alloced_pointers, save, sizeof(void*) * (sub->map_size));
		sub->map_size *= 2;
		pfree(save);
	}
	sub->alloced_pointers[x] = pointer;
	if ( x > sub->highmark ) sub->highmark = x;
	GetMemoryContext(pointer) = context;
    MemoryContextSwitchTo(old);
	return pointer;
}

/*
 * AllocSetFree
 *		Frees allocated memory; memory is removed from the set.
 */
#ifdef HAVE_ALLOCINFO
static void
SubSetFree(MemoryContext context, void *pointer, const char* file, int line, const char* func)
#else
static void
SubSetFree(MemoryContext context, void *pointer)
#endif
{
    SubSetContext*  sub = (SubSetContext*)context;
	int x = 0;
	void** store = sub->alloced_pointers;
	for ( x=0;x<sub->map_size;x++ ) {
		if ( pointer == *store ) {
			*store = NULL;
			break;
		}
		store++;
	}
	GetMemoryContext(pointer) = sub->header.parent;
	pfree(pointer);
}

/*
 * AllocSetRealloc
 *		Returns new pointer to allocated memory of given size; this memory
 *		is added to the set.  Memory associated with given pointer is copied
 *		into the new memory, and the old memory is freed.
 */
static void *
SubSetRealloc(MemoryContext context, void *pointer, Size size)
{
	SubSetContext*  sub = (SubSetContext*)context;
	void* save;
	GetMemoryContext(pointer) = sub->header.parent;
	save = repalloc(pointer,size);
        int x = 0;
        void** store = sub->alloced_pointers;
        for ( x=0;x<sub->map_size;x++ ) {
                if ( pointer == *store ) {
                        *store = save;
                        break;
                }
                store++;
        }
	GetMemoryContext(save) = context;
	return save;
}

/*
 * AllocSetStats
 *		Displays stats about memory consumption of an allocset.
 */
static size_t
SubSetStats(MemoryContext context, char* describe, int size)
{	
	int x = 0;
	Size hold = 0;
	SubSetContext* sub = (SubSetContext*)context;
        void** store = sub->alloced_pointers;
        for ( x=0;x<sub->map_size;x++ ) {
                if (*store != NULL) {
                       hold += GetMemorySize(*store); 
                }
                store++;
        }
        if (describe != NULL) {
            snprintf(describe, size, "::%u used from %s",
                     (uint32)hold,sub->header.parent->name);
        } else {
            user_log("%s: %ld used from %s",
			sub->header.name,hold,sub->header.parent->name);
        }
	return 0;
}


#ifdef MEMORY_CONTEXT_CHECKING

/*
 * AllocSetCheck
 *		Walk through chunks and check consistency of memory.
 *
 * NOTE: report errors as NOTICE, *not* ERROR or FATAL.  Otherwise you'll
 * find yourself in an infinite loop when trouble occurs, because this
 * routine will be entered again when elog cleanup tries to release memory!
 */
static void
SubSetCheck(MemoryContext context)
{

}

#endif   /* MEMORY_CONTEXT_CHECKING */
