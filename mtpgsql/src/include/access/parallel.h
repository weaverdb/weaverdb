/* Parallel context — Weaver has no upstream DSM ParallelContext.
 * HNSW / IVFFlat builds use in-process pthread workers
 * (BuildGraphParallel / AssignTuplesParallel).
 */
#ifndef ACCESS_PARALLEL_H
#define ACCESS_PARALLEL_H

#define ParallelContext void

#endif /* ACCESS_PARALLEL_H */