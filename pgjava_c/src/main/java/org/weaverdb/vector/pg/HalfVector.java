/*-------------------------------------------------------------------------
 *
 * Packed float16 codec matching bytea_to_halfvec / halfvec_to_bytea.
 *
 *-------------------------------------------------------------------------
 */

package org.weaverdb.vector.pg;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;

/**
 * Encode/decode half-precision embeddings as native-endian {@code byte[]}
 * for SQL {@code bytea_to_halfvec} / {@code halfvec_to_bytea}.
 * <p>
 * Layout: {@code dim * uint16} IEEE-754 binary16, no header (same as C).
 */
public final class HalfVector {

    private HalfVector() {}

    /** Pack floats as native-endian IEEE float16 bytes. */
    public static byte[] encode(float... values) {
        if (values == null || values.length == 0) {
            throw new IllegalArgumentException("half vector must have at least 1 dimension");
        }
        ByteBuffer buf = ByteBuffer.allocate(values.length * Short.BYTES);
        buf.order(ByteOrder.nativeOrder());
        for (float v : values) {
            buf.putShort((short) floatToFloat16Bits(v));
        }
        return buf.array();
    }

    /** Unpack native-endian float16 bytes to float32. */
    public static float[] decode(byte[] blob) {
        if (blob == null || blob.length == 0) {
            throw new IllegalArgumentException("empty half vector blob");
        }
        if (blob.length % Short.BYTES != 0) {
            throw new IllegalArgumentException("bytea length must be a multiple of 2");
        }
        ByteBuffer buf = ByteBuffer.wrap(blob).order(ByteOrder.nativeOrder());
        float[] out = new float[blob.length / Short.BYTES];
        for (int i = 0; i < out.length; i++) {
            out[i] = float16BitsToFloat(buf.getShort() & 0xFFFF);
        }
        return out;
    }

    /**
     * Convert float32 to IEEE-754 binary16 bits (matches typical halfutils rounding).
     */
    static int floatToFloat16Bits(float value) {
        int fbits = Float.floatToRawIntBits(value);
        int sign = (fbits >>> 16) & 0x8000;
        int val = fbits & 0x7FFFFFFF;

        if (val >= 0x7F800000) {
            // Inf / NaN
            if (val == 0x7F800000) {
                return sign | 0x7C00;
            }
            return sign | 0x7E00 | ((val >>> 13) & 0x3FF);
        }
        if (val < 0x38800000) {
            // Subnormal or zero in float16
            if (val < 0x33000000) {
                return sign;
            }
            int exp = (val >>> 23) - 127 + 1;
            int mant = (val & 0x007FFFFF) | 0x00800000;
            int shift = 14 - (exp + 14);
            int half = mant >> (shift + 13 - 10);
            // round to nearest even-ish
            int roundBit = (mant >> (shift + 12 - 10)) & 1;
            int sticky = mant & ((1 << (shift + 12 - 10)) - 1);
            if (roundBit != 0 && (sticky != 0 || (half & 1) != 0)) {
                half++;
            }
            return sign | half;
        }
        // Normal
        int exp = ((val >>> 23) - 127 + 15) << 10;
        int mant = (val >>> 13) & 0x3FF;
        int half = sign | exp | mant;
        // round: look at remaining bits
        int roundBits = val & 0x1FFF;
        if (roundBits > 0x1000 || (roundBits == 0x1000 && (mant & 1) != 0)) {
            half++;
        }
        return half;
    }

    static float float16BitsToFloat(int hbits) {
        int sign = (hbits & 0x8000) << 16;
        int exp = (hbits >> 10) & 0x1F;
        int mant = hbits & 0x3FF;
        int fbits;
        if (exp == 0) {
            if (mant == 0) {
                fbits = sign;
            } else {
                exp = -14;
                while ((mant & 0x400) == 0) {
                    mant <<= 1;
                    exp--;
                }
                mant &= 0x3FF;
                fbits = sign | ((exp + 127) << 23) | (mant << 13);
            }
        } else if (exp == 31) {
            fbits = sign | 0x7F800000 | (mant << 13);
        } else {
            fbits = sign | ((exp - 15 + 127) << 23) | (mant << 13);
        }
        return Float.intBitsToFloat(fbits);
    }
}
