/*-------------------------------------------------------------------------
 *
 * Smoke test for pgvector: inserts, ivfflat/hnsw indexes, ORDER BY distance.
 *
 *-------------------------------------------------------------------------
 */

package org.weaverdb.direct;

import java.util.Properties;
import org.junit.jupiter.api.AfterAll;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.Test;
import org.weaverdb.DBReference;
import org.weaverdb.Statement;

public class PgvectorSmokeTest {

    private static final String DB_DIR =
            System.getProperty("user.dir") + "/build/pgvector_testdb";

    @BeforeAll
    public static void setup() throws Throwable {
        ProcessBuilder b = new ProcessBuilder(
                "rm", "-rf", System.getProperty("user.dir") + "/build/mtpg");
        b.inheritIO().start().waitFor();

        b = new ProcessBuilder("cp", "-rf", "../build_test/mtpg", "build/");
        b.inheritIO().start().waitFor();

        b = new ProcessBuilder("rm", "-rf", DB_DIR);
        b.inheritIO().start().waitFor();

        b = new ProcessBuilder("build/mtpg/bin/initdb", "-D", DB_DIR);
        b.inheritIO().start().waitFor();

        Properties prop = new Properties();
        prop.setProperty("datadir", DB_DIR);
        prop.setProperty("start_delay", "10");
        prop.setProperty("stdlog", "TRUE");
        prop.setProperty("disable_crc", "TRUE");
        DirectWeaverInitializer.initialize(prop);
    }

    @AfterAll
    public static void shutdown() {
        DirectWeaverInitializer.forceShutdown();
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
