/*-------------------------------------------------------------------------
 *
 * ivfflat / hnsw index creation and scan coverage via Weaver prepared API.
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
public class PgvectorIndexTest {

    @BeforeAll
    public static void setup() throws Throwable {
        PgvectorWeaverTestSupport.ensureInitialized();
        try (DBReference conn = DBReference.connect("template1")) {
            exec(conn, "create table pv_idx_j (id int, emb vector)");
            exec(conn, "insert into pv_idx_j values (1, '[1,0,0]')");
            exec(conn, "insert into pv_idx_j values (2, '[0,1,0]')");
            exec(conn, "insert into pv_idx_j values (3, '[0,0,1]')");
            exec(conn, "insert into pv_idx_j values (4, '[9,0,0]')");
        }
    }

    @Test
    @Order(1)
    public void createIvfflatWithLists() throws Exception {
        try (DBReference conn = DBReference.connect("template1")) {
            exec(conn,
                    "create index pv_idx_j_ivf on pv_idx_j using ivfflat (emb vector_l2_ops) with (lists = 2)");
        }
    }

    @Test
    @Order(2)
    public void orderByLimitAfterIvfflat() throws Exception {
        assertIds("select id from pv_idx_j order by emb <-> '[1,0,0]' limit 2", 1, 2);
    }

    @Test
    @Order(3)
    public void insertThenOrderByWithIvfflatIndex() throws Exception {
        try (DBReference conn = DBReference.connect("template1")) {
            exec(conn, "insert into pv_idx_j values (5, '[1,1,0]')");
        }
        List<Integer> got = queryIntColumn(
                "select id from pv_idx_j order by emb <-> '[1,1,0]' limit 1", 1);
        Assertions.assertEquals(5, got.get(0).intValue());
    }

    @Test
    @Order(4)
    public void createHnswWithMandEfConstruction() throws Exception {
        try (DBReference conn = DBReference.connect("template1")) {
            exec(conn,
                    "create index pv_idx_j_hnsw on pv_idx_j using hnsw (emb vector_l2_ops) with (m = 8, ef_construction = 32)");
        }
    }

    @Test
    @Order(5)
    public void orderByLimitAfterHnswIndexes() throws Exception {
        List<Integer> got = queryIntColumn(
                "select id from pv_idx_j order by emb <-> '[0,0,1]' limit 2", 1);
        Assertions.assertEquals(2, got.size());
        Assertions.assertEquals(3, got.get(0).intValue());
        Assertions.assertTrue(got.get(1) == 1 || got.get(1) == 2,
                "second nearest to [0,0,1] is id 1 or 2 (tie), got " + got.get(1));
    }

    @Test
    @Order(6)
    public void createHnswWithEfConstructionOnlyOnSeparateTable() throws Exception {
        try (DBReference conn = DBReference.connect("template1")) {
            exec(conn, "create table pv_idx_ef (id int, emb vector)");
            exec(conn, "insert into pv_idx_ef values (1, '[1,0,0]')");
            exec(conn, "insert into pv_idx_ef values (2, '[0,2,0]')");
            exec(conn,
                    "create index pv_idx_ef_hnsw on pv_idx_ef using hnsw (emb vector_l2_ops) with (ef_construction = 64)");
        }
    }

    @Test
    @Order(7)
    public void insertIntoHnswIndexedTable() throws Exception {
        try (DBReference conn = DBReference.connect("template1")) {
            exec(conn, "insert into pv_idx_ef values (3, '[1,1,0]')");
        }
        List<Integer> got = queryIntColumn(
                "select id from pv_idx_ef order by emb <-> '[1,1,0]' limit 1", 1);
        Assertions.assertEquals(3, got.get(0).intValue());
    }

    @Test
    @Order(8)
    public void orderByOnEfOnlyHnswTable() throws Exception {
        assertIds("select id from pv_idx_ef order by emb <-> '[1,0,0]' limit 1", 1);
    }

    @Test
    @Order(9)
    public void listsOptionRejectedOnHnsw() {
        ExecutionException ex = Assertions.assertThrows(ExecutionException.class, () -> {
            try (DBReference conn = DBReference.connect("template1")) {
                exec(conn,
                        "create index pv_idx_bad_lists on pv_idx_j using hnsw (emb vector_l2_ops) with (lists = 2)");
            }
        });
        Assertions.assertTrue(
                ex.getMessage() != null && ex.getMessage().toLowerCase().contains("lists"),
                "expected lists-related error, got: " + ex.getMessage());
    }

    @Test
    @Order(10)
    public void mOptionRejectedOnIvfflat() {
        ExecutionException ex = Assertions.assertThrows(ExecutionException.class, () -> {
            try (DBReference conn = DBReference.connect("template1")) {
                exec(conn,
                        "create index pv_idx_bad_m on pv_idx_j using ivfflat (emb vector_l2_ops) with (m = 8)");
            }
        });
        Assertions.assertTrue(
                ex.getMessage() != null && ex.getMessage().toLowerCase().contains("m"),
                "expected m-related error, got: " + ex.getMessage());
    }

    @Test
    @Order(11)
    public void efConstructionMustSatisfyTwoM() {
        ExecutionException ex = Assertions.assertThrows(ExecutionException.class, () -> {
            try (DBReference conn = DBReference.connect("template1")) {
                exec(conn,
                        "create index pv_idx_bad_ef on pv_idx_j using hnsw (emb vector_l2_ops) with (m = 16, ef_construction = 8)");
            }
        });
        Assertions.assertNotNull(ex.getMessage());
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
