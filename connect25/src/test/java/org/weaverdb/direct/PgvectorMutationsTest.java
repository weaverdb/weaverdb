/*-------------------------------------------------------------------------
 *
 * UPDATE / DELETE / bulk insert / NULL emb on indexed pgvector tables.
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
public class PgvectorMutationsTest {

    @BeforeAll
    public static void setup() throws Throwable {
        PgvectorWeaverTestSupport.ensureInitialized();
        try (DBReference conn = DBReference.connect("template1")) {
            exec(conn, "create table pv_mut_w25 (id int, emb vector)");
            exec(conn, "insert into pv_mut_w25 values (1, '[1,0,0]')");
            exec(conn, "insert into pv_mut_w25 values (2, '[0,1,0]')");
            exec(conn, "insert into pv_mut_w25 values (3, '[0,0,1]')");
            exec(conn, "insert into pv_mut_w25 values (4, '[9,0,0]')");
        }
    }

    @Test
    @Order(1)
    public void createDualIndexes() throws Exception {
        try (DBReference conn = DBReference.connect("template1")) {
            exec(conn,
                    "create index pv_mut_w25_ivf on pv_mut_w25 using ivfflat (emb vector_l2_ops) with (lists = 2)");
            exec(conn,
                    "create index pv_mut_w25_hnsw on pv_mut_w25 using hnsw (emb vector_l2_ops) with (m = 8, ef_construction = 32)");
        }
    }

    @Test
    @Order(2)
    public void bulkInsertWithBothIndexes() throws Exception {
        try (DBReference conn = DBReference.connect("template1")) {
            for (int i = 100; i < 110; i++) {
                exec(conn, "insert into pv_mut_w25 values (" + i + ", '[0," + (i - 99) + ",0]')");
            }
        }
        Assertions.assertEquals(1, queryInt("select count(*) from pv_mut_w25 where id = 109"));
    }

    @Test
    @Order(3)
    public void updateVectorThenOrderBy() throws Exception {
        try (DBReference conn = DBReference.connect("template1")) {
            exec(conn, "update pv_mut_w25 set emb = '[0.9,0.1,0]' where id = 2");
        }
        List<Integer> got = queryIntColumn(
                "select id from pv_mut_w25 order by emb <-> '[0.9,0.1,0]' limit 1", 1);
        Assertions.assertEquals(2, got.get(0).intValue());
    }

    @Test
    @Order(4)
    public void deleteRowWithIndexes() throws Exception {
        try (DBReference conn = DBReference.connect("template1")) {
            exec(conn, "delete from pv_mut_w25 where id = 4");
        }
        List<Integer> ids = queryIntColumn("select id from pv_mut_w25 where id = 4", 1);
        Assertions.assertTrue(ids.isEmpty());
    }

    @Test
    @Order(5)
    public void orderByStillWorksAfterMutations() throws Exception {
        List<Integer> got = queryIntColumn(
                "select id from pv_mut_w25 order by emb <-> '[1,0,0]' limit 3", 1);
        Assertions.assertEquals(3, got.size());
        Assertions.assertTrue(got.contains(1));
        Assertions.assertTrue(got.contains(2));
        Assertions.assertFalse(got.contains(4));
    }

    @Test
    @Order(6)
    public void insertNullEmbeddingDoesNotCrash() throws Exception {
        try (DBReference conn = DBReference.connect("template1")) {
            exec(conn, "insert into pv_mut_w25 (id, emb) values (99, null)");
        }
        Assertions.assertEquals(1, queryInt("select count(*) from pv_mut_w25 where emb is null"));
    }

    private static int queryInt(String sql) throws ExecutionException {
        try (DBReference conn = DBReference.connect("template1");
                Statement s = conn.statement(sql)) {
            Output<Integer> out = s.linkOutput(1, Integer.class);
            s.execute();
            Assertions.assertTrue(s.fetch());
            return out.get().intValue();
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
