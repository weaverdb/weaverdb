/*
 *  subset.c
 *  axon
 *
 *  Created by Myron Scott on 6/25/05.
 *  Copyright 2005 __MyCompanyName__. All rights reserved.
 *	this is like aset but uses palloced memory from the parent
 */



#include "postgres.h"


#include "utils/memutils.h"


/*
 * AllocSetContext is our standard implementation of MemoryContext.
 */
typedef struct UmemSetContext
{
	MemoryContextData header;	/* Standard memory-context fields */
        umem_cache_t*     pointer_cache;
} UmemSetContext;


/*
 * These functions implement the MemoryContext API for AllocSet contexts.
 */
static void *UmemSetAlloc(MemoryContext context, Size size);
static void UmemSetFree(MemoryContext context, void *pointer);
static void *UmemSetRealloc(MemoryContext context, void *pointer, Size size);
static void UmemSetInit(MemoryContext context);
static void UmemSetReset(MemoryContext context);
static void UmemSetDelete(MemoryContext context);

#ifdef MEMORY_CONTEXT_CHECKING
static void UmemSetCheck(MemoryContext context);
#endif
static size_t UmemSetStats(MemoryContext context, char* describe, int size);

/*
 * This is the virtual function table for AllocSet contexts.
 */
static MemoryContextMethods UmemSetMethods = {
	UmemSetAlloc,
	UmemSetFree,
	UmemSetRealloc,
	UmemSetInit,
	UmemSetReset,
	UmemSetDelete,
#ifdef MEMORY_CONTEXT_CHECKING
	UmemSetCheck,
#endif
	UmemSetStats
};


/*
 * Public routines
 */

MemoryContext
UmemSetContextCreate(MemoryContext parent,
					  const char *name)
{
	UmemSetContext*	context;

	/* Do the type-independent part of context creation */
	context = (UmemSetContext*) MemoryContextCreate(T_UmemSetContext,
											 sizeof(UmemSetContext),
											 &UmemSetMethods,
											 parent,
											 name);

	return (MemoryContext) context;
}


static void
UmemSetInit(MemoryContext context)
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
UmemSetReset(MemoryContext context)
{
	UmemSetContext*  sub = (UmemSetContext*)context;
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
	sub->alloced_pointers = MemoryContextAlloc(sub->header.parent,sizeof(void*) * sub->highmark);
	memset(sub->alloced_pointers,0x00,sizeof(void*) * sub->highmark);
	sub->map_size = sub->highmark;
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
UmemSetDelete(MemoryContext context)
{
	UmemSetContext*  sub = (UmemSetContext*)context;
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
static void *
UmemSetAlloc(MemoryContext context, Size size)
{
	UmemSetContext*  sub = (UmemSetContext*)context;
	void* pointer = MemoryContextAlloc(sub->header.parent,size);
	int x;
	void** store = sub->alloced_pointers;
	for ( x=0;x<sub->map_size;x++ ) {
		if ( *store == NULL ) break;
		store++;
	}
	if ( x == sub->map_size ) {
		void** save = sub->alloced_pointers;
		sub->alloced_pointers = MemoryContextAlloc(sub->header.parent,sizeof(void*) * (sub->map_size * 2) );
		memset(sub->alloced_pointers,0x00,sizeof(void*) * sub->map_size * 2);
		memmove(sub->alloced_pointers, save, sizeof(void*) * (sub->map_size));
		sub->map_size *= 2;
		pfree(save);
	}
	sub->alloced_pointers[x] = pointer;
	if ( x > sub->highmark ) sub->highmark = x;
	GetMemoryContext(pointer) = context;
	return pointer;
}

/*
 * AllocSetFree
 *		Frees allocated memory; memory is removed from the set.
 */
static void
UmemSetFree(MemoryContext context, void *pointer)
{
	UmemSetContext*  sub = (UmemSetContext*)context;
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
UmemSetRealloc(MemoryContext context, void *pointer, Size size)
{
	UmemSetContext*  sub = (UmemSetContext*)context;
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
	return save;
}

/*
 * AllocSetStats
 *		Displays stats about memory consumption of an allocset.
 */
static size_t
UmemSetStats(MemoryContext context, char* describe, int size)
{	
	int x = 0;
	Size hold = 0;
	UmemSetContext* sub = (UmemSetContext*)context;
        void** store = sub->alloced_pointers;
        for ( x=0;x<sub->map_size;x++ ) {
                if (*store != NULL) {
                       hold += GetMemorySize(*store); 
                }
                store++;
        }		
        if (describe != NULL) {
            snprintf(describe, size, "%s: %ld used from %s\n",
			sub->header.name,hold,sub->header.parent->name);
        } else {
            userlog(
		"%s: %ld used from %s\n",
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
UmemSetCheck(MemoryContext context)
{

}

#endif   /* MEMORY_CONTEXT_CHECKING */
