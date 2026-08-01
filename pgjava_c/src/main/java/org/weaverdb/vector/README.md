# WeaverDB Vector Support

Native **pgvector** in the WeaverDB C engine is the index and distance engine
(HNSW / IVFFlat, `vector` / `halfvec` / `sparsevec` / `bit` types, operators
`<->` / `<#>` / `<=>` / `<~>` / `<%>`).

This Java package provides helpers around that engine:

| Component | Purpose |
|-----------|---------|
| `org.weaverdb.vector.pg.*` | Codecs: float/half/sparse/bit ↔ `byte[]` matching `bytea_to_*` / `*_to_bytea` |
| `Vector` / `VectorFunctions` | Lightweight float[] helpers (optional app-side math) |
| `InMemoryVectorStore` | **In-memory / demos only** — not a substitute for native ANN indexes |

## Recommended path: bind blobs + functional indexes

Apps typically store embeddings as `bytea` (or convert on insert) and index with
`bytea_to_vector(emb)` (or halfvec / sparsevec / bit equivalents):

```java
import org.weaverdb.vector.pg.DenseVector;

byte[] emb = DenseVector.encode(0.1f, 0.2f, 0.3f);

try (Statement s = conn.statement(
        "insert into docs (id, emb) values ($id, $emb)")) {
    s.linkInput("id", Integer.class).set(1);
    s.linkInput("emb", byte[].class).set(emb);
    s.execute();
}

// Functional ANN index (SQL):
// create index on docs using hnsw (bytea_to_vector(emb) vector_l2_ops);

try (Statement s = conn.statement(
        "select id from docs order by bytea_to_vector(emb) <-> bytea_to_vector($q) limit 10")) {
    s.linkInput("q", byte[].class).set(DenseVector.encode(0.1f, 0.2f, 0.3f));
    // ...
}
```

### Codec layouts (native endian)

| Type | SQL | `byte[]` layout |
|------|-----|-----------------|
| Dense float32 | `bytea_to_vector` / `vector_to_bytea` | `float32[dim]` |
| Half float16 | `bytea_to_halfvec` / `halfvec_to_bytea` | `float16[dim]` |
| Sparse | `bytea_to_sparsevec` / `sparsevec_to_bytea` | `dim\|nnz\|0\|indices[nnz]\|values[nnz]` |
| Bit | `bytea_to_bit` / `bit_to_bytea` | `int32 bitlen \| MSB-packed bits` |

Large `bytea` / vector columns use `attstorage='e'`: small values stay inline;
oversized tuples span via blob-indirect (`ISINDIRECT`) storage automatically.

## Deprecated framing

`InMemoryVectorStore` remains for unit tests and tiny datasets. Prefer native
pgvector indexes for production similarity search.
