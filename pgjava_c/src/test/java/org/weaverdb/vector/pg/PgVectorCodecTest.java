package org.weaverdb.vector.pg;

import java.util.BitSet;
import org.junit.jupiter.api.Assertions;
import org.junit.jupiter.api.Test;

public class PgVectorCodecTest {

    @Test
    public void denseRoundTrip() {
        float[] v = {1.0f, 0.0f, -2.5f};
        Assertions.assertArrayEquals(v, DenseVector.decode(DenseVector.encode(v)));
    }

    @Test
    public void denseSingleDimension() {
        float[] v = {42.0f};
        Assertions.assertArrayEquals(v, DenseVector.decode(DenseVector.encode(v)));
        Assertions.assertEquals(1, DenseVector.dimensions(DenseVector.encode(v)));
    }

    @Test
    public void denseEightDimensionsWithNegatives() {
        float[] v = {0.0f, -1.0f, 2.5f, -3.25f, 4.0f, 0.125f, -0.5f, 8.0f};
        byte[] blob = DenseVector.encode(v);
        Assertions.assertEquals(8 * Float.BYTES, blob.length);
        Assertions.assertEquals(8, DenseVector.dimensions(blob));
        Assertions.assertArrayEquals(v, DenseVector.decode(blob));
    }

    @Test
    public void denseRejectsEmpty() {
        Assertions.assertThrows(IllegalArgumentException.class, () -> DenseVector.encode());
        Assertions.assertThrows(IllegalArgumentException.class, () -> DenseVector.decode(new byte[0]));
        Assertions.assertThrows(IllegalArgumentException.class, () -> DenseVector.decode(new byte[3]));
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
    public void halfSingleAndEightDimensions() {
        float[] one = {1.0f};
        Assertions.assertArrayEquals(one, HalfVector.decode(HalfVector.encode(one)), 0.0f);

        float[] eight = {1.0f, 0.0f, -1.0f, 0.5f, -0.5f, 2.0f, -2.0f, 0.0f};
        byte[] blob = HalfVector.encode(eight);
        Assertions.assertEquals(8 * Short.BYTES, blob.length);
        float[] got = HalfVector.decode(blob);
        Assertions.assertEquals(eight.length, got.length);
        for (int i = 0; i < eight.length; i++) {
            Assertions.assertEquals(eight[i], got[i], 0.0f);
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
    public void sparseUnitAxesAndEmptyNnz() {
        SparseVector empty = SparseVector.of(4, new int[] {}, new float[] {});
        SparseVector emptyGot = SparseVector.decode(empty.encode());
        Assertions.assertEquals(4, emptyGot.dimensions);
        Assertions.assertEquals(0, emptyGot.indices.length);

        SparseVector axis = SparseVector.of(3, new int[] {2}, new float[] {-1.5f});
        byte[] blob = axis.encode();
        Assertions.assertEquals(12 + 4 + 4, blob.length);
        SparseVector got = SparseVector.decode(blob);
        Assertions.assertEquals(3, got.dimensions);
        Assertions.assertArrayEquals(new int[] {2}, got.indices);
        Assertions.assertArrayEquals(new float[] {-1.5f}, got.values);
    }

    @Test
    public void sparseMultiNnzMatchesByteLayoutSize() {
        SparseVector s = SparseVector.of(16, new int[] {0, 3, 7, 15}, new float[] {1.0f, -2.0f, 3.5f, 0.25f});
        byte[] blob = s.encode();
        Assertions.assertEquals(12 + 4 * 4 + 4 * 4, blob.length);
        SparseVector got = SparseVector.decode(blob);
        Assertions.assertEquals(s.dimensions, got.dimensions);
        Assertions.assertArrayEquals(s.indices, got.indices);
        Assertions.assertArrayEquals(s.values, got.values);
    }

    @Test
    public void sparseRejectsBadShapes() {
        Assertions.assertThrows(IllegalArgumentException.class,
                () -> SparseVector.of(0, new int[] {}, new float[] {}));
        Assertions.assertThrows(IllegalArgumentException.class,
                () -> SparseVector.of(2, new int[] {0, 1}, new float[] {1.0f}));
        Assertions.assertThrows(IllegalArgumentException.class,
                () -> SparseVector.of(2, new int[] {1, 0}, new float[] {1.0f, 2.0f}));
        Assertions.assertThrows(IllegalArgumentException.class,
                () -> SparseVector.of(2, new int[] {0}, new float[] {0.0f}));
        Assertions.assertThrows(IllegalArgumentException.class,
                () -> SparseVector.decode(new byte[8]));
    }

    @Test
    public void bitRoundTrip() {
        boolean[] bits = {true, false, true, true, false};
        Assertions.assertArrayEquals(bits, BitVector.decode(BitVector.encode(bits)));
    }

    @Test
    public void bitLengthsCrossingByteBoundary() {
        boolean[] eight = {true, false, true, false, true, false, true, false};
        Assertions.assertArrayEquals(eight, BitVector.decode(BitVector.encode(eight)));
        Assertions.assertEquals(8, BitVector.bitLength(BitVector.encode(eight)));

        boolean[] nine = {true, false, true, false, true, false, true, false, true};
        byte[] blob = BitVector.encode(nine);
        Assertions.assertEquals(Integer.BYTES + 2, blob.length);
        Assertions.assertArrayEquals(nine, BitVector.decode(blob));
    }

    @Test
    public void bitFromBitSet() {
        BitSet bs = new BitSet();
        bs.set(0);
        bs.set(2);
        boolean[] got = BitVector.decode(BitVector.encode(bs, 3));
        Assertions.assertArrayEquals(new boolean[] {true, false, true}, got);
    }

    @Test
    public void bitRejectsEmpty() {
        Assertions.assertThrows(IllegalArgumentException.class, () -> BitVector.encode());
        Assertions.assertThrows(IllegalArgumentException.class, () -> BitVector.decode(new byte[2]));
    }
}
