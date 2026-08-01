/*-------------------------------------------------------------------------
 *
 * Index embeddings from float32 / float16 byte[] blobs via bytea_to_*.
 *
 * Covers: conversion round-trip, vector-column ingest from blobs, and
 * functional HNSW/IVFFlat indexes over a bytea column (vector + halfvec).
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
import org.weaverdb.vector.pg.DenseVector;
import org.weaverdb.vector.pg.HalfVector;

@TestMethodOrder(MethodOrderer.OrderAnnotation.class)
@ExtendWith(InstallNative.class)
public class PgvectorBlobIndexTest {

    private static final String VEC_TABLE = "pv_blob_vec_jni";
    private static final String BYTEA_TABLE = "pv_blob_ba_jni";
    private static final String HALF_BYTEA_TABLE = "pv_blob_half_ba_jni";

    @BeforeAll
    public static void setup() throws Exception {
        try (DBReference conn = DBReferenceManager.connect("template1")) {
            exec(conn, "create table " + VEC_TABLE + " (id int, emb vector)");
            exec(conn, "create table " + BYTEA_TABLE + " (id int, emb bytea)");
            exec(conn, "create table " + HALF_BYTEA_TABLE + " (id int, emb bytea)");
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
        Assertions.assertEquals(List.of(1, 2), got);
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
    public void orderByWithBlobQueryOnByteaFunctionalIndex() throws Exception {
        List<Integer> got = orderByBlobQuery(
                "select id from " + BYTEA_TABLE
                        + " order by bytea_to_vector(emb) <-> bytea_to_vector($q) limit 2",
                DenseVector.encode(0.0f, 0.0f, 1.0f));
        Assertions.assertEquals(3, got.get(0).intValue());
        Assertions.assertEquals(2, got.size());
        Assertions.assertTrue(got.get(1) == 1 || got.get(1) == 2,
                "second nearest should be id 1 or 2, got " + got.get(1));
    }

    @Test
    @Order(9)
    public void insertAfterFunctionalIndex() throws Exception {
        try (DBReference conn = DBReferenceManager.connect("template1")) {
            insertBlobBytea(conn, BYTEA_TABLE, 5, DenseVector.encode(1.0f, 1.0f, 0.0f));
        }
        List<Integer> got = orderByBlobQuery(
                "select id from " + BYTEA_TABLE
                        + " order by bytea_to_vector(emb) <-> bytea_to_vector($q) limit 1",
                DenseVector.encode(1.0f, 1.0f, 0.0f));
        Assertions.assertEquals(5, got.get(0).intValue());
    }

    @Test
    @Order(10)
    public void halfvecByteaFunctionalOrderBy() throws Exception {
        try (DBReference conn = DBReferenceManager.connect("template1")) {
            insertBlobBytea(conn, HALF_BYTEA_TABLE, 1, HalfVector.encode(1.0f, 0.0f, 0.0f));
            insertBlobBytea(conn, HALF_BYTEA_TABLE, 2, HalfVector.encode(0.0f, 1.0f, 0.0f));
            insertBlobBytea(conn, HALF_BYTEA_TABLE, 3, HalfVector.encode(0.0f, 0.0f, 1.0f));
            exec(conn,
                    "create index " + HALF_BYTEA_TABLE + "_hnsw on " + HALF_BYTEA_TABLE
                            + " using hnsw (bytea_to_halfvec(emb) halfvec_l2_ops)"
                            + " with (m = 8, ef_construction = 32)");
        }
        List<Integer> got = orderByBlobQuery(
                "select id from " + HALF_BYTEA_TABLE
                        + " order by bytea_to_halfvec(emb) <-> bytea_to_halfvec($q) limit 2",
                HalfVector.encode(1.0f, 0.0f, 0.0f));
        Assertions.assertEquals(List.of(1, 2), got);
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
        List<Integer> rows = new ArrayList<>();
        try (DBReference conn = DBReferenceManager.connect("template1");
                Statement s = conn.statement(sql)) {
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
        return rows;
    }

    private static void exec(DBReference conn, String sql) throws Exception {
        try (Statement s = conn.statement(sql)) {
            s.execute();
        }
    }
}
