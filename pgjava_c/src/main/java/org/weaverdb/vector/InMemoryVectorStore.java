package org.weaverdb.vector;

import java.util.*;
import java.util.concurrent.ConcurrentHashMap;

/**
 * Simple brute-force vector store.
 *
 * Useful for:
 * - Prototyping
 * - Small-to-medium collections (< ~50k vectors)
 * - Exact search (no approximation)
 *
 * For production-scale use, a proper ANN index (HNSW, etc.) should be plugged in.
 */
public class InMemoryVectorStore implements VectorStore {

    private final Map<String, Entry> vectors = new ConcurrentHashMap<>();
    private final DistanceFunction distanceFunction;
    private final DistanceStrategy strategy;

    public InMemoryVectorStore() {
        this(DistanceStrategy.SQUARED_EUCLIDEAN);
    }

    public InMemoryVectorStore(DistanceStrategy strategy) {
        this.strategy = Objects.requireNonNull(strategy);
        this.distanceFunction = strategy.function;
    }

    /** Advanced constructor if you want to supply a custom distance function. */
    public InMemoryVectorStore(DistanceFunction distanceFunction, DistanceStrategy strategy) {
        this.distanceFunction = Objects.requireNonNull(distanceFunction);
        this.strategy = strategy != null ? strategy : DistanceStrategy.CUSTOM;
    }

    @Override
    public void add(String id, Vector vector, Object metadata) {
        vectors.put(id, new Entry(vector, metadata));
    }

    @Override
    public void remove(String id) {
        vectors.remove(id);
    }

    @Override
    public List<VectorMatch> search(Vector query, int topK) {
        return searchInternal(query, topK, Double.NEGATIVE_INFINITY);
    }

    @Override
    public List<VectorMatch> search(Vector query, int topK, double minSimilarity) {
        // Convert similarity threshold to the distance function being used.
        double threshold = (strategy == DistanceStrategy.COSINE_DISTANCE)
                ? (1.0 - minSimilarity)
                : Double.NEGATIVE_INFINITY;
        return searchInternal(query, topK, threshold);
    }

    private List<VectorMatch> searchInternal(Vector query, int topK, double maxDistance) {
        PriorityQueue<VectorMatch> best = new PriorityQueue<>(
                Comparator.comparingDouble(VectorMatch::distance).reversed()
        );

        for (Map.Entry<String, Entry> e : vectors.entrySet()) {
            double dist = distanceFunction.distance(query, e.getValue().vector);
            if (dist <= maxDistance) continue;

            VectorMatch match = new VectorMatch(
                    e.getKey(),
                    e.getValue().vector,
                    e.getValue().metadata,
                    toScore(dist),
                    dist
            );

            best.offer(match);
            if (best.size() > topK) {
                best.poll();
            }
        }

        List<VectorMatch> results = new ArrayList<>(best);
        Collections.reverse(results); // best first
        return results;
    }

    private double toScore(double distance) {
        switch (strategy) {
            case SQUARED_EUCLIDEAN:
            case EUCLIDEAN:
                return -distance;
            case COSINE_DISTANCE:
                return 1.0 - distance;
            default:
                return -distance;
        }
    }

    @Override
    public int size() {
        return vectors.size();
    }

    @Override
    public void clear() {
        vectors.clear();
    }

    private static class Entry {
        final Vector vector;
        final Object metadata;

        Entry(Vector vector, Object metadata) {
            this.vector = vector;
            this.metadata = metadata;
        }
    }

    @FunctionalInterface
    public interface DistanceFunction {
        double distance(Vector a, Vector b);
    }

    /**
     * Built-in distance strategies. This makes it easy to know which metric
     * the store is using for score conversion and filtering.
     */
    public enum DistanceStrategy {
        SQUARED_EUCLIDEAN(VectorFunctions::squaredEuclidean),
        EUCLIDEAN(VectorFunctions::euclidean),
        COSINE_DISTANCE(VectorFunctions::cosineDistance),
        INNER_PRODUCT(VectorFunctions::innerProduct),
        CUSTOM(null); // used when supplying a completely custom function

        final DistanceFunction function;

        DistanceStrategy(DistanceFunction function) {
            this.function = function;
        }
    }
}
