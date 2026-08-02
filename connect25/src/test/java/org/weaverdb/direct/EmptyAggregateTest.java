/*-------------------------------------------------------------------------
 *
 * Empty-aggregate SEGV regression (Java 25 / FFM).
 *
 * count(*) over zero matching rows used to crash in ExecTargetList when
 * ecxt_scantuple was non-NULL but val was NULL. The stress suite hit this
 * via `where id % 2 = 0` after deleting every even id; the same crash
 * occurs for any empty aggregate filter.
 *
 *-------------------------------------------------------------------------
 */

package org.weaverdb.direct;

import org.junit.jupiter.api.Assertions;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.MethodOrderer;
import org.junit.jupiter.api.Order;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.TestMethodOrder;
import org.weaverdb.DBReference;
import org.weaverdb.Output;
import org.weaverdb.Statement;

@TestMethodOrder(MethodOrderer.OrderAnnotation.class)
public class EmptyAggregateTest {

    private static final String TABLE = "empty_agg_w25";

    @BeforeAll
    public static void setup() throws Throwable {
        PgvectorWeaverTestSupport.ensureInitialized();
        try (DBReference conn = DBReference.connect("template1")) {
            exec(conn, "create table " + TABLE + " (id int)");
            exec(conn, "insert into " + TABLE + " values (1)");
            exec(conn, "insert into " + TABLE + " values (3)");
        }
    }

    @Test
    @Order(1)
    public void countWithModuloFilterMatchingRows() throws Exception {
        Assertions.assertEquals(2, queryInt(
                "select count(*) from " + TABLE + " where id % 2 = 1"));
    }

    @Test
    @Order(2)
    public void countWithModuloFilterMatchingZeroRows() throws Exception {
        Assertions.assertEquals(0, queryInt(
                "select count(*) from " + TABLE + " where id % 2 = 0"));
    }

    @Test
    @Order(3)
    public void countWithImpossiblePredicate() throws Exception {
        Assertions.assertEquals(0, queryInt(
                "select count(*) from " + TABLE + " where id < 0"));
    }

    @Test
    @Order(4)
    public void countOnEmptyTable() throws Exception {
        try (DBReference conn = DBReference.connect("template1")) {
            exec(conn, "create table " + TABLE + "_empty (id int)");
        }
        Assertions.assertEquals(0, queryInt(
                "select count(*) from " + TABLE + "_empty"));
    }

    private static int queryInt(String sql) throws Exception {
        try (DBReference conn = DBReference.connect("template1");
                Statement s = conn.statement(sql)) {
            Output<Integer> out = s.linkOutput(1, Integer.class);
            s.execute();
            Assertions.assertTrue(s.fetch());
            return out.get().intValue();
        }
    }

    private static void exec(DBReference conn, String sql) throws Exception {
        try (Statement s = conn.statement(sql)) {
            s.execute();
        }
    }
}
