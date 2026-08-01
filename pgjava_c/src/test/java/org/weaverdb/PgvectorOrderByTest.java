/*-------------------------------------------------------------------------
 *
 * ORDER BY vector distance via JNI prepared Statement (WExec/WFetch path).
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
public class PgvectorOrderByTest {

    @BeforeAll
    public static void setup() throws Exception {
        try (DBReference conn = DBReferenceManager.connect("template1")) {
            exec(conn, "create table pv_jni_ob (id int, emb vector)");
            exec(conn, "insert into pv_jni_ob values (1, '[1,0,0]')");
            exec(conn, "insert into pv_jni_ob values (2, '[0,1,0]')");
            exec(conn, "insert into pv_jni_ob values (3, '[0,0,1]')");
            exec(conn, "insert into pv_jni_ob values (4, '[9,0,0]')");
        }
    }

    @Test
    @Order(1)
    public void plainSelectById() throws Exception {
        assertIds("select id from pv_jni_ob", 1, 2, 3, 4);
    }

    @Test
    @Order(2)
    public void orderByL2DistanceLimit3() throws Exception {
        // Without an index, Seq+Sort keeps insertion order among ties.
        assertIds("select id from pv_jni_ob order by emb <-> '[1,0,0]' limit 3", 1, 2, 3);
    }

    @Test
    @Order(3)
    public void orderByL2DistanceLimit1() throws Exception {
        assertIds("select id from pv_jni_ob order by emb <-> '[1,0,0]' limit 1", 1);
    }

    @Test
    @Order(4)
    public void orderByL2DistanceLimit3ViaFetchSet() throws Exception {
        List<Integer> got = new ArrayList<>();
        try (DBReference conn = DBReferenceManager.connect("template1");
                Statement s = conn.statement(
                        "select id from pv_jni_ob order by emb <-> '[1,0,0]' limit 3")) {
            s.linkOutput(1, Integer.class);
            FetchSet.stream(s).forEach(row -> {
                Integer id = (Integer) row.get(0).get();
                if (id != null) {
                    got.add(id);
                }
            });
        }
        Assertions.assertEquals(List.of(1, 2, 3), got);
    }

    @Test
    @Order(5)
    public void createIvfflatIndex() throws Exception {
        try (DBReference conn = DBReferenceManager.connect("template1")) {
            exec(conn,
                    "create index pv_jni_ob_ivf on pv_jni_ob using ivfflat (emb vector_l2_ops) with (lists = 2)");
        }
    }

    @Test
    @Order(6)
    public void orderByL2DistanceLimit3WithIvfflatIndex() throws Exception {
        // Index Scan may reorder equidistant ids 2 and 3
        assertNearestThenTiePair(
                "select id from pv_jni_ob order by emb <-> '[1,0,0]' limit 3", 1, 2, 3);
    }

    @Test
    @Order(7)
    public void createHnswIndexWithOptions() throws Exception {
        try (DBReference conn = DBReferenceManager.connect("template1")) {
            exec(conn,
                    "create index pv_jni_ob_hnsw on pv_jni_ob using hnsw (emb vector_l2_ops) with (m = 8, ef_construction = 32)");
        }
    }

    @Test
    @Order(8)
    public void orderByL2DistanceLimit3WithHnswIndex() throws Exception {
        assertNearestThenTiePair(
                "select id from pv_jni_ob order by emb <-> '[1,0,0]' limit 3", 1, 2, 3);
    }

    @Test
    @Order(9)
    public void orderByLimitWithOffset() throws Exception {
        // After indexes exist, offset into the tied pair may be 2,3 or 3,2
        assertTiePair(
                "select id from pv_jni_ob order by emb <-> '[1,0,0]' limit 2 offset 1", 2, 3);
    }

    private static void assertIds(String sql, int... expected) throws Exception {
        List<Integer> got = queryIntColumn(sql, 1);
        Assertions.assertEquals(expected.length, got.size(), "row count for: " + sql);
        for (int i = 0; i < expected.length; i++) {
            Assertions.assertEquals(expected[i], got.get(i).intValue(),
                    "column id row " + (i + 1) + " for: " + sql);
        }
    }

    private static void assertTiePair(String sql, int a, int b) throws Exception {
        List<Integer> got = queryIntColumn(sql, 1);
        Assertions.assertEquals(2, got.size(), "row count for: " + sql);
        Assertions.assertTrue(
                (got.get(0) == a && got.get(1) == b) || (got.get(0) == b && got.get(1) == a),
                "expected tie pair {" + a + "," + b + "}, got " + got + " for: " + sql);
    }

    private static void assertNearestThenTiePair(String sql, int nearest, int tieA, int tieB)
            throws Exception {
        List<Integer> got = queryIntColumn(sql, 1);
        Assertions.assertEquals(3, got.size(), "row count for: " + sql);
        Assertions.assertEquals(nearest, got.get(0).intValue(), "nearest for: " + sql);
        Assertions.assertTrue(
                (got.get(1) == tieA && got.get(2) == tieB)
                        || (got.get(1) == tieB && got.get(2) == tieA),
                "expected tied pair {" + tieA + "," + tieB + "} after nearest, got " + got
                        + " for: " + sql);
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
