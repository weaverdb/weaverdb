package org.weaverdb.vector;

import java.io.Serializable;
import java.util.Arrays;

/**
 * Immutable dense vector for similarity search.
 * <p>
 * Designed to work cleanly with WeaverDB's Java extension mechanism
 * (stored via serialization as a Java object, or as bytea).
 */
public final class Vector implements Serializable {

    private static final long serialVersionUID = 1L;

    private final float[] data;
    private final int dimension;

    public Vector(float[] values) {
        if (values == null) {
            throw new IllegalArgumentException("Vector data cannot be null");
        }
        this.data = values.clone();
        this.dimension = values.length;
    }

    public Vector(double[] values) {
        if (values == null) {
            throw new IllegalArgumentException("Vector data cannot be null");
        }
        this.data = new float[values.length];
        for (int i = 0; i < values.length; i++) {
            this.data[i] = (float) values[i];
        }
        this.dimension = values.length;
    }

    /** Convenience constructor from a list of floats. */
    public static Vector of(float... values) {
        return new Vector(values);
    }

    public static Vector of(double... values) {
        return new Vector(values);
    }

    public int dimension() {
        return dimension;
    }

    /**
     * Returns a copy of the underlying data.
     * Callers should not modify the returned array.
     */
    public float[] toFloatArray() {
        return data.clone();
    }

    /**
     * Zero-copy view for performance-critical paths.
     * The returned array must not be modified.
     */
    public float[] unsafeData() {
        return data;
    }

    @Override
    public boolean equals(Object o) {
        if (this == o) return true;
        if (!(o instanceof Vector)) return false;
        Vector vector = (Vector) o;
        return Arrays.equals(data, vector.data);
    }

    @Override
    public int hashCode() {
        return Arrays.hashCode(data);
    }

    @Override
    public String toString() {
        if (dimension > 8) {
            return "Vector[dim=" + dimension + ", " + Arrays.toString(Arrays.copyOf(data, 4)) + "...]";
        }
        return "Vector" + Arrays.toString(data);
    }
}
