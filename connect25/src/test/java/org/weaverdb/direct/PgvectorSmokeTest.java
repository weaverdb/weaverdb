/*-------------------------------------------------------------------------
 *
 * Smoke test for pgvector via Weaver (inserts; indexes covered by shell tests).
 *
 *-------------------------------------------------------------------------
 */

package org.weaverdb.direct;

import org.junit.jupiter.api.AfterAll;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.Test;
import org.weaverdb.DBReference;
import org.weaverdb.Statement;

public class PgvectorSmokeTest {

    @BeforeAll
    public static void setup() throws Throwable {
        PgvectorWeaverTestSupport.ensureInitialized();
    }

    @AfterAll
    public static void shutdown() {
        PgvectorWeaverTestSupport.shutdownIfInitialized();
    }

    @Test
    public void vectorInsertIndexAndOrderBy() throws Exception {
        try (DBReference conn = DBReference.connect("template1")) {
            exec(conn, "create table pv_junit (id int, emb vector)");
            exec(conn, "insert into pv_junit values (1, '[1,0,0]')");
            exec(conn, "insert into pv_junit values (2, '[0,1,0]')");
            exec(conn, "insert into pv_junit values (3, '[0,0,1]')");
        }
    }

    private static void exec(DBReference conn, String sql) throws Exception {
        try (Statement s = conn.statement(sql)) {
            s.execute();
        }
    }
}
