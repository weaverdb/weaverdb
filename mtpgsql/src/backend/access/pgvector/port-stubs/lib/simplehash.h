/* Minimal stub for lib/simplehash.h used by modern pgvector hnsw code.
 * The actual simplehash is a generated hash table template in PG.
 * For Weaver build we only need the types referenced not to explode.
 */
#ifndef LIB_SIMPLEHASH_H
#define LIB_SIMPLEHASH_H

/* Opaque types referenced from hnsw.h */
typedef struct simplehash simplehash;
typedef struct simplehash_iterator simplehash_iterator;

#endif /* LIB_SIMPLEHASH_H */
