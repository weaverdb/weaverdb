/*-------------------------------------------------------------------------
 *
 * ORDER BY varbit ANN distance via JNI prepared Statement.
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
public class PgvectorBitOrderByTest {

    @BeforeAll
    public static void setup() throws Exception {
        try (DBReference conn = DBReferenceManager.connect("template1")) {
            exec(conn, "create table pv_jni_bit_ob (id int, emb varbit)");
            exec(conn, "insert into pv_jni_bit_ob values (1, 'B100')");
            exec(conn, "insert into pv_jni_bit_ob values (2, 'B010')");
            exec(conn, "insert into pv_jni_bit_ob values (3, 'B001')");
        }
    }

    @Test
    @Order(1)
    public void orderByHammingLimit2() throws Exception {
        assertIds(
                "select id from pv_jni_bit_ob order by emb <~> 'B100' limit 2",
                1, 2);
    }

    @Test
    @Order(2)
    public void createBitHammingIvfflatIndex() throws Exception {
        try (DBReference conn = DBReferenceManager.connect("template1")) {
            exec(conn,
                    "create index pv_jni_bit_ob_ivf on pv_jni_bit_ob using ivfflat (emb bit_hamming_ops) with (lists = 2)");
        }
    }

    @Test
    @Order(3)
    public void orderByHammingWithIvfflatIndex() throws Exception {
        assertIds(
                "select id from pv_jni_bit_ob order by emb <~> 'B100' limit 2",
                1, 2);
    }

    @Test
    @Order(4)
    public void orderByJaccardWithHnswIndex() throws Exception {
        try (DBReference conn = DBReferenceManager.connect("template1")) {
            exec(conn,
                    "create index pv_jni_bit_ob_jaccard on pv_jni_bit_ob using hnsw (emb bit_jaccard_ops) with (m = 8, ef_construction = 32)");
        }
        assertIds(
                "select id from pv_jni_bit_ob order by emb <%> 'B100' limit 2",
                1, 2);
    }

    private static void assertIds(String sql, int... expected) throws Exception {
        List<Integer> got = queryIntColumn(sql, 1);
        Assertions.assertEquals(expected.length, got.size(), "row count for: " + sql);
        for (int i = 0; i < expected.length; i++) {
            Assertions.assertEquals(expected[i], got.get(i).intValue(),
                    "column id row " + (i + 1) + " for: " + sql);
        }
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
