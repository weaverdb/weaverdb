/*-------------------------------------------------------------------------
 *
 * ORDER BY halfvec L2 distance via Weaver Statement (JNI prepared path).
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
import org.weaverdb.FetchSet;
import org.weaverdb.Output;
import org.weaverdb.Statement;

@TestMethodOrder(MethodOrderer.OrderAnnotation.class)
public class PgvectorHalfvecOrderByTest {

    @BeforeAll
    public static void setup() throws Throwable {
        PgvectorWeaverTestSupport.ensureInitialized();
        try (DBReference conn = DBReference.connect("template1")) {
            exec(conn, "create table pv_half_ob_j (id int, emb halfvec)");
            exec(conn, "insert into pv_half_ob_j values (1, '[1,0,0]')");
            exec(conn, "insert into pv_half_ob_j values (2, '[0,1,0]')");
            exec(conn, "insert into pv_half_ob_j values (3, '[0,0,1]')");
            exec(conn, "insert into pv_half_ob_j values (4, '[9,0,0]')");
        }
    }

    @Test
    @Order(1)
    public void orderByHalfvecL2DistanceLimit3() throws Exception {
        assertIds(
                "select id from pv_half_ob_j order by emb <-> '[1,0,0]'::halfvec limit 3",
                1, 2, 3);
    }

    @Test
    @Order(2)
    public void orderByHalfvecL2DistanceLimit3ViaFetchSet() throws Exception {
        List<Integer> got = new ArrayList<>();
        try (DBReference conn = DBReference.connect("template1");
                Statement s = conn.statement(
                        "select id from pv_half_ob_j order by emb <-> '[1,0,0]'::halfvec limit 3")) {
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
    @Order(3)
    public void createHalfvecIvfflatIndex() throws Exception {
        try (DBReference conn = DBReference.connect("template1")) {
            exec(conn,
                    "create index pv_half_ob_j_ivf on pv_half_ob_j using ivfflat (emb halfvec_l2_ops) with (lists = 2)");
        }
    }

    @Test
    @Order(4)
    public void orderByHalfvecL2WithIvfflatIndex() throws Exception {
        assertNearestThenTiePair(
                "select id from pv_half_ob_j order by emb <-> '[1,0,0]'::halfvec limit 3",
                1, 2, 3);
    }

    @Test
    @Order(5)
    public void createHalfvecHnswIndex() throws Exception {
        try (DBReference conn = DBReference.connect("template1")) {
            exec(conn,
                    "create index pv_half_ob_j_hnsw on pv_half_ob_j using hnsw (emb halfvec_l2_ops) with (m = 8, ef_construction = 32)");
        }
    }

    @Test
    @Order(6)
    public void orderByHalfvecL2WithHnswIndex() throws Exception {
        assertNearestThenTiePair(
                "select id from pv_half_ob_j order by emb <-> '[1,0,0]'::halfvec limit 3",
                1, 2, 3);
    }

    private static void assertIds(String sql, int... expected) throws Exception {
        List<Integer> got = queryIntColumn(sql, 1);
        Assertions.assertEquals(expected.length, got.size(), "row count for: " + sql);
        for (int i = 0; i < expected.length; i++) {
            Assertions.assertEquals(expected[i], got.get(i).intValue(),
                    "column id row " + (i + 1) + " for: " + sql);
        }
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
