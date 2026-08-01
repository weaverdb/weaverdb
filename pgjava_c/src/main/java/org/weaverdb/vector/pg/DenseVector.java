/*-------------------------------------------------------------------------
 *
 * Packed float32 codec matching bytea_to_vector / vector_to_bytea.
 *
 *-------------------------------------------------------------------------
 */

package org.weaverdb.vector.pg;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;

/**
 * Encode/decode dense float32 embeddings as native-endian {@code byte[]}
 * for SQL {@code bytea_to_vector} / {@code vector_to_bytea}.
 */
public final class DenseVector {

    private DenseVector() {}

    /** Pack floats as native-endian float32 bytes (no header). */
    public static byte[] encode(float... values) {
        if (values == null || values.length == 0) {
            throw new IllegalArgumentException("dense vector must have at least 1 dimension");
        }
        ByteBuffer buf = ByteBuffer.allocate(values.length * Float.BYTES);
        buf.order(ByteOrder.nativeOrder());
        for (float v : values) {
            buf.putFloat(v);
        }
        return buf.array();
    }

    /** Unpack native-endian float32 bytes. */
    public static float[] decode(byte[] blob) {
        if (blob == null || blob.length == 0) {
            throw new IllegalArgumentException("empty dense vector blob");
        }
        if (blob.length % Float.BYTES != 0) {
            throw new IllegalArgumentException("bytea length must be a multiple of 4");
        }
        ByteBuffer buf = ByteBuffer.wrap(blob).order(ByteOrder.nativeOrder());
        float[] out = new float[blob.length / Float.BYTES];
        for (int i = 0; i < out.length; i++) {
            out[i] = buf.getFloat();
        }
        return out;
    }

    public static int dimensions(byte[] blob) {
        return decode(blob).length;
    }
}
