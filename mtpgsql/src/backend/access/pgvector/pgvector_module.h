#ifndef PGVECTOR_MODULE_H
#define PGVECTOR_MODULE_H

/*
 * One-time pgvector setup (CPU dispatch, HNSW/IVF env, lock tranche).
 * Safe to call from any thread; uses pthread_once.
 */
extern void PgvectorModuleInit(void);
extern void PgvectorEnsureInit(void);

#endif
