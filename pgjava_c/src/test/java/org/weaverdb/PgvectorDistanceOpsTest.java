/*-------------------------------------------------------------------------
 *
 * Cosine / IP opclasses, hybrid WHERE+ORDER BY (promoted from shell smokes).
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
public class PgvectorDistanceOpsTest {

    @BeforeAll
    public static void setup() throws Exception {
        try (DBReference conn = DBReferenceManager.connect("template1")) {
            exec(conn, "create table pv_jni_dist (id int, emb vector)");
            exec(conn, "insert into pv_jni_dist values (1, '[1,0,0]')");
            exec(conn, "insert into pv_jni_dist values (2, '[0,1,0]')");
            exec(conn, "insert into pv_jni_dist values (3, '[0,0,1]')");
            exec(conn, "insert into pv_jni_dist values (4, '[9,0,0]')");
            exec(conn,
                    "create index pv_jni_dist_l2_hnsw on pv_jni_dist using hnsw (emb vector_l2_ops) with (m = 8, ef_construction = 32)");
        }
    }

    @Test
    @Order(1)
    public void hybridWhereThenL2Order() throws Exception {
        assertIds("select id from pv_jni_dist where id >= 2 order by emb <-> '[1,0,0]' limit 2",
                2, 3);
    }

    @Test
    @Order(2)
    public void hybridWhereInThenL2Order() throws Exception {
        assertIds("select id from pv_jni_dist where id in (1, 4) order by emb <-> '[1,0,0]'",
                1, 4);
    }

    @Test
    @Order(3)
    public void createIpIndexesAndOrderBy() throws Exception {
        try (DBReference conn = DBReferenceManager.connect("template1")) {
            exec(conn, "create table pv_jni_ip (id int, emb vector)");
            exec(conn, "insert into pv_jni_ip values (1, '[1,0,0]')");
            exec(conn, "insert into pv_jni_ip values (2, '[0,1,0]')");
            exec(conn, "insert into pv_jni_ip values (3, '[0,0,1]')");
            exec(conn,
                    "create index pv_jni_ip_ivf on pv_jni_ip using ivfflat (emb vector_ip_ops) with (lists = 2)");
            exec(conn,
                    "create index pv_jni_ip_hnsw on pv_jni_ip using hnsw (emb vector_ip_ops) with (m = 8, ef_construction = 32)");
        }
        assertIds("select id from pv_jni_ip order by emb <#> '[1,0,0]' limit 2", 1, 2);
    }

    @Test
    @Order(4)
    public void createCosineIndexesAndOrderBy() throws Exception {
        try (DBReference conn = DBReferenceManager.connect("template1")) {
            exec(conn, "create table pv_jni_cos (id int, emb vector)");
            exec(conn, "insert into pv_jni_cos values (1, '[1,0,0]')");
            exec(conn, "insert into pv_jni_cos values (2, '[0,1,0]')");
            exec(conn, "insert into pv_jni_cos values (3, '[0,0,1]')");
            exec(conn,
                    "create index pv_jni_cos_ivf on pv_jni_cos using ivfflat (emb vector_cosine_ops) with (lists = 2)");
            exec(conn,
                    "create index pv_jni_cos_hnsw on pv_jni_cos using hnsw (emb vector_cosine_ops) with (m = 8, ef_construction = 32)");
        }
        assertIds("select id from pv_jni_cos order by emb <=> '[1,0,0]' limit 2", 1, 2);
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
