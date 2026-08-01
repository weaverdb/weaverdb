/*-------------------------------------------------------------------------
 *
 * Blob→vector conversion and functional indexes via SQL (FFM path).
 * Java byte[] binding is covered by pgjava_c PgvectorBlobIndexTest.
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
import org.weaverdb.Output;
import org.weaverdb.Statement;

@TestMethodOrder(MethodOrderer.OrderAnnotation.class)
public class PgvectorBlobIndexTest {

    private static final String VEC_TABLE = "pv_blob_vec_w25";
    private static final String BYTEA_TABLE = "pv_blob_ba_w25";

    @BeforeAll
    public static void setup() throws Throwable {
        PgvectorWeaverTestSupport.ensureInitialized();
        try (DBReference conn = DBReference.connect("template1")) {
            exec(conn, "create table " + VEC_TABLE + " (id int, emb vector)");
            exec(conn, "create table " + BYTEA_TABLE + " (id int, emb bytea)");
            exec(conn,
                    "insert into " + VEC_TABLE
                            + " values (1, bytea_to_vector(vector_to_bytea('[1,0,0]')))");
            exec(conn,
                    "insert into " + VEC_TABLE
                            + " values (2, bytea_to_vector(vector_to_bytea('[0,1,0]')))");
            exec(conn,
                    "insert into " + VEC_TABLE
                            + " values (3, bytea_to_vector(vector_to_bytea('[0,0,1]')))");
            exec(conn,
                    "insert into " + VEC_TABLE
                            + " values (4, bytea_to_vector(vector_to_bytea('[9,0,0]')))");
            exec(conn,
                    "insert into " + BYTEA_TABLE + " values (1, vector_to_bytea('[1,0,0]'))");
            exec(conn,
                    "insert into " + BYTEA_TABLE + " values (2, vector_to_bytea('[0,1,0]'))");
            exec(conn,
                    "insert into " + BYTEA_TABLE + " values (3, vector_to_bytea('[0,0,1]'))");
            exec(conn,
                    "insert into " + BYTEA_TABLE + " values (4, vector_to_bytea('[9,0,0]'))");
        }
    }

    @Test
    @Order(1)
    public void roundTripByteaToVector() throws Exception {
        // Distance 0 means round-trip preserved the embedding bytes.
        assertIds(
                "select 1 where bytea_to_vector(vector_to_bytea('[1,0,0]')) <-> '[1,0,0]' = 0",
                1);
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
        assertIds(
                "select id from " + VEC_TABLE
                        + " order by emb <-> bytea_to_vector(vector_to_bytea('[1,0,0]')) limit 2",
                1, 2);
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
        List<Integer> got = queryIntColumn(
                "select id from " + BYTEA_TABLE
                        + " order by bytea_to_vector(emb) <-> '[0,0,1]' limit 2",
                1);
        Assertions.assertEquals(3, got.get(0).intValue());
        Assertions.assertEquals(2, got.size());
        Assertions.assertTrue(got.get(1) == 1 || got.get(1) == 2);
    }

    @Test
    @Order(6)
    public void insertAfterFunctionalIndex() throws Exception {
        try (DBReference conn = DBReference.connect("template1")) {
            exec(conn,
                    "insert into " + BYTEA_TABLE + " values (5, vector_to_bytea('[1,1,0]'))");
        }
        assertIds(
                "select id from " + BYTEA_TABLE
                        + " order by bytea_to_vector(emb) <-> '[1,1,0]' limit 1",
                5);
    }

    private static void assertIds(String sql, int... expected) throws Exception {
        List<Integer> got = queryIntColumn(sql, 1);
        Assertions.assertEquals(expected.length, got.size(), "row count for: " + sql);
        for (int i = 0; i < expected.length; i++) {
            Assertions.assertEquals(expected[i], got.get(i).intValue(), "row " + i);
        }
    }

    private static List<Integer> queryIntColumn(String sql, int columnIndex) throws Exception {
        List<Integer> rows = new ArrayList<>();
        try (DBReference conn = DBReference.connect("template1");
                Statement s = conn.statement(sql)) {
            Output<Integer> out = s.linkOutput(columnIndex, Integer.class);
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
