package org.weaverdb.vector;

import java.util.List;

/**
 * High-level API for vector storage and similarity search,
 * modeled after the experience developers have with pgvector + an ORM.
 *
 * Implementations can store vectors in WeaverDB while performing
 * indexing and search in Java (the recommended approach for WeaverDB).
 */
public interface VectorStore {

    /** Add or replace a vector with associated metadata. */
    void add(String id, Vector vector, Object metadata);

    /** Remove a vector by id. */
    void remove(String id);

    /**
     * Find the k most similar vectors to the query.
     *
     * @param query     the query vector
     * @param topK      number of results to return
     * @return list of matches ordered by similarity (best first)
     */
    List<VectorMatch> search(Vector query, int topK);

    /**
     * Search with a minimum similarity threshold (for cosine similarity).
     */
    List<VectorMatch> search(Vector query, int topK, double minSimilarity);

    /** Number of vectors currently in the store. */
    int size();

    /** Clear all vectors (useful for testing). */
    void clear();
}
