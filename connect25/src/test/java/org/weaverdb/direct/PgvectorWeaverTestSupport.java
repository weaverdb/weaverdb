/*-------------------------------------------------------------------------
 *
 * Shared one-time Weaver + mtpg setup for pgvector JUnit tests.
 *
 *-------------------------------------------------------------------------
 */

package org.weaverdb.direct;

import java.util.Properties;

final class PgvectorWeaverTestSupport {

    static final String DB_DIR =
            System.getProperty("user.dir") + "/build/pgvector_junit_testdb";

    private static boolean initialized;

    private PgvectorWeaverTestSupport() {
    }

    static synchronized void ensureInitialized() throws Throwable {
        if (initialized) {
            return;
        }
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
        initialized = true;
    }

    static synchronized void shutdownIfInitialized() {
        if (!initialized) {
            return;
        }
        DirectWeaverInitializer.forceShutdown();
        initialized = false;
    }
}
