package org.weaverdb.vector;

/**
 * Core vector distance and similarity functions.
 * <p>
 * These are designed to be registered as WeaverDB Java functions using
 * {@link org.weaverdb.FunctionInstaller} so they can be called from SQL.
 *
 * All functions assume vectors have already been validated to have matching dimensions.
 */
public final class VectorFunctions {

    private VectorFunctions() {}

    // ==================== Distance Functions (lower is more similar) ====================

    /** Squared Euclidean distance (L2^2). Often preferred for indexing. */
    public static double squaredEuclidean(Vector a, Vector b) {
        float[] va = a.unsafeData();
        float[] vb = b.unsafeData();
        int dim = va.length;
        double sum = 0.0;
        for (int i = 0; i < dim; i++) {
            double diff = va[i] - vb[i];
            sum += diff * diff;
        }
        return sum;
    }

    /** Euclidean distance (L2). */
    public static double euclidean(Vector a, Vector b) {
        return Math.sqrt(squaredEuclidean(a, b));
    }

    /** Inner product (dot product). Higher is more similar for normalized vectors. */
    public static double innerProduct(Vector a, Vector b) {
        float[] va = a.unsafeData();
        float[] vb = b.unsafeData();
        int dim = va.length;
        double sum = 0.0;
        for (int i = 0; i < dim; i++) {
            sum += va[i] * vb[i];
        }
        return sum;
    }

    // ==================== Similarity Functions (higher is more similar) ====================

    /** Cosine similarity. Returns value between -1 and 1. */
    public static double cosineSimilarity(Vector a, Vector b) {
        double dot = innerProduct(a, b);
        double normA = norm(a);
        double normB = norm(b);
        if (normA == 0.0 || normB == 0.0) {
            return 0.0;
        }
        return dot / (normA * normB);
    }

    /** Cosine distance = 1 - cosine similarity. Useful as a distance metric (0 to 2 range). */
    public static double cosineDistance(Vector a, Vector b) {
        return 1.0 - cosineSimilarity(a, b);
    }

    // ==================== Utility ====================

    public static double norm(Vector v) {
        float[] data = v.unsafeData();
        double sum = 0.0;
        for (float x : data) {
            sum += x * x;
        }
        return Math.sqrt(sum);
    }

    /** L2-normalize a vector in place (returns new normalized vector). */
    public static Vector normalize(Vector v) {
        float[] data = v.unsafeData();
        double n = norm(v);
        if (n == 0.0) {
            return v;
        }
        float[] normalized = new float[data.length];
        double inv = 1.0 / n;
        for (int i = 0; i < data.length; i++) {
            normalized[i] = (float) (data[i] * inv);
        }
        return new Vector(normalized);
    }

    /**
     * Batch version of squaredEuclidean for simple brute-force search.
     * Returns distances in the same order as the input array.
     */
    public static double[] squaredEuclideanBatch(Vector query, Vector[] candidates) {
        double[] results = new double[candidates.length];
        for (int i = 0; i < candidates.length; i++) {
            results[i] = squaredEuclidean(query, candidates[i]);
        }
        return results;
    }
}
