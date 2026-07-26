package org.weaverdb.vector.example;

import org.weaverdb.vector.*;

import java.util.List;

/**
 * Demonstrates how to use the Java vector extension on top of WeaverDB.
 *
 * This shows the recommended "pgvector-like" experience:
 * - Store vectors + metadata in the database (or just in Java for now)
 * - Use a high-level VectorStore API for search
 * - Optionally register functions so you can also query from SQL
 */
public class VectorExample {

    public static void main(String[] args) throws Exception {
        // 1. Create a simple in-memory vector store (brute force, exact results)
        VectorStore store = new InMemoryVectorStore(InMemoryVectorStore.DistanceStrategy.COSINE_DISTANCE);

        // 2. Add some vectors with metadata
        store.add("doc1", Vector.of(0.1f, 0.2f, 0.3f), "First document");
        store.add("doc2", Vector.of(0.4f, 0.5f, 0.6f), "Second document");
        store.add("doc3", Vector.of(0.9f, 0.8f, 0.7f), "Third document");

        // 3. Search
        Vector query = Vector.of(0.35f, 0.45f, 0.55f);
        List<VectorMatch> results = store.search(query, 3);

        System.out.println("Top matches for query:");
        for (VectorMatch match : results) {
            System.out.printf("  %s -> score=%.4f, metadata=%s%n",
                    match.id(), match.score(), match.metadata());
        }

        // 4. (Optional) Register functions with a real WeaverDB connection
        // so they become callable from SQL:
        //
        // VectorRegistration.register(dbReference);
        //
        // Then from SQL you can do things like:
        //   SELECT id, cosine_similarity(embedding, $query) AS sim
        //   FROM documents
        //   ORDER BY sim DESC
        //   LIMIT 10;
    }
}
