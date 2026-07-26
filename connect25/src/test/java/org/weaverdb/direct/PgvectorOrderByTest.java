/*-------------------------------------------------------------------------
 *
 * ORDER BY vector distance via Weaver Statement (prepared path).
 *
 *-------------------------------------------------------------------------
 */

package org.weaverdb.direct;

import java.util.ArrayList;
import java.util.List;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.Assertions;
import org.weaverdb.DBReference;
import org.weaverdb.ExecutionException;
import org.weaverdb.Output;
import org.weaverdb.Statement;

public class PgvectorOrderByTest {

    @BeforeAll
    public static void setup() throws Throwable {
        PgvectorWeaverTestSupport.ensureInitialized();
        try (DBReference conn = DBReference.connect("template1")) {
            exec(conn, "create table pv_ob_j (id int, emb vector)");
            exec(conn, "insert into pv_ob_j values (1, '[1,0,0]')");
            exec(conn, "insert into pv_ob_j values (2, '[0,1,0]')");
            exec(conn, "insert into pv_ob_j values (3, '[0,0,1]')");
            exec(conn, "insert into pv_ob_j values (4, '[9,0,0]')");
        }
    }

    @Test
    public void plainSelectById() throws Exception {
        assertIds("select id from pv_ob_j", 1, 2, 3, 4);
    }

    @Test
    public void orderByL2DistanceLimit3() throws Exception {
        List<Integer> got = queryIntColumn(
                "select id from pv_ob_j order by emb <-> '[1,0,0]' limit 3", 1);
        Assertions.assertTrue(got.size() >= 3, "expected at least 3 rows, got " + got);
        Assertions.assertEquals(1, got.get(0).intValue());
        Assertions.assertEquals(2, got.get(1).intValue());
        Assertions.assertEquals(3, got.get(2).intValue());
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
