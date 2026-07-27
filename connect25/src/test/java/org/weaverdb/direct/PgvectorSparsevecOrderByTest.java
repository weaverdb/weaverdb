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
            exec(conn, "insert into pv_sv_ob_j values (1, '{1:1}/1')");
            exec(conn, "insert into pv_sv_ob_j values (2, '{2:1}/1')");
            exec(conn, "insert into pv_sv_ob_j values (3, '{3:1}/1')");
        }
    }

    @Test
    @Order(1)
    public void orderBySparsevecL2Limit2() throws Exception {
        assertIds(
                "select id from pv_sv_ob_j order by emb <-> '{1:1}/1'::sparsevec limit 2",
                1, 2);
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
        assertIds(
                "select id from pv_sv_ob_j order by emb <-> '{1:1}/1'::sparsevec limit 2",
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
