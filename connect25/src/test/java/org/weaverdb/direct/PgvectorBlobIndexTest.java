/*-------------------------------------------------------------------------
 *
 * Blob→vector conversion and functional indexes via real byte[] binds (FFM).
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
import org.weaverdb.vector.pg.DenseVector;
import org.weaverdb.vector.pg.HalfVector;

@TestMethodOrder(MethodOrderer.OrderAnnotation.class)
public class PgvectorBlobIndexTest {

    private static final String VEC_TABLE = "pv_blob_vec_w25";
    private static final String BYTEA_TABLE = "pv_blob_ba_w25";
    private static final String HALF_BYTEA_TABLE = "pv_blob_half_ba_w25";

    @BeforeAll
    public static void setup() throws Throwable {
        PgvectorWeaverTestSupport.ensureInitialized();
        try (DBReference conn = DBReference.connect("template1")) {
            exec(conn, "create table " + VEC_TABLE + " (id int, emb vector)");
            exec(conn, "create table " + BYTEA_TABLE + " (id int, emb bytea)");
            exec(conn, "create table " + HALF_BYTEA_TABLE + " (id int, emb bytea)");
            insertBytea(conn, VEC_TABLE, 1, DenseVector.encode(1.0f, 0.0f, 0.0f), true);
            insertBytea(conn, VEC_TABLE, 2, DenseVector.encode(0.0f, 1.0f, 0.0f), true);
            insertBytea(conn, VEC_TABLE, 3, DenseVector.encode(0.0f, 0.0f, 1.0f), true);
            insertBytea(conn, VEC_TABLE, 4, DenseVector.encode(9.0f, 0.0f, 0.0f), true);
            insertBytea(conn, BYTEA_TABLE, 1, DenseVector.encode(1.0f, 0.0f, 0.0f), false);
            insertBytea(conn, BYTEA_TABLE, 2, DenseVector.encode(0.0f, 1.0f, 0.0f), false);
            insertBytea(conn, BYTEA_TABLE, 3, DenseVector.encode(0.0f, 0.0f, 1.0f), false);
            insertBytea(conn, BYTEA_TABLE, 4, DenseVector.encode(9.0f, 0.0f, 0.0f), false);
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
        Assertions.assertEquals(List.of(1, 2), got);
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
        List<Integer> got = orderBy(
                "select id from " + BYTEA_TABLE
                        + " order by bytea_to_vector(emb) <-> bytea_to_vector($q) limit 2",
                DenseVector.encode(0.0f, 0.0f, 1.0f));
        Assertions.assertEquals(3, got.get(0).intValue());
        Assertions.assertEquals(2, got.size());
        Assertions.assertTrue(got.get(1) == 1 || got.get(1) == 2);
    }

    @Test
    @Order(6)
    public void insertAfterFunctionalIndex() throws Exception {
        try (DBReference conn = DBReference.connect("template1")) {
            insertBytea(conn, BYTEA_TABLE, 5, DenseVector.encode(1.0f, 1.0f, 0.0f), false);
        }
        List<Integer> got = orderBy(
                "select id from " + BYTEA_TABLE
                        + " order by bytea_to_vector(emb) <-> bytea_to_vector($q) limit 1",
                DenseVector.encode(1.0f, 1.0f, 0.0f));
        Assertions.assertEquals(List.of(5), got);
    }

    @Test
    @Order(7)
    public void halfvecByteaFunctionalOrderBy() throws Exception {
        try (DBReference conn = DBReference.connect("template1")) {
            insertBytea(conn, HALF_BYTEA_TABLE, 1, HalfVector.encode(1.0f, 0.0f, 0.0f), false);
            insertBytea(conn, HALF_BYTEA_TABLE, 2, HalfVector.encode(0.0f, 1.0f, 0.0f), false);
            insertBytea(conn, HALF_BYTEA_TABLE, 3, HalfVector.encode(0.0f, 0.0f, 1.0f), false);
            exec(conn,
                    "create index " + HALF_BYTEA_TABLE + "_hnsw on " + HALF_BYTEA_TABLE
                            + " using hnsw (bytea_to_halfvec(emb) halfvec_l2_ops)"
                            + " with (m = 8, ef_construction = 32)");
        }
        List<Integer> got = orderBy(
                "select id from " + HALF_BYTEA_TABLE
                        + " order by bytea_to_halfvec(emb) <-> bytea_to_halfvec($q) limit 2",
                HalfVector.encode(1.0f, 0.0f, 0.0f));
        Assertions.assertEquals(List.of(1, 2), got);
    }

    /**
     * Bind embeddings as byte[]. Direct BINARY bind works for modest sizes;
     * oversized payloads use a stream channel (avoids fixed transfer buffer).
     */
    private static void insertBytea(DBReference conn, String table, int id, byte[] emb,
            boolean asVector) throws Exception {
        String sql = asVector
                ? "insert into " + table + " values ($id, bytea_to_vector($emb))"
                : "insert into " + table + " values ($id, $emb)";
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

    private static void exec(DBReference conn, String sql) throws Exception {
        try (Statement s = conn.statement(sql)) {
            s.execute();
        }
    }
}
