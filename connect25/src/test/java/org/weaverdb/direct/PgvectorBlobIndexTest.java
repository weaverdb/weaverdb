/*-------------------------------------------------------------------------
 *
 * Blob→vector conversion and functional indexes via real byte[] binds (FFM).
 *
 * Covers dense / halfvec / sparsevec / bit codecs plus HNSW/IVFFlat functional
 * indexes over bytea and Weaver blob (OID 1803) columns.
 *
 * ANN Index Scan over functional *_to_*(emb) is covered with both SQL
 * literals and bound converters($q). Typed-column binds are also covered.
 *
 *-------------------------------------------------------------------------
 */

package org.weaverdb.direct;

import java.util.ArrayList;
import java.util.List;
import org.junit.jupiter.api.Assertions;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.MethodOrderer;
import org.junit.jupiter.api.Order;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.TestMethodOrder;
import org.weaverdb.DBReference;
import org.weaverdb.Input;
import org.weaverdb.Output;
import org.weaverdb.Statement;
import org.weaverdb.vector.pg.BitVector;
import org.weaverdb.vector.pg.DenseVector;
import org.weaverdb.vector.pg.HalfVector;
import org.weaverdb.vector.pg.SparseVector;

@TestMethodOrder(MethodOrderer.OrderAnnotation.class)
public class PgvectorBlobIndexTest {

    private static final String VEC_TABLE = "pv_blob_vec_w25";
    private static final String BYTEA_TABLE = "pv_blob_ba_w25";
    private static final String BLOB_TABLE = "pv_blob_typed_w25";
    private static final String HALF_BYTEA_TABLE = "pv_blob_half_ba_w25";
    private static final String HALF_BLOB_TABLE = "pv_blob_half_typed_w25";
    private static final String DIM8_BYTEA_TABLE = "pv_blob_dim8_ba_w25";
    private static final String SPARSE_TABLE = "pv_blob_sv_w25";
    private static final String SPARSE_BYTEA_TABLE = "pv_blob_sv_ba_w25";
    private static final String SPARSE_BLOB_TABLE = "pv_blob_sv_typed_w25";
    private static final String BIT_TABLE = "pv_blob_bit_w25";
    private static final String BIT_BYTEA_TABLE = "pv_blob_bit_ba_w25";
    private static final String BIT_BLOB_TABLE = "pv_blob_bit_typed_w25";

    @BeforeAll
    public static void setup() throws Throwable {
        PgvectorWeaverTestSupport.ensureInitialized();
        try (DBReference conn = DBReference.connect("template1")) {
            exec(conn, "create table " + VEC_TABLE + " (id int, emb vector)");
            exec(conn, "create table " + BYTEA_TABLE + " (id int, emb bytea)");
            exec(conn, "create table " + BLOB_TABLE + " (id int, emb blob)");
            exec(conn, "create table " + HALF_BYTEA_TABLE + " (id int, emb bytea)");
            exec(conn, "create table " + HALF_BLOB_TABLE + " (id int, emb blob)");
            exec(conn, "create table " + DIM8_BYTEA_TABLE + " (id int, emb bytea)");
            exec(conn, "create table " + SPARSE_TABLE + " (id int, emb sparsevec)");
            exec(conn, "create table " + SPARSE_BYTEA_TABLE + " (id int, emb bytea)");
            exec(conn, "create table " + SPARSE_BLOB_TABLE + " (id int, emb blob)");
            exec(conn, "create table " + BIT_TABLE + " (id int, emb varbit)");
            exec(conn, "create table " + BIT_BYTEA_TABLE + " (id int, emb bytea)");
            exec(conn, "create table " + BIT_BLOB_TABLE + " (id int, emb blob)");
            insertBytea(conn, VEC_TABLE, 1, DenseVector.encode(1.0f, 0.0f, 0.0f), "vector");
            insertBytea(conn, VEC_TABLE, 2, DenseVector.encode(0.0f, 1.0f, 0.0f), "vector");
            insertBytea(conn, VEC_TABLE, 3, DenseVector.encode(0.0f, 0.0f, 1.0f), "vector");
            insertBytea(conn, VEC_TABLE, 4, DenseVector.encode(9.0f, 0.0f, 0.0f), "vector");
            insertBytea(conn, BYTEA_TABLE, 1, DenseVector.encode(1.0f, 0.0f, 0.0f), "bytea");
            insertBytea(conn, BYTEA_TABLE, 2, DenseVector.encode(0.0f, 1.0f, 0.0f), "bytea");
            insertBytea(conn, BYTEA_TABLE, 3, DenseVector.encode(0.0f, 0.0f, 1.0f), "bytea");
            insertBytea(conn, BYTEA_TABLE, 4, DenseVector.encode(9.0f, 0.0f, 0.0f), "bytea");
            insertBytea(conn, BLOB_TABLE, 1, DenseVector.encode(1.0f, 0.0f, 0.0f), "blob");
            insertBytea(conn, BLOB_TABLE, 2, DenseVector.encode(0.0f, 1.0f, 0.0f), "blob");
            insertBytea(conn, BLOB_TABLE, 3, DenseVector.encode(0.0f, 0.0f, 1.0f), "blob");
            insertBytea(conn, BLOB_TABLE, 4, DenseVector.encode(9.0f, 0.0f, 0.0f), "blob");
        }
    }

    @Test
    @Order(1)
    public void roundTripByteaToVector() throws Exception {
        byte[] blob = DenseVector.encode(1.0f, 0.0f, 0.0f);
        try (DBReference conn = DBReference.connect("template1");
                Statement s = conn.statement(
                        "select vector_to_bytea(bytea_to_vector($emb))")) {
            s.linkInput("emb", byte[].class).set(blob);
            Output<byte[]> out = s.linkOutput(1, byte[].class);
            s.execute();
            Assertions.assertTrue(s.fetch());
            Assertions.assertArrayEquals(blob, out.get());
        }
    }

    @Test
    @Order(2)
    public void createIndexesOnVectorFromBlobs() throws Exception {
        try (DBReference conn = DBReference.connect("template1")) {
            exec(conn,
                    "create index " + VEC_TABLE + "_ivf on " + VEC_TABLE
                            + " using ivfflat (emb vector_l2_ops) with (lists = 2)");
            exec(conn,
                    "create index " + VEC_TABLE + "_hnsw on " + VEC_TABLE
                            + " using hnsw (emb vector_l2_ops) with (m = 8, ef_construction = 32)");
        }
    }

    @Test
    @Order(3)
    public void orderByOnVectorColumnFromBlobs() throws Exception {
        List<Integer> got = orderBy(
                "select id from " + VEC_TABLE
                        + " order by emb <-> bytea_to_vector($q) limit 2",
                DenseVector.encode(1.0f, 0.0f, 0.0f));
        Assertions.assertEquals(1, got.get(0).intValue());
        Assertions.assertEquals(2, got.size());
        Assertions.assertTrue(got.get(1) == 2 || got.get(1) == 3);
    }

    @Test
    @Order(4)
    public void createFunctionalIndexesOnBytea() throws Exception {
        try (DBReference conn = DBReference.connect("template1")) {
            exec(conn,
                    "create index " + BYTEA_TABLE + "_ivf on " + BYTEA_TABLE
                            + " using ivfflat (bytea_to_vector(emb) vector_l2_ops) with (lists = 2)");
            exec(conn,
                    "create index " + BYTEA_TABLE + "_hnsw on " + BYTEA_TABLE
                            + " using hnsw (bytea_to_vector(emb) vector_l2_ops) with (m = 8, ef_construction = 32)");
        }
    }

    @Test
    @Order(5)
    public void orderByOnByteaFunctionalIndex() throws Exception {
        List<Integer> got = queryIds(
                "select id from " + BYTEA_TABLE
                        + " order by bytea_to_vector(emb) <-> '[0,0,1]'::vector limit 2");
        Assertions.assertEquals(3, got.get(0).intValue());
        Assertions.assertEquals(2, got.size());
        Assertions.assertTrue(got.get(1) == 1 || got.get(1) == 2);

        List<Integer> bound = orderBy(
                "select id from " + BYTEA_TABLE
                        + " order by bytea_to_vector(emb) <-> bytea_to_vector($q) limit 2",
                DenseVector.encode(0.0f, 0.0f, 1.0f));
        Assertions.assertEquals(3, bound.get(0).intValue());
        Assertions.assertEquals(2, bound.size());
        Assertions.assertTrue(bound.get(1) == 1 || bound.get(1) == 2);
    }

    @Test
    @Order(6)
    public void insertAfterFunctionalIndex() throws Exception {
        try (DBReference conn = DBReference.connect("template1")) {
            insertBytea(conn, BYTEA_TABLE, 5, DenseVector.encode(1.0f, 1.0f, 0.0f), "bytea");
        }
        List<Integer> got = queryIds(
                "select id from " + BYTEA_TABLE
                        + " order by bytea_to_vector(emb) <-> '[1,1,0]'::vector limit 1");
        Assertions.assertEquals(List.of(5), got);
    }

    @Test
    @Order(7)
    public void halfvecByteaFunctionalOrderBy() throws Exception {
        try (DBReference conn = DBReference.connect("template1")) {
            insertBytea(conn, HALF_BYTEA_TABLE, 1, HalfVector.encode(1.0f, 0.0f, 0.0f), "bytea");
            insertBytea(conn, HALF_BYTEA_TABLE, 2, HalfVector.encode(0.0f, 1.0f, 0.0f), "bytea");
            insertBytea(conn, HALF_BYTEA_TABLE, 3, HalfVector.encode(0.0f, 0.0f, 1.0f), "bytea");
            exec(conn,
                    "create index " + HALF_BYTEA_TABLE + "_hnsw on " + HALF_BYTEA_TABLE
                            + " using hnsw (bytea_to_halfvec(emb) halfvec_l2_ops)"
                            + " with (m = 8, ef_construction = 32)");
        }
        List<Integer> got = queryIds(
                "select id from " + HALF_BYTEA_TABLE
                        + " order by bytea_to_halfvec(emb) <-> '[1,0,0]'::halfvec limit 2");
        Assertions.assertEquals(1, got.get(0).intValue());
        Assertions.assertTrue(got.get(1) == 2 || got.get(1) == 3);
    }

    @Test
    @Order(8)
    public void denseVaryingByteShapesRoundTripAndIndex() throws Exception {
        byte[] one = DenseVector.encode(-3.5f);
        byte[] eightA = DenseVector.encode(1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
        byte[] eightB = DenseVector.encode(0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
        byte[] eightC = DenseVector.encode(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f);
        byte[] eightFar = DenseVector.encode(9.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);

        try (DBReference conn = DBReference.connect("template1");
                Statement s = conn.statement(
                        "select vector_to_bytea(bytea_to_vector($emb))")) {
            s.linkInput("emb", byte[].class).set(one);
            Output<byte[]> out = s.linkOutput(1, byte[].class);
            s.execute();
            Assertions.assertTrue(s.fetch());
            Assertions.assertArrayEquals(one, out.get());
        }

        try (DBReference conn = DBReference.connect("template1")) {
            insertBytea(conn, DIM8_BYTEA_TABLE, 1, eightA, "bytea");
            insertBytea(conn, DIM8_BYTEA_TABLE, 2, eightB, "bytea");
            insertBytea(conn, DIM8_BYTEA_TABLE, 3, eightC, "bytea");
            insertBytea(conn, DIM8_BYTEA_TABLE, 4, eightFar, "bytea");
            exec(conn,
                    "create index " + DIM8_BYTEA_TABLE + "_hnsw on " + DIM8_BYTEA_TABLE
                            + " using hnsw (bytea_to_vector(emb) vector_l2_ops)"
                            + " with (m = 8, ef_construction = 32)");
            exec(conn,
                    "create index " + DIM8_BYTEA_TABLE + "_ivf on " + DIM8_BYTEA_TABLE
                            + " using ivfflat (bytea_to_vector(emb) vector_l2_ops) with (lists = 2)");
        }
        List<Integer> got = queryIds(
                "select id from " + DIM8_BYTEA_TABLE
                        + " order by bytea_to_vector(emb) <-> '[1,0,0,0,0,0,0,0]'::vector limit 2");
        Assertions.assertEquals(1, got.get(0).intValue());
        Assertions.assertEquals(2, got.size());
    }

    @Test
    @Order(9)
    public void roundTripByteaToSparsevec() throws Exception {
        byte[] blob = SparseVector.of(5, new int[] {0, 2}, new float[] {1.0f, 2.0f}).encode();
        try (DBReference conn = DBReference.connect("template1");
                Statement s = conn.statement(
                        "select sparsevec_to_bytea(bytea_to_sparsevec($emb))")) {
            s.linkInput("emb", byte[].class).set(blob);
            Output<byte[]> out = s.linkOutput(1, byte[].class);
            s.execute();
            Assertions.assertTrue(s.fetch());
            Assertions.assertArrayEquals(blob, out.get());
        }
    }

    @Test
    @Order(10)
    public void insertBlobIntoSparsevecColumn() throws Exception {
        try (DBReference conn = DBReference.connect("template1")) {
            insertBytea(conn, SPARSE_TABLE, 1,
                    SparseVector.of(3, new int[] {0}, new float[] {1.0f}).encode(), "sparsevec");
            insertBytea(conn, SPARSE_TABLE, 2,
                    SparseVector.of(3, new int[] {1}, new float[] {1.0f}).encode(), "sparsevec");
            insertBytea(conn, SPARSE_TABLE, 3,
                    SparseVector.of(3, new int[] {2}, new float[] {1.0f}).encode(), "sparsevec");
            exec(conn,
                    "create index " + SPARSE_TABLE + "_hnsw on " + SPARSE_TABLE
                            + " using hnsw (emb sparsevec_l2_ops) with (m = 8, ef_construction = 32)");
        }
        List<Integer> got = orderBy(
                "select id from " + SPARSE_TABLE
                        + " order by emb <-> bytea_to_sparsevec($q) limit 2",
                SparseVector.of(3, new int[] {0}, new float[] {1.0f}).encode());
        Assertions.assertEquals(1, got.get(0).intValue());
        Assertions.assertTrue(got.get(1) == 2 || got.get(1) == 3);
    }

    @Test
    @Order(11)
    public void sparsevecByteaFunctionalOrderBy() throws Exception {
        try (DBReference conn = DBReference.connect("template1")) {
            insertBytea(conn, SPARSE_BYTEA_TABLE, 1,
                    SparseVector.of(3, new int[] {0}, new float[] {1.0f}).encode(), "bytea");
            insertBytea(conn, SPARSE_BYTEA_TABLE, 2,
                    SparseVector.of(3, new int[] {1}, new float[] {1.0f}).encode(), "bytea");
            insertBytea(conn, SPARSE_BYTEA_TABLE, 3,
                    SparseVector.of(3, new int[] {2}, new float[] {1.0f}).encode(), "bytea");
            exec(conn,
                    "create index " + SPARSE_BYTEA_TABLE + "_hnsw on " + SPARSE_BYTEA_TABLE
                            + " using hnsw (bytea_to_sparsevec(emb) sparsevec_l2_ops)"
                            + " with (m = 8, ef_construction = 32)");
        }
        List<Integer> got = queryIds(
                "select id from " + SPARSE_BYTEA_TABLE
                        + " order by bytea_to_sparsevec(emb) <-> '{1:1}/3'::sparsevec limit 2");
        Assertions.assertEquals(1, got.get(0).intValue());
        Assertions.assertTrue(got.get(1) == 2 || got.get(1) == 3);
    }

    @Test
    @Order(12)
    public void insertSparseBlobAfterFunctionalIndex() throws Exception {
        try (DBReference conn = DBReference.connect("template1")) {
            insertBytea(conn, SPARSE_BYTEA_TABLE, 4,
                    SparseVector.of(3, new int[] {0, 1}, new float[] {1.0f, 1.0f}).encode(), "bytea");
        }
        List<Integer> got = queryIds(
                "select id from " + SPARSE_BYTEA_TABLE
                        + " order by bytea_to_sparsevec(emb) <-> '{1:1,2:1}/3'::sparsevec limit 1");
        Assertions.assertEquals(List.of(4), got);
    }

    @Test
    @Order(13)
    public void roundTripByteaToBit() throws Exception {
        byte[] blob = BitVector.encode(true, false, true);
        try (DBReference conn = DBReference.connect("template1");
                Statement s = conn.statement(
                        "select bit_to_bytea(bytea_to_bit($emb))")) {
            s.linkInput("emb", byte[].class).set(blob);
            Output<byte[]> out = s.linkOutput(1, byte[].class);
            s.execute();
            Assertions.assertTrue(s.fetch());
            Assertions.assertArrayEquals(blob, out.get());
        }
    }

    @Test
    @Order(14)
    public void insertBlobIntoBitColumn() throws Exception {
        try (DBReference conn = DBReference.connect("template1")) {
            insertBytea(conn, BIT_TABLE, 1, BitVector.encode(true, false, false), "bit");
            insertBytea(conn, BIT_TABLE, 2, BitVector.encode(false, true, false), "bit");
            insertBytea(conn, BIT_TABLE, 3, BitVector.encode(false, false, true), "bit");
            exec(conn,
                    "create index " + BIT_TABLE + "_ivf on " + BIT_TABLE
                            + " using ivfflat (emb bit_hamming_ops) with (lists = 2)");
        }
        List<Integer> got = orderBy(
                "select id from " + BIT_TABLE
                        + " order by emb <~> bytea_to_bit($q) limit 2",
                BitVector.encode(true, false, false));
        Assertions.assertEquals(1, got.get(0).intValue());
        Assertions.assertTrue(got.get(1) == 2 || got.get(1) == 3);
    }

    @Test
    @Order(15)
    public void bitByteaFunctionalHammingIndex() throws Exception {
        try (DBReference conn = DBReference.connect("template1")) {
            insertBytea(conn, BIT_BYTEA_TABLE, 1, BitVector.encode(true, false, false), "bytea");
            insertBytea(conn, BIT_BYTEA_TABLE, 2, BitVector.encode(false, true, false), "bytea");
            insertBytea(conn, BIT_BYTEA_TABLE, 3, BitVector.encode(false, false, true), "bytea");
            exec(conn,
                    "create index " + BIT_BYTEA_TABLE + "_ivf on " + BIT_BYTEA_TABLE
                            + " using ivfflat (bytea_to_bit(emb) bit_hamming_ops) with (lists = 2)");
        }
        List<Integer> hamming = queryIds(
                "select id from " + BIT_BYTEA_TABLE
                        + " order by bytea_to_bit(emb) <~> 'B100'::varbit limit 2");
        Assertions.assertEquals(1, hamming.get(0).intValue());
        Assertions.assertTrue(hamming.get(1) == 2 || hamming.get(1) == 3);
    }

    @Test
    @Order(16)
    public void bitByteaFunctionalJaccardIndex() throws Exception {
        try (DBReference conn = DBReference.connect("template1")) {
            exec(conn,
                    "create index " + BIT_BYTEA_TABLE + "_jaccard on " + BIT_BYTEA_TABLE
                            + " using hnsw (bytea_to_bit(emb) bit_jaccard_ops)"
                            + " with (m = 8, ef_construction = 32)");
        }
        List<Integer> jaccard = queryIds(
                "select id from " + BIT_BYTEA_TABLE
                        + " order by bytea_to_bit(emb) <%> 'B100'::varbit limit 2");
        Assertions.assertEquals(1, jaccard.get(0).intValue());
        Assertions.assertEquals(2, jaccard.size());
    }

    @Test
    @Order(17)
    public void insertBitBlobAfterFunctionalIndex() throws Exception {
        try (DBReference conn = DBReference.connect("template1")) {
            insertBytea(conn, BIT_BYTEA_TABLE, 4, BitVector.encode(true, true, false), "bytea");
        }
        List<Integer> got = queryIds(
                "select id from " + BIT_BYTEA_TABLE
                        + " order by bytea_to_bit(emb) <~> 'B110'::varbit limit 1");
        Assertions.assertEquals(List.of(4), got);
    }

    @Test
    @Order(18)
    public void roundTripBlobToVector() throws Exception {
        byte[] packed = DenseVector.encode(1.0f, 0.0f, 0.0f);
        try (DBReference conn = DBReference.connect("template1");
                Statement s = conn.statement(
                        "select vector_to_blob(blob_to_vector($emb))")) {
            s.linkInput("emb", byte[].class).set(packed);
            Output<byte[]> out = s.linkOutput(1, byte[].class);
            s.execute();
            Assertions.assertTrue(s.fetch());
            Assertions.assertArrayEquals(packed, out.get());
        }
    }

    @Test
    @Order(19)
    public void typedBlobColumnFunctionalIndex() throws Exception {
        try (DBReference conn = DBReference.connect("template1")) {
            exec(conn,
                    "create index " + BLOB_TABLE + "_ivf on " + BLOB_TABLE
                            + " using ivfflat (blob_to_vector(emb) vector_l2_ops) with (lists = 2)");
            exec(conn,
                    "create index " + BLOB_TABLE + "_hnsw on " + BLOB_TABLE
                            + " using hnsw (blob_to_vector(emb) vector_l2_ops)"
                            + " with (m = 8, ef_construction = 32)");
        }
        List<Integer> got = queryIds(
                "select id from " + BLOB_TABLE
                        + " order by blob_to_vector(emb) <-> '[0,0,1]'::vector limit 2");
        Assertions.assertEquals(3, got.get(0).intValue());
        Assertions.assertEquals(2, got.size());

        List<Integer> bound = orderBy(
                "select id from " + BLOB_TABLE
                        + " order by blob_to_vector(emb) <-> blob_to_vector($q) limit 2",
                DenseVector.encode(0.0f, 0.0f, 1.0f));
        Assertions.assertEquals(3, bound.get(0).intValue());
        Assertions.assertEquals(2, bound.size());

        try (DBReference conn = DBReference.connect("template1")) {
            insertBytea(conn, BLOB_TABLE, 5, DenseVector.encode(1.0f, 1.0f, 0.0f), "blob");
        }
        Assertions.assertEquals(List.of(5), queryIds(
                "select id from " + BLOB_TABLE
                        + " order by blob_to_vector(emb) <-> '[1,1,0]'::vector limit 1"));
    }

    @Test
    @Order(20)
    public void typedBlobHalfSparseBitFunctionalIndexes() throws Exception {
        try (DBReference conn = DBReference.connect("template1")) {
            insertBytea(conn, HALF_BLOB_TABLE, 1, HalfVector.encode(1.0f, 0.0f, 0.0f), "halfblob");
            insertBytea(conn, HALF_BLOB_TABLE, 2, HalfVector.encode(0.0f, 1.0f, 0.0f), "halfblob");
            insertBytea(conn, HALF_BLOB_TABLE, 3, HalfVector.encode(0.0f, 0.0f, 1.0f), "halfblob");
            exec(conn,
                    "create index " + HALF_BLOB_TABLE + "_hnsw on " + HALF_BLOB_TABLE
                            + " using hnsw (blob_to_halfvec(emb) halfvec_l2_ops)"
                            + " with (m = 8, ef_construction = 32)");

            insertBytea(conn, SPARSE_BLOB_TABLE, 1,
                    SparseVector.of(3, new int[] {0}, new float[] {1.0f}).encode(), "sparseblob");
            insertBytea(conn, SPARSE_BLOB_TABLE, 2,
                    SparseVector.of(3, new int[] {1}, new float[] {1.0f}).encode(), "sparseblob");
            insertBytea(conn, SPARSE_BLOB_TABLE, 3,
                    SparseVector.of(3, new int[] {2}, new float[] {1.0f}).encode(), "sparseblob");
            exec(conn,
                    "create index " + SPARSE_BLOB_TABLE + "_hnsw on " + SPARSE_BLOB_TABLE
                            + " using hnsw (blob_to_sparsevec(emb) sparsevec_l2_ops)"
                            + " with (m = 8, ef_construction = 32)");

            insertBytea(conn, BIT_BLOB_TABLE, 1, BitVector.encode(true, false, false), "bitblob");
            insertBytea(conn, BIT_BLOB_TABLE, 2, BitVector.encode(false, true, false), "bitblob");
            insertBytea(conn, BIT_BLOB_TABLE, 3, BitVector.encode(false, false, true), "bitblob");
            exec(conn,
                    "create index " + BIT_BLOB_TABLE + "_ivf on " + BIT_BLOB_TABLE
                            + " using ivfflat (blob_to_bit(emb) bit_hamming_ops) with (lists = 2)");
        }
        Assertions.assertEquals(List.of(1), queryIds(
                "select id from " + HALF_BLOB_TABLE
                        + " order by blob_to_halfvec(emb) <-> '[1,0,0]'::halfvec limit 1"));
        Assertions.assertEquals(List.of(1), queryIds(
                "select id from " + SPARSE_BLOB_TABLE
                        + " order by blob_to_sparsevec(emb) <-> '{1:1}/3'::sparsevec limit 1"));
        Assertions.assertEquals(List.of(1), queryIds(
                "select id from " + BIT_BLOB_TABLE
                        + " order by blob_to_bit(emb) <~> 'B100'::varbit limit 1"));
    }

    private static void insertBytea(DBReference conn, String table, int id, byte[] emb,
            String mode) throws Exception {
        String sql;
        switch (mode) {
            case "vector":
                sql = "insert into " + table + " values ($id, bytea_to_vector($emb))";
                break;
            case "sparsevec":
                sql = "insert into " + table + " values ($id, bytea_to_sparsevec($emb))";
                break;
            case "bit":
                sql = "insert into " + table + " values ($id, bytea_to_bit($emb))";
                break;
            case "blob":
                // FFM byte[] binds as bytea; convert into SQL blob (OID 1803).
                sql = "insert into " + table
                        + " values ($id, vector_to_blob(bytea_to_vector($emb)))";
                break;
            case "halfblob":
                sql = "insert into " + table
                        + " values ($id, halfvec_to_blob(bytea_to_halfvec($emb)))";
                break;
            case "sparseblob":
                sql = "insert into " + table
                        + " values ($id, sparsevec_to_blob(bytea_to_sparsevec($emb)))";
                break;
            case "bitblob":
                sql = "insert into " + table
                        + " values ($id, bit_to_blob(bytea_to_bit($emb)))";
                break;
            default:
                sql = "insert into " + table + " values ($id, $emb)";
                break;
        }
        try (Statement s = conn.statement(sql)) {
            Input<Integer> idIn = s.linkInput("id", Integer.class);
            idIn.set(id);
            if (emb.length <= 4096) {
                Input<byte[]> embIn = s.linkInput("emb", byte[].class);
                embIn.set(emb);
            } else {
                Input<byte[]> embIn = s.linkInputStream("emb", (byte[] value, java.io.OutputStream out) -> {
                    out.write(value);
                });
                embIn.set(emb);
            }
            s.execute();
        }
    }

    private static List<Integer> orderBy(String sql, byte[] query) throws Exception {
        List<Integer> rows = new ArrayList<>();
        try (DBReference conn = DBReference.connect("template1");
                Statement s = conn.statement(sql)) {
            if (query.length <= 4096) {
                s.linkInput("q", byte[].class).set(query);
            } else {
                Input<byte[]> ch = s.linkInputStream("q", (byte[] value, java.io.OutputStream out) -> {
                    out.write(value);
                });
                ch.set(query);
            }
            Output<Integer> out = s.linkOutput(1, Integer.class);
            s.execute();
            while (s.fetch()) {
                Integer v = out.get();
                if (v != null) {
                    rows.add(v);
                }
            }
        }
        return rows;
    }

    private static List<Integer> queryIds(String sql) throws Exception {
        List<Integer> rows = new ArrayList<>();
        try (DBReference conn = DBReference.connect("template1");
                Statement s = conn.statement(sql)) {
            Output<Integer> out = s.linkOutput(1, Integer.class);
            s.execute();
            while (s.fetch()) {
                Integer v = out.get();
                if (v != null) {
                    rows.add(v);
                }
            }
        }
        return rows;
    }

    private static void exec(DBReference conn, String sql) throws Exception {
        try (Statement s = conn.statement(sql)) {
            s.execute();
        }
    }
}
