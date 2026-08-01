/*-------------------------------------------------------------------------
 *
 * UPDATE / DELETE / bulk insert / NULL emb (JNI prepared path).
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
public class PgvectorMutationsTest {

    @BeforeAll
    public static void setup() throws Exception {
        try (DBReference conn = DBReferenceManager.connect("template1")) {
            exec(conn, "create table pv_jni_mut (id int, emb vector)");
            exec(conn, "insert into pv_jni_mut values (1, '[1,0,0]')");
            exec(conn, "insert into pv_jni_mut values (2, '[0,1,0]')");
            exec(conn, "insert into pv_jni_mut values (3, '[0,0,1]')");
            exec(conn, "insert into pv_jni_mut values (4, '[9,0,0]')");
        }
    }

    @Test
    @Order(1)
    public void createDualIndexes() throws Exception {
        try (DBReference conn = DBReferenceManager.connect("template1")) {
            exec(conn,
                    "create index pv_jni_mut_ivf on pv_jni_mut using ivfflat (emb vector_l2_ops) with (lists = 2)");
            exec(conn,
                    "create index pv_jni_mut_hnsw on pv_jni_mut using hnsw (emb vector_l2_ops) with (m = 8, ef_construction = 32)");
        }
    }

    @Test
    @Order(2)
    public void bulkInsertWithBothIndexes() throws Exception {
        try (DBReference conn = DBReferenceManager.connect("template1")) {
            for (int i = 100; i < 110; i++) {
                exec(conn, "insert into pv_jni_mut values (" + i + ", '[0," + (i - 99) + ",0]')");
            }
        }
        Assertions.assertEquals(1, queryInt("select count(*) from pv_jni_mut where id = 109"));
    }

    @Test
    @Order(3)
    public void updateVectorThenOrderBy() throws Exception {
        try (DBReference conn = DBReferenceManager.connect("template1")) {
            exec(conn, "update pv_jni_mut set emb = '[0.9,0.1,0]' where id = 2");
        }
        List<Integer> got = queryIntColumn(
                "select id from pv_jni_mut order by emb <-> '[0.9,0.1,0]' limit 1", 1);
        Assertions.assertEquals(2, got.get(0).intValue());
    }

    @Test
    @Order(4)
    public void deleteRowWithIndexes() throws Exception {
        try (DBReference conn = DBReferenceManager.connect("template1")) {
            exec(conn, "delete from pv_jni_mut where id = 4");
        }
        List<Integer> ids = queryIntColumn("select id from pv_jni_mut where id = 4", 1);
        Assertions.assertTrue(ids.isEmpty());
    }

    @Test
    @Order(5)
    public void orderByStillWorksAfterMutations() throws Exception {
        List<Integer> got = queryIntColumn(
                "select id from pv_jni_mut order by emb <-> '[1,0,0]' limit 3", 1);
        Assertions.assertEquals(3, got.size());
        Assertions.assertFalse(got.contains(4));
    }

    @Test
    @Order(6)
    public void insertNullEmbeddingDoesNotCrash() throws Exception {
        try (DBReference conn = DBReferenceManager.connect("template1")) {
            exec(conn, "insert into pv_jni_mut (id, emb) values (99, null)");
        }
        Assertions.assertEquals(1, queryInt("select count(*) from pv_jni_mut where emb is null"));
    }

    @Test
    @Order(7)
    public void vacuumAfterIndexMutations() throws Exception {
        try (DBReference conn = DBReferenceManager.connect("template1")) {
            exec(conn, "vacuum pv_jni_mut");
        }
        List<Integer> got = queryIntColumn(
                "select id from pv_jni_mut where emb is not null order by emb <-> '[1,0,0]' limit 1",
                1);
        Assertions.assertFalse(got.isEmpty());
        Assertions.assertEquals(1, got.get(0).intValue());
    }

    private static int queryInt(String sql) throws ExecutionException {
        try (DBReference conn = DBReferenceManager.connect("template1");
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
