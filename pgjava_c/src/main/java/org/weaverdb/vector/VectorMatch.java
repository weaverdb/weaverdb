package org.weaverdb.vector;

/**
 * Result from a vector similarity search.
 */
public final class VectorMatch {

    private final String id;
    private final Vector vector;
    private final Object metadata;
    private final double score;
    private final double distance;

    public VectorMatch(String id, Vector vector, Object metadata, double score, double distance) {
        this.id = id;
        this.vector = vector;
        this.metadata = metadata;
        this.score = score;
        this.distance = distance;
    }

    public String id() {
        return id;
    }

    public Vector vector() {
        return vector;
    }

    public Object metadata() {
        return metadata;
    }

    /**
     * Similarity score (higher is better).
     * For cosine similarity this is the cosine value.
     * For distance-based indexes this may be a negated distance.
     */
    public double score() {
        return score;
    }

    /**
     * Raw distance (lower is better). Interpretation depends on the distance function used.
     */
    public double distance() {
        return distance;
    }

    @Override
    public String toString() {
        return "VectorMatch{" +
                "id='" + id + '\'' +
                ", score=" + score +
                ", distance=" + distance +
                ", metadata=" + metadata +
                '}';
    }
}
