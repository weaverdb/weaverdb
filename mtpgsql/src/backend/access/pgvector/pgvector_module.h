#ifndef PGVECTOR_MODULE_H
#define PGVECTOR_MODULE_H

/*
 * One-time pgvector setup (CPU dispatch, HNSW/IVF env, lock tranche).
 * Invoked from InitPostgres and initweaverbackend; pthread_once makes repeat calls safe.
 */
extern void PgvectorModuleInit(void);

#endif
