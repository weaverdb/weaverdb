# WeaverDB Vector Extension (Java-based)

This package provides pgvector-like functionality for WeaverDB, implemented **entirely through Java extensions** rather than modifying the C storage engine.

## Design Philosophy

WeaverDB already has excellent support for calling Java code from SQL (`LANGUAGE 'java'` + `FunctionInstaller`). 

Instead of fighting the ancient Postgres 7 storage engine to add a native `vector` type + index access method (very hard), we take the pragmatic path:

- Store vectors + metadata using normal WeaverDB tables (or Java-serialized objects).
- Perform indexing and similarity search in Java, where we have access to modern libraries and techniques.
- Expose distance functions back into SQL when desired.

This approach is faster to build, easier to extend, and aligns with WeaverDB's existing strengths.

## Current Components (MVP)

| Component              | Purpose                                      | Status     |
|------------------------|----------------------------------------------|------------|
| `Vector`               | Immutable float[] wrapper (Serializable)     | Done       |
| `VectorFunctions`      | cosine, euclidean, inner product, etc.       | Done       |
| `VectorStore`          | High-level search API                        | Done       |
| `InMemoryVectorStore`  | Brute-force exact search (great for <50k)    | Done       |
| `VectorRegistration`   | Easy registration of functions into SQL      | Done       |
| `VectorMatch`          | Search result object                         | Done       |

## Usage Example

```java
// Java side (recommended for most AI use cases)
VectorStore store = new InMemoryVectorStore(VectorFunctions::cosineDistance);

store.add("chunk1", Vector.of(embedding), Map.of("doc", "paper.pdf", "page", 3));

List<VectorMatch> results = store.search(queryVector, 10);
```

```sql
-- Optional: also expose functions to SQL
SELECT cosine_similarity(embedding_col, $query_vec) AS sim
FROM documents
ORDER BY sim DESC
LIMIT 10;
```

## Next Steps / Roadmap

1. **Persistent Vector Storage** — Helpers to create a standard `vectors` table + metadata columns.
2. **Better Serialization** — Efficient `float[]` ↔ `bytea` without full Java object serialization (lower overhead).
3. **Real ANN Index** — Plug in HNSW (Java), Lucene KNN, or a disk-backed structure.
4. **Hybrid Search** — Combine vector similarity + metadata filters efficiently.
5. **Direct float[] binding** — Improve the FFM/JNI layer to pass primitive arrays more efficiently.

## Why This Works Well on WeaverDB

- The Java object loader already supports passing arbitrary `Serializable` objects in and out of the database.
- `FunctionInstaller` makes it trivial to expose Java methods as SQL functions.
- The single-writer + threaded model makes it relatively easy to keep a Java-side index consistent with the DB.

---

This is intentionally a **Java-native** vector extension. It gives you most of what pgvector offers for AI/agent workloads with far less complexity.
