/*-------------------------------------------------------------------------
 *
 * ORDER BY sparsevec L2 distance via Weaver Statement (JNI prepared path).
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
public class PgvectorSparsevecOrderByTest {

    @BeforeAll
    public static void setup() throws Throwable {
        PgvectorWeaverTestSupport.ensureInitialized();
        try (DBReference conn = DBReference.connect("template1")) {
            exec(conn, "create table pv_sv_ob_j (id int, emb sparsevec)");
            /* Dimensions must cover the highest index (1-based SQL indices). */
            exec(conn, "insert into pv_sv_ob_j values (1, '{1:1}/3')");
            exec(conn, "insert into pv_sv_ob_j values (2, '{2:1}/3')");
            exec(conn, "insert into pv_sv_ob_j values (3, '{3:1}/3')");
        }
    }

    @Test
    @Order(1)
    public void orderBySparsevecL2Limit2() throws Exception {
        /* Unit axes are equidistant for 2nd place — same as vector ORDER BY smoke. */
        assertNearestThenTie(
                "select id from pv_sv_ob_j order by emb <-> '{1:1}/3'::sparsevec limit 2",
                1, 2, 3);
    }

    @Test
    @Order(2)
    public void createSparsevecHnswIndex() throws Exception {
        try (DBReference conn = DBReference.connect("template1")) {
            exec(conn,
                    "create index pv_sv_ob_j_hnsw on pv_sv_ob_j using hnsw (emb sparsevec_l2_ops) with (m = 8, ef_construction = 32)");
        }
    }

    @Test
    @Order(3)
    public void orderBySparsevecL2WithHnswIndex() throws Exception {
        assertNearestThenTie(
                "select id from pv_sv_ob_j order by emb <-> '{1:1}/3'::sparsevec limit 2",
                1, 2, 3);
    }

    private static void assertNearestThenTie(String sql, int nearest, int tieA, int tieB)
            throws Exception {
        List<Integer> got = queryIntColumn(sql, 1);
        Assertions.assertEquals(2, got.size(), "row count for: " + sql);
        Assertions.assertEquals(nearest, got.get(0).intValue(),
                "nearest for: " + sql);
        Assertions.assertTrue(got.get(1) == tieA || got.get(1) == tieB,
                "2nd should be " + tieA + "|" + tieB + " for: " + sql + ", got " + got);
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
