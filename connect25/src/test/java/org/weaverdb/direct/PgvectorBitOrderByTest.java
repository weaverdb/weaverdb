/*-------------------------------------------------------------------------
 *
 * ORDER BY varbit ANN distance via Weaver Statement (JNI prepared path).
 *
 * Fixtures match pgvector_features_smoke.sh (B100, B110, B010) so Hamming /
 * Jaccard distances are strict: id 2 is uniquely second-nearest to B100.
 * IVFFlat with lists=2 needs probes>=2 on this tiny heap or only one list
 * is searched and LIMIT 2 can return a single row.
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
public class PgvectorBitOrderByTest {

    @BeforeAll
    public static void setup() throws Throwable {
        PgvectorWeaverTestSupport.ensureInitialized();
        try (DBReference conn = DBReference.connect("template1")) {
            exec(conn, "create table pv_bit_ob_j (id int, emb varbit)");
            exec(conn, "insert into pv_bit_ob_j values (1, 'B100')");
            exec(conn, "insert into pv_bit_ob_j values (2, 'B110')");
            exec(conn, "insert into pv_bit_ob_j values (3, 'B010')");
        }
    }

    @Test
    @Order(1)
    public void orderByHammingLimit2() throws Exception {
        assertIds(
                "select id from pv_bit_ob_j order by emb <~> 'B100' limit 2",
                1, 2);
    }

    @Test
    @Order(2)
    public void createBitHammingIvfflatIndex() throws Exception {
        try (DBReference conn = DBReference.connect("template1")) {
            exec(conn,
                    "create index pv_bit_ob_j_ivf on pv_bit_ob_j using ivfflat (emb bit_hamming_ops) with (lists = 2)");
        }
    }

    @Test
    @Order(3)
    public void orderByHammingWithIvfflatIndex() throws Exception {
        try (DBReference conn = DBReference.connect("template1")) {
            exec(conn, "set ivfflat.probes = 2");
            exec(conn, "set enable_seqscan = off");
            assertIds(conn,
                    "select id from pv_bit_ob_j order by emb <~> 'B100' limit 2",
                    1, 2);
        }
    }

    @Test
    @Order(4)
    public void orderByJaccardWithHnswIndex() throws Exception {
        try (DBReference conn = DBReference.connect("template1")) {
            exec(conn,
                    "create index pv_bit_ob_j_jaccard on pv_bit_ob_j using hnsw (emb bit_jaccard_ops) with (m = 8, ef_construction = 32)");
            exec(conn, "set enable_seqscan = off");
            assertIds(conn,
                    "select id from pv_bit_ob_j order by emb <%> 'B100' limit 2",
                    1, 2);
        }
    }

    private static void assertIds(String sql, int... expected) throws Exception {
        try (DBReference conn = DBReference.connect("template1")) {
            assertIds(conn, sql, expected);
        }
    }

    private static void assertIds(DBReference conn, String sql, int... expected)
            throws Exception {
        List<Integer> got = queryIntColumn(conn, sql, 1);
        Assertions.assertEquals(expected.length, got.size(), "row count for: " + sql);
        for (int i = 0; i < expected.length; i++) {
            Assertions.assertEquals(expected[i], got.get(i).intValue(),
                    "column id row " + (i + 1) + " for: " + sql);
        }
    }

    private static List<Integer> queryIntColumn(DBReference conn, String sql, int columnIndex)
            throws ExecutionException {
        List<Integer> rows = new ArrayList<>();
        try (Statement s = conn.statement(sql)) {
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
