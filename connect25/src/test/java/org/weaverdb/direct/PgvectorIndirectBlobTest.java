/*-------------------------------------------------------------------------
 *
 * Blob-indirect heap embeddings (ISINDIRECT): materialize + ORDER BY distance.
 *
 * Vectors use attstorage extended so oversized tuples span blobstorage.
 * Index AMs on very high dimensions are limited in this port; this test
 * covers heap fetch + sequential scan / planner paths.
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
import org.weaverdb.ExecutionException;
import org.weaverdb.Output;
import org.weaverdb.Statement;

@TestMethodOrder(MethodOrderer.OrderAnnotation.class)
public class PgvectorIndirectBlobTest {

    /** Exceeds MaxTupleSize so embeddings are stored as blob-indirect on heap. */
    private static final int DIM = 2200;

    private static final String TABLE = "pv_blob_w25";

    @BeforeAll
    public static void setup() throws Throwable {
        PgvectorWeaverTestSupport.ensureInitialized();
        try (DBReference conn = DBReference.connect("template1")) {
            exec(conn, "create table " + TABLE + " (id int, emb vector)");
            exec(conn, "insert into " + TABLE + " values (1, '" + unitVectorLiteral(0) + "')");
            exec(conn, "insert into " + TABLE + " values (2, '" + unitVectorLiteral(1) + "')");
        }
    }

    @Test
    @Order(1)
    public void orderByFindsNearestIndirectRow() throws Exception {
        List<Integer> got = queryIntColumn(
                "select id from " + TABLE + " order by emb <-> '" + unitVectorLiteral(0)
                        + "' limit 1",
                1);
        Assertions.assertEquals(1, got.size());
        Assertions.assertEquals(1, got.get(0).intValue());
    }

    @Test
    @Order(2)
    public void insertAndOrderByIndirectEmbedding() throws Exception {
        try (DBReference conn = DBReference.connect("template1")) {
            exec(conn, "insert into " + TABLE + " values (3, '" + unitVectorLiteral(2) + "')");
        }
        List<Integer> got = queryIntColumn(
                "select id from " + TABLE + " order by emb <-> '" + unitVectorLiteral(2)
                        + "' limit 1",
                1);
        Assertions.assertEquals(3, got.get(0).intValue());
    }

    @Test
    @Order(3)
    public void largeByteaColumnUsesExtendedStorage() throws Exception {
        String baTable = "pv_blob_ba_indirect_w25";
        try (DBReference conn = DBReference.connect("template1")) {
            exec(conn, "create table " + baTable + " (id int, emb bytea)");
            exec(conn,
                    "insert into " + baTable + " values (1, vector_to_bytea('"
                            + unitVectorLiteral(0) + "'::vector))");
            exec(conn,
                    "insert into " + baTable + " values (2, vector_to_bytea('"
                            + unitVectorLiteral(1) + "'::vector))");
        }
        List<Integer> got = queryIntColumn(
                "select id from " + baTable + " order by bytea_to_vector(emb) <-> '"
                        + unitVectorLiteral(0) + "'::vector limit 1",
                1);
        Assertions.assertEquals(1, got.get(0).intValue());
    }

    private static String unitVectorLiteral(int unitIndex) {
        StringBuilder sb = new StringBuilder(DIM * 3);
        sb.append('[');
        for (int i = 0; i < DIM; i++) {
            if (i > 0) {
                sb.append(',');
            }
            sb.append(i == unitIndex ? '1' : '0');
        }
        sb.append(']');
        return sb.toString();
    }

    private static List<Integer> queryIntColumn(String sql, int columnIndex)
            throws ExecutionException {
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
