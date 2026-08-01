#ifndef PGVECTOR_SESSION_VARS_H
#define PGVECTOR_SESSION_VARS_H

/*
 * PG7 SET/SHOW/RESET handlers for pgvector search knobs.
 * Writes through HnswGetEnv() / IvfflatGetEnv() (already read by scans/cost).
 */
PG_EXTERN bool parse_hnsw_ef_search(char *value);
PG_EXTERN bool show_hnsw_ef_search(void);
PG_EXTERN bool reset_hnsw_ef_search(void);

PG_EXTERN bool parse_hnsw_iterative_scan(char *value);
PG_EXTERN bool show_hnsw_iterative_scan(void);
PG_EXTERN bool reset_hnsw_iterative_scan(void);

PG_EXTERN bool parse_hnsw_max_scan_tuples(char *value);
PG_EXTERN bool show_hnsw_max_scan_tuples(void);
PG_EXTERN bool reset_hnsw_max_scan_tuples(void);

PG_EXTERN bool parse_hnsw_scan_mem_multiplier(char *value);
PG_EXTERN bool show_hnsw_scan_mem_multiplier(void);
PG_EXTERN bool reset_hnsw_scan_mem_multiplier(void);

PG_EXTERN bool parse_ivfflat_probes(char *value);
PG_EXTERN bool show_ivfflat_probes(void);
PG_EXTERN bool reset_ivfflat_probes(void);

PG_EXTERN bool parse_ivfflat_iterative_scan(char *value);
PG_EXTERN bool show_ivfflat_iterative_scan(void);
PG_EXTERN bool reset_ivfflat_iterative_scan(void);

PG_EXTERN bool parse_ivfflat_max_probes(char *value);
PG_EXTERN bool show_ivfflat_max_probes(void);
PG_EXTERN bool reset_ivfflat_max_probes(void);

PG_EXTERN bool parse_hnsw_build_workers(char *value);
PG_EXTERN bool show_hnsw_build_workers(void);
PG_EXTERN bool reset_hnsw_build_workers(void);

PG_EXTERN bool parse_ivfflat_assign_workers(char *value);
PG_EXTERN bool show_ivfflat_assign_workers(void);
PG_EXTERN bool reset_ivfflat_assign_workers(void);

#endif /* PGVECTOR_SESSION_VARS_H */
