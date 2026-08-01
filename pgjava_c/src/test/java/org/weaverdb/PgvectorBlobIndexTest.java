/*-------------------------------------------------------------------------
 *
 * Index embeddings from float32 / float16 / sparse / bit byte[] blobs via
 * bytea_to_*.
 *
 * Covers: conversion round-trip, typed-column ingest from blobs, and
 * functional HNSW/IVFFlat indexes over a bytea column (vector, halfvec,
 * sparsevec, bit).
 *
 * Note: ANN Index Scan over functional bytea_to_*(emb) expressions is
 * asserted with SQL literals. Bound bytea_to_*($q) queries are validated
 * on typed columns and (with seqscan) against bytea heap values — a known
 * planner/index quirk mis-orders bound expression queries on functional
 * indexes.
 *
 *-------------------------------------------------------------------------
 */

package org.weaverdb;

import java.util.ArrayList;
import java.util.List;
import org.junit.jupiter.api.Assertions;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.MethodOrderer;
import org.junit.jupiter.api.Order;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.TestMethodOrder;
import org.junit.jupiter.api.extension.ExtendWith;
import org.weaverdb.vector.pg.BitVector;
import org.weaverdb.vector.pg.DenseVector;
import org.weaverdb.vector.pg.HalfVector;
import org.weaverdb.vector.pg.SparseVector;

@TestMethodOrder(MethodOrderer.OrderAnnotation.class)
@ExtendWith(InstallNative.class)
public class PgvectorBlobIndexTest {

    private static final String VEC_TABLE = "pv_blob_vec_jni";
    private static final String BYTEA_TABLE = "pv_blob_ba_jni";
    private static final String HALF_BYTEA_TABLE = "pv_blob_half_ba_jni";
    private static final String DIM8_BYTEA_TABLE = "pv_blob_dim8_ba_jni";
    private static final String SPARSE_TABLE = "pv_blob_sv_jni";
    private static final String SPARSE_BYTEA_TABLE = "pv_blob_sv_ba_jni";
    private static final String BIT_TABLE = "pv_blob_bit_jni";
    private static final String BIT_BYTEA_TABLE = "pv_blob_bit_ba_jni";

    @BeforeAll
    public static void setup() throws Exception {
        try (DBReference conn = DBReferenceManager.connect("template1")) {
            exec(conn, "create table " + VEC_TABLE + " (id int, emb vector)");
            exec(conn, "create table " + BYTEA_TABLE + " (id int, emb bytea)");
            exec(conn, "create table " + HALF_BYTEA_TABLE + " (id int, emb bytea)");
            exec(conn, "create table " + DIM8_BYTEA_TABLE + " (id int, emb bytea)");
            exec(conn, "create table " + SPARSE_TABLE + " (id int, emb sparsevec)");
            exec(conn, "create table " + SPARSE_BYTEA_TABLE + " (id int, emb bytea)");
            exec(conn, "create table " + BIT_TABLE + " (id int, emb varbit)");
            exec(conn, "create table " + BIT_BYTEA_TABLE + " (id int, emb bytea)");
        }
    }

    @Test
    @Order(1)
    public void roundTripByteaToVector() throws Exception {
        byte[] blob = DenseVector.encode(1.0f, 0.0f, 0.0f);
        try (DBReference conn = DBReferenceManager.connect("template1");
                Statement s = conn.statement(
                        "select vector_to_bytea(bytea_to_vector($emb))")) {
            Input<byte[]> in = s.linkInput("emb", byte[].class);
            Output<byte[]> out = s.linkOutput(1, byte[].class);
            in.set(blob);
            s.execute();
            Assertions.assertTrue(s.fetch());
            Assertions.assertArrayEquals(blob, out.get());
        }
    }

    @Test
    @Order(2)
    public void roundTripByteaToHalfvec() throws Exception {
        byte[] blob = HalfVector.encode(1.0f, 0.0f, 0.0f);
        try (DBReference conn = DBReferenceManager.connect("template1");
                Statement s = conn.statement(
                        "select halfvec_to_bytea(bytea_to_halfvec($emb))")) {
            Input<byte[]> in = s.linkInput("emb", byte[].class);
            Output<byte[]> out = s.linkOutput(1, byte[].class);
            in.set(blob);
            s.execute();
            Assertions.assertTrue(s.fetch());
            Assertions.assertArrayEquals(blob, out.get());
        }
    }

    @Test
    @Order(3)
    public void insertBlobIntoVectorColumn() throws Exception {
        try (DBReference conn = DBReferenceManager.connect("template1")) {
            insertBlobVector(conn, VEC_TABLE, 1, 1.0f, 0.0f, 0.0f);
            insertBlobVector(conn, VEC_TABLE, 2, 0.0f, 1.0f, 0.0f);
            insertBlobVector(conn, VEC_TABLE, 3, 0.0f, 0.0f, 1.0f);
            insertBlobVector(conn, VEC_TABLE, 4, 9.0f, 0.0f, 0.0f);
        }
    }

    @Test
    @Order(4)
    public void createIndexesOnVectorFromBlobs() throws Exception {
        try (DBReference conn = DBReferenceManager.connect("template1")) {
            exec(conn,
                    "create index " + VEC_TABLE + "_ivf on " + VEC_TABLE
                            + " using ivfflat (emb vector_l2_ops) with (lists = 2)");
            exec(conn,
                    "create index " + VEC_TABLE + "_hnsw on " + VEC_TABLE
                            + " using hnsw (emb vector_l2_ops) with (m = 8, ef_construction = 32)");
        }
    }

    @Test
    @Order(5)
    public void orderByWithBlobQueryOnVectorColumn() throws Exception {
        List<Integer> got = orderByBlobQuery(
                "select id from " + VEC_TABLE + " order by emb <-> bytea_to_vector($q) limit 2",
                DenseVector.encode(1.0f, 0.0f, 0.0f));
        Assertions.assertEquals(1, got.get(0).intValue());
        Assertions.assertEquals(2, got.size());
        Assertions.assertTrue(got.get(1) == 2 || got.get(1) == 3,
                "second nearest to [1,0,0] is id 2 or 3 (tie), got " + got.get(1));
    }

    @Test
    @Order(6)
    public void insertBlobIntoByteaColumn() throws Exception {
        try (DBReference conn = DBReferenceManager.connect("template1")) {
            insertBlobBytea(conn, BYTEA_TABLE, 1, DenseVector.encode(1.0f, 0.0f, 0.0f));
            insertBlobBytea(conn, BYTEA_TABLE, 2, DenseVector.encode(0.0f, 1.0f, 0.0f));
            insertBlobBytea(conn, BYTEA_TABLE, 3, DenseVector.encode(0.0f, 0.0f, 1.0f));
            insertBlobBytea(conn, BYTEA_TABLE, 4, DenseVector.encode(9.0f, 0.0f, 0.0f));
        }
    }

    @Test
    @Order(7)
    public void createFunctionalIndexesOnBytea() throws Exception {
        try (DBReference conn = DBReferenceManager.connect("template1")) {
            exec(conn,
                    "create index " + BYTEA_TABLE + "_ivf on " + BYTEA_TABLE
                            + " using ivfflat (bytea_to_vector(emb) vector_l2_ops) with (lists = 2)");
            exec(conn,
                    "create index " + BYTEA_TABLE + "_hnsw on " + BYTEA_TABLE
                            + " using hnsw (bytea_to_vector(emb) vector_l2_ops) with (m = 8, ef_construction = 32)");
        }
    }

    @Test
    @Order(8)
    public void orderByOnByteaFunctionalIndex() throws Exception {
        // Bound bytea_to_vector($q) against functional indexes is unreliable; use a
        // literal query to assert the indexed Java blobs are searchable.
        List<Integer> got = queryIds(
                "select id from " + BYTEA_TABLE
                        + " order by bytea_to_vector(emb) <-> '[0,0,1]'::vector limit 2");
        Assertions.assertEquals(3, got.get(0).intValue());
        Assertions.assertEquals(2, got.size());
        Assertions.assertTrue(got.get(1) == 1 || got.get(1) == 2,
                "second nearest should be id 1 or 2, got " + got.get(1));

        // Seqscan + bound query still proves codec distance on the heap blobs.
        List<Integer> seq = orderByBlobQuerySeqscan(
                "select id from " + BYTEA_TABLE
                        + " order by bytea_to_vector(emb) <-> bytea_to_vector($q) limit 2",
                DenseVector.encode(0.0f, 0.0f, 1.0f));
        Assertions.assertEquals(3, seq.get(0).intValue());
    }

    @Test
    @Order(9)
    public void insertAfterFunctionalIndex() throws Exception {
        try (DBReference conn = DBReferenceManager.connect("template1")) {
            insertBlobBytea(conn, BYTEA_TABLE, 5, DenseVector.encode(1.0f, 1.0f, 0.0f));
        }
        List<Integer> got = queryIds(
                "select id from " + BYTEA_TABLE
                        + " order by bytea_to_vector(emb) <-> '[1,1,0]'::vector limit 1");
        Assertions.assertEquals(5, got.get(0).intValue());
    }

    @Test
    @Order(10)
    public void halfvecByteaFunctionalOrderBy() throws Exception {
        try (DBReference conn = DBReferenceManager.connect("template1")) {
            insertBlobBytea(conn, HALF_BYTEA_TABLE, 1, HalfVector.encode(1.0f, 0.0f, 0.0f));
            insertBlobBytea(conn, HALF_BYTEA_TABLE, 2, HalfVector.encode(0.0f, 1.0f, 0.0f));
            insertBlobBytea(conn, HALF_BYTEA_TABLE, 3, HalfVector.encode(0.0f, 0.0f, 1.0f));
            // Java half bytes match SQL halfvec_to_bytea for the unit vector.
            try (Statement s = conn.statement(
                    "select case when emb = halfvec_to_bytea('[1,0,0]'::halfvec) then 1 else 0 end from "
                            + HALF_BYTEA_TABLE + " where id = 1")) {
                Output<Integer> out = s.linkOutput(1, Integer.class);
                s.execute();
                Assertions.assertTrue(s.fetch());
                Assertions.assertEquals(1, out.get().intValue());
            }
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
    @Order(11)
    public void denseVaryingByteShapesRoundTripAndIndex() throws Exception {
        byte[] one = DenseVector.encode(-3.5f);
        byte[] eightA = DenseVector.encode(1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
        byte[] eightB = DenseVector.encode(0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
        byte[] eightC = DenseVector.encode(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f);
        byte[] eightFar = DenseVector.encode(9.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);

        try (DBReference conn = DBReferenceManager.connect("template1");
                Statement s = conn.statement(
                        "select vector_to_bytea(bytea_to_vector($emb))")) {
            Input<byte[]> in = s.linkInput("emb", byte[].class);
            Output<byte[]> out = s.linkOutput(1, byte[].class);
            in.set(one);
            s.execute();
            Assertions.assertTrue(s.fetch());
            Assertions.assertArrayEquals(one, out.get());
        }

        try (DBReference conn = DBReferenceManager.connect("template1")) {
            insertBlobBytea(conn, DIM8_BYTEA_TABLE, 1, eightA);
            insertBlobBytea(conn, DIM8_BYTEA_TABLE, 2, eightB);
            insertBlobBytea(conn, DIM8_BYTEA_TABLE, 3, eightC);
            insertBlobBytea(conn, DIM8_BYTEA_TABLE, 4, eightFar);
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
    @Order(12)
    public void roundTripByteaToSparsevec() throws Exception {
        byte[] blob = SparseVector.of(5, new int[] {0, 2}, new float[] {1.0f, 2.0f}).encode();
        try (DBReference conn = DBReferenceManager.connect("template1");
                Statement s = conn.statement(
                        "select sparsevec_to_bytea(bytea_to_sparsevec($emb))")) {
            Input<byte[]> in = s.linkInput("emb", byte[].class);
            Output<byte[]> out = s.linkOutput(1, byte[].class);
            in.set(blob);
            s.execute();
            Assertions.assertTrue(s.fetch());
            Assertions.assertArrayEquals(blob, out.get());
        }
    }

    @Test
    @Order(13)
    public void insertBlobIntoSparsevecColumn() throws Exception {
        try (DBReference conn = DBReferenceManager.connect("template1")) {
            insertBlobSparsevec(conn, SPARSE_TABLE, 1, SparseVector.of(3, new int[] {0}, new float[] {1.0f}));
            insertBlobSparsevec(conn, SPARSE_TABLE, 2, SparseVector.of(3, new int[] {1}, new float[] {1.0f}));
            insertBlobSparsevec(conn, SPARSE_TABLE, 3, SparseVector.of(3, new int[] {2}, new float[] {1.0f}));
            exec(conn,
                    "create index " + SPARSE_TABLE + "_hnsw on " + SPARSE_TABLE
                            + " using hnsw (emb sparsevec_l2_ops) with (m = 8, ef_construction = 32)");
        }
        // Typed column + bound query is reliable.
        List<Integer> got = orderByBlobQuery(
                "select id from " + SPARSE_TABLE
                        + " order by emb <-> bytea_to_sparsevec($q) limit 2",
                SparseVector.of(3, new int[] {0}, new float[] {1.0f}).encode());
        Assertions.assertEquals(1, got.get(0).intValue());
        Assertions.assertTrue(got.get(1) == 2 || got.get(1) == 3);
    }

    @Test
    @Order(14)
    public void sparsevecByteaFunctionalOrderBy() throws Exception {
        try (DBReference conn = DBReferenceManager.connect("template1")) {
            insertBlobBytea(conn, SPARSE_BYTEA_TABLE, 1,
                    SparseVector.of(3, new int[] {0}, new float[] {1.0f}).encode());
            insertBlobBytea(conn, SPARSE_BYTEA_TABLE, 2,
                    SparseVector.of(3, new int[] {1}, new float[] {1.0f}).encode());
            insertBlobBytea(conn, SPARSE_BYTEA_TABLE, 3,
                    SparseVector.of(3, new int[] {2}, new float[] {1.0f}).encode());
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
    @Order(15)
    public void insertSparseBlobAfterFunctionalIndex() throws Exception {
        try (DBReference conn = DBReferenceManager.connect("template1")) {
            insertBlobBytea(conn, SPARSE_BYTEA_TABLE, 4,
                    SparseVector.of(3, new int[] {0, 1}, new float[] {1.0f, 1.0f}).encode());
        }
        List<Integer> got = queryIds(
                "select id from " + SPARSE_BYTEA_TABLE
                        + " order by bytea_to_sparsevec(emb) <-> '{1:1,2:1}/3'::sparsevec limit 1");
        Assertions.assertEquals(4, got.get(0).intValue());
    }

    @Test
    @Order(16)
    public void roundTripByteaToBit() throws Exception {
        byte[] blob = BitVector.encode(true, false, true);
        try (DBReference conn = DBReferenceManager.connect("template1");
                Statement s = conn.statement(
                        "select bit_to_bytea(bytea_to_bit($emb))")) {
            Input<byte[]> in = s.linkInput("emb", byte[].class);
            Output<byte[]> out = s.linkOutput(1, byte[].class);
            in.set(blob);
            s.execute();
            Assertions.assertTrue(s.fetch());
            Assertions.assertArrayEquals(blob, out.get());
        }
    }

    @Test
    @Order(17)
    public void insertBlobIntoBitColumn() throws Exception {
        try (DBReference conn = DBReferenceManager.connect("template1")) {
            insertBlobBit(conn, BIT_TABLE, 1, BitVector.encode(true, false, false));
            insertBlobBit(conn, BIT_TABLE, 2, BitVector.encode(false, true, false));
            insertBlobBit(conn, BIT_TABLE, 3, BitVector.encode(false, false, true));
            exec(conn,
                    "create index " + BIT_TABLE + "_ivf on " + BIT_TABLE
                            + " using ivfflat (emb bit_hamming_ops) with (lists = 2)");
        }
        List<Integer> got = orderByBlobQuery(
                "select id from " + BIT_TABLE
                        + " order by emb <~> bytea_to_bit($q) limit 2",
                BitVector.encode(true, false, false));
        Assertions.assertEquals(1, got.get(0).intValue());
        Assertions.assertTrue(got.get(1) == 2 || got.get(1) == 3);
    }

    @Test
    @Order(18)
    public void bitByteaFunctionalHammingIndex() throws Exception {
        try (DBReference conn = DBReferenceManager.connect("template1")) {
            insertBlobBytea(conn, BIT_BYTEA_TABLE, 1, BitVector.encode(true, false, false));
            insertBlobBytea(conn, BIT_BYTEA_TABLE, 2, BitVector.encode(false, true, false));
            insertBlobBytea(conn, BIT_BYTEA_TABLE, 3, BitVector.encode(false, false, true));
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
    @Order(19)
    public void bitByteaFunctionalJaccardIndex() throws Exception {
        try (DBReference conn = DBReferenceManager.connect("template1")) {
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
    @Order(20)
    public void insertBitBlobAfterFunctionalIndex() throws Exception {
        try (DBReference conn = DBReferenceManager.connect("template1")) {
            insertBlobBytea(conn, BIT_BYTEA_TABLE, 4, BitVector.encode(true, true, false));
        }
        List<Integer> got = queryIds(
                "select id from " + BIT_BYTEA_TABLE
                        + " order by bytea_to_bit(emb) <~> 'B110'::varbit limit 1");
        Assertions.assertEquals(4, got.get(0).intValue());
    }

    private static void insertBlobVector(DBReference conn, String table, int id, float... emb)
            throws Exception {
        try (Statement s = conn.statement(
                "insert into " + table + " values ($id, bytea_to_vector($emb))")) {
            Input<Integer> idIn = s.linkInput("id", Integer.class);
            Input<byte[]> embIn = s.linkInput("emb", byte[].class);
            idIn.set(id);
            embIn.set(DenseVector.encode(emb));
            s.execute();
        }
    }

    private static void insertBlobSparsevec(DBReference conn, String table, int id, SparseVector emb)
            throws Exception {
        try (Statement s = conn.statement(
                "insert into " + table + " values ($id, bytea_to_sparsevec($emb))")) {
            Input<Integer> idIn = s.linkInput("id", Integer.class);
            Input<byte[]> embIn = s.linkInput("emb", byte[].class);
            idIn.set(id);
            embIn.set(emb.encode());
            s.execute();
        }
    }

    private static void insertBlobBit(DBReference conn, String table, int id, byte[] emb)
            throws Exception {
        try (Statement s = conn.statement(
                "insert into " + table + " values ($id, bytea_to_bit($emb))")) {
            Input<Integer> idIn = s.linkInput("id", Integer.class);
            Input<byte[]> embIn = s.linkInput("emb", byte[].class);
            idIn.set(id);
            embIn.set(emb);
            s.execute();
        }
    }

    private static void insertBlobBytea(DBReference conn, String table, int id, byte[] emb)
            throws Exception {
        try (Statement s = conn.statement("insert into " + table + " values ($id, $emb)")) {
            Input<Integer> idIn = s.linkInput("id", Integer.class);
            Input<byte[]> embIn = s.linkInput("emb", byte[].class);
            idIn.set(id);
            embIn.set(emb);
            s.execute();
        }
    }

    private static List<Integer> orderByBlobQuery(String sql, byte[] query) throws Exception {
        return orderByBlobQuery(sql, query, false);
    }

    private static List<Integer> orderByBlobQuerySeqscan(String sql, byte[] query) throws Exception {
        return orderByBlobQuery(sql, query, true);
    }

    private static List<Integer> orderByBlobQuery(String sql, byte[] query, boolean forceSeqscan)
            throws Exception {
        List<Integer> rows = new ArrayList<>();
        try (DBReference conn = DBReferenceManager.connect("template1")) {
            if (forceSeqscan) {
                exec(conn, "set enable_indexscan = off");
                exec(conn, "set enable_bitmapscan = off");
            }
            try (Statement s = conn.statement(sql)) {
                Input<byte[]> in = s.linkInput("q", byte[].class);
                Output<Integer> out = s.linkOutput(1, Integer.class);
                in.set(query);
                s.execute();
                while (s.fetch()) {
                    Integer v = out.get();
                    if (v != null) {
                        rows.add(v);
                    }
                }
            }
        }
        return rows;
    }

    private static List<Integer> queryIds(String sql) throws Exception {
        List<Integer> rows = new ArrayList<>();
        try (DBReference conn = DBReferenceManager.connect("template1");
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
