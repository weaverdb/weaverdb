/*-------------------------------------------------------------------------
 *
 * Blob-indirect heap embeddings (ISINDIRECT): materialize + ORDER BY distance.
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

@TestMethodOrder(MethodOrderer.OrderAnnotation.class)
@ExtendWith(InstallNative.class)
public class PgvectorIndirectBlobTest {

    private static final int DIM = 2200;

    private static final String TABLE = "pv_blob_jni";

    @BeforeAll
    public static void setup() throws Throwable {
        try (DBReference conn = DBReferenceManager.connect("template1")) {
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
        try (DBReference conn = DBReferenceManager.connect("template1")) {
            exec(conn, "insert into " + TABLE + " values (3, '" + unitVectorLiteral(2) + "')");
        }
        List<Integer> got = queryIntColumn(
                "select id from " + TABLE + " order by emb <-> '" + unitVectorLiteral(2)
                        + "' limit 1",
                1);
        Assertions.assertEquals(3, got.get(0).intValue());
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
        try (DBReference conn = DBReferenceManager.connect("template1");
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
