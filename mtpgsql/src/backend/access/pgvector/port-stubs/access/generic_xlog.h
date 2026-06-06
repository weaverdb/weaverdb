/* Stub access/generic_xlog.h for pgvector (used in ivfflat index build paths for WAL-logging pages).
 * In WeaverDB we don't have the generic xlog facility from later PG; stub so headers compile.
 * The functions are only called in code paths we can leave as no-op for the initial integration.
 */
#ifndef ACCESS_GENERIC_XLOG_H
#define ACCESS_GENERIC_XLOG_H

#include "access/xlog.h"   /* if project has */

typedef struct GenericXLogState GenericXLogState;

/* Stubs for the API pgvector ivf uses */
static inline GenericXLogState *
GenericXLogStart(Relation rel)
{
	(void) rel;
	return (GenericXLogState *) 0;
}

static inline Page
GenericXLogRegisterBuffer(GenericXLogState *state, Buffer buf, int flags)
{
	(void) state; (void) buf; (void) flags;
	return (Page) 0;
}

static inline void
GenericXLogFinish(GenericXLogState *state)
{
	(void) state;
}

#define GENERIC_XLOG_FULL_IMAGE 0

#endif /* ACCESS_GENERIC_XLOG_H */
