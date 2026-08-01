package org.weaverdb.vector.pg;

import org.junit.jupiter.api.Assertions;
import org.junit.jupiter.api.Test;

public class PgVectorCodecTest {

    @Test
    public void denseRoundTrip() {
        float[] v = {1.0f, 0.0f, -2.5f};
        Assertions.assertArrayEquals(v, DenseVector.decode(DenseVector.encode(v)));
    }

    @Test
    public void halfRoundTripExactOnes() {
        float[] v = {1.0f, 0.0f, -1.0f};
        float[] got = HalfVector.decode(HalfVector.encode(v));
        Assertions.assertEquals(v.length, got.length);
        for (int i = 0; i < v.length; i++) {
            Assertions.assertEquals(v[i], got[i], 0.0f);
        }
    }

    @Test
    public void sparseRoundTrip() {
        SparseVector s = SparseVector.of(5, new int[] {0, 2}, new float[] {1.0f, 2.0f});
        SparseVector got = SparseVector.decode(s.encode());
        Assertions.assertEquals(s.dimensions, got.dimensions);
        Assertions.assertArrayEquals(s.indices, got.indices);
        Assertions.assertArrayEquals(s.values, got.values);
    }

    @Test
    public void bitRoundTrip() {
        boolean[] bits = {true, false, true, true, false};
        Assertions.assertArrayEquals(bits, BitVector.decode(BitVector.encode(bits)));
    }
}
