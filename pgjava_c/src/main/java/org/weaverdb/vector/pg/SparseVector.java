/*-------------------------------------------------------------------------
 *
 * Sparse embedding codec matching bytea_to_sparsevec / sparsevec_to_bytea.
 *
 *-------------------------------------------------------------------------
 */

package org.weaverdb.vector.pg;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.Objects;

/**
 * Encode/decode sparse embeddings as native-endian {@code byte[]} for SQL
 * {@code bytea_to_sparsevec} / {@code sparsevec_to_bytea}.
 * <p>
 * Layout (matches C / sparsevec_send after vl_len_):
 * {@code int32 dim | int32 nnz | int32 unused(=0) | int32 indices[nnz] | float32 values[nnz]}.
 * Indices are 0-based and strictly ascending; values must be non-zero.
 */
public final class SparseVector {

    public final int dimensions;
    public final int[] indices;
    public final float[] values;

    public SparseVector(int dimensions, int[] indices, float[] values) {
        Objects.requireNonNull(indices, "indices");
        Objects.requireNonNull(values, "values");
        if (dimensions < 1) {
            throw new IllegalArgumentException("sparsevec must have at least 1 dimension");
        }
        if (indices.length != values.length) {
            throw new IllegalArgumentException("indices and values length mismatch");
        }
        if (indices.length > dimensions) {
            throw new IllegalArgumentException("nnz cannot exceed dimensions");
        }
        for (int i = 0; i < indices.length; i++) {
            if (indices[i] < 0 || indices[i] >= dimensions) {
                throw new IllegalArgumentException("sparsevec index out of bounds");
            }
            if (i > 0 && indices[i] <= indices[i - 1]) {
                throw new IllegalArgumentException("sparsevec indices must be strictly ascending");
            }
            if (values[i] == 0f || Float.isNaN(values[i]) || Float.isInfinite(values[i])) {
                throw new IllegalArgumentException("sparsevec values must be finite and non-zero");
            }
        }
        this.dimensions = dimensions;
        this.indices = indices.clone();
        this.values = values.clone();
    }

    public static SparseVector of(int dimensions, int[] indices, float[] values) {
        return new SparseVector(dimensions, indices, values);
    }

    public byte[] encode() {
        int nnz = indices.length;
        ByteBuffer buf = ByteBuffer.allocate(12 + nnz * 4 + nnz * 4);
        buf.order(ByteOrder.nativeOrder());
        buf.putInt(dimensions);
        buf.putInt(nnz);
        buf.putInt(0);
        for (int idx : indices) {
            buf.putInt(idx);
        }
        for (float v : values) {
            buf.putFloat(v);
        }
        return buf.array();
    }

    public static SparseVector decode(byte[] blob) {
        if (blob == null || blob.length < 12) {
            throw new IllegalArgumentException("bytea too short for sparsevec header");
        }
        ByteBuffer buf = ByteBuffer.wrap(blob).order(ByteOrder.nativeOrder());
        int dim = buf.getInt();
        int nnz = buf.getInt();
        int unused = buf.getInt();
        if (unused != 0) {
            throw new IllegalArgumentException("expected unused to be 0, not " + unused);
        }
        int need = 12 + nnz * 4 + nnz * 4;
        if (blob.length != need) {
            throw new IllegalArgumentException("bytea length does not match sparsevec layout");
        }
        int[] indices = new int[nnz];
        float[] values = new float[nnz];
        for (int i = 0; i < nnz; i++) {
            indices[i] = buf.getInt();
        }
        for (int i = 0; i < nnz; i++) {
            values[i] = buf.getFloat();
        }
        return new SparseVector(dim, indices, values);
    }
}
