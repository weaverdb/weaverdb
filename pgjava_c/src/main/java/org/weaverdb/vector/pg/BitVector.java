/*-------------------------------------------------------------------------
 *
 * Packed bit codec matching bytea_to_bit / bit_to_bytea.
 *
 *-------------------------------------------------------------------------
 */

package org.weaverdb.vector.pg;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.BitSet;
import java.util.Objects;

/**
 * Encode/decode bit embeddings as native-endian {@code byte[]} for SQL
 * {@code bytea_to_bit} / {@code bit_to_bytea}.
 * <p>
 * Layout: {@code int32 bit_length | MSB-first packed bits}
 * (same packing as PostgreSQL varbit / InitBitVector).
 */
public final class BitVector {

    private BitVector() {}

    /**
     * Encode bits from a boolean array (index 0 = MSB of first data byte).
     */
    public static byte[] encode(boolean... bits) {
        if (bits == null || bits.length == 0) {
            throw new IllegalArgumentException("bit length must be at least 1");
        }
        int bytes = (bits.length + 7) / 8;
        byte[] packed = new byte[bytes];
        for (int i = 0; i < bits.length; i++) {
            if (bits[i]) {
                packed[i / 8] |= (byte) (0x80 >>> (i % 8));
            }
        }
        ByteBuffer buf = ByteBuffer.allocate(Integer.BYTES + bytes);
        buf.order(ByteOrder.nativeOrder());
        buf.putInt(bits.length);
        buf.put(packed);
        return buf.array();
    }

    /**
     * Encode from a {@link BitSet}; bits {@code 0..bitLength-1} are used
     * with bit 0 as the MSB of the first packed byte.
     */
    public static byte[] encode(BitSet bits, int bitLength) {
        Objects.requireNonNull(bits, "bits");
        if (bitLength < 1) {
            throw new IllegalArgumentException("bit length must be at least 1");
        }
        boolean[] arr = new boolean[bitLength];
        for (int i = 0; i < bitLength; i++) {
            arr[i] = bits.get(i);
        }
        return encode(arr);
    }

    /** Decode to a boolean array (index 0 = MSB of first data byte). */
    public static boolean[] decode(byte[] blob) {
        if (blob == null || blob.length < Integer.BYTES) {
            throw new IllegalArgumentException("bytea too short for bit length header");
        }
        ByteBuffer buf = ByteBuffer.wrap(blob).order(ByteOrder.nativeOrder());
        int bitLength = buf.getInt();
        if (bitLength < 1) {
            throw new IllegalArgumentException("bit length must be at least 1");
        }
        int bytes = (bitLength + 7) / 8;
        if (blob.length != Integer.BYTES + bytes) {
            throw new IllegalArgumentException("bytea length does not match bit length");
        }
        byte[] packed = new byte[bytes];
        buf.get(packed);
        boolean[] out = new boolean[bitLength];
        for (int i = 0; i < bitLength; i++) {
            out[i] = (packed[i / 8] & (0x80 >>> (i % 8))) != 0;
        }
        return out;
    }

    public static int bitLength(byte[] blob) {
        if (blob == null || blob.length < Integer.BYTES) {
            throw new IllegalArgumentException("bytea too short for bit length header");
        }
        return ByteBuffer.wrap(blob).order(ByteOrder.nativeOrder()).getInt();
    }
}
