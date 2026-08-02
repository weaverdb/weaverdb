/*-------------------------------------------------------------------------
 *
 * Shared one-time Weaver + mtpg setup for pgvector JUnit tests.
 *
 *-------------------------------------------------------------------------
 */

package org.weaverdb.direct;

import java.nio.file.Path;
import java.util.Properties;
import org.weaverdb.DBReference;
import org.weaverdb.ExecutionException;
import org.weaverdb.Statement;

final class PgvectorWeaverTestSupport {

    static final String DB_DIR =
            System.getProperty("user.dir") + "/build/testdb";

    private static boolean initialized;

    private PgvectorWeaverTestSupport() {
    }

    static synchronized void ensureInitialized() throws Throwable {
        if (!initialized) {
            Path connect25 = Path.of(System.getProperty("user.dir")).toAbsolutePath();

            /*
             * Stage build/mtpg before any DirectWeaverInitializer reference.
             * That class's static System.loadLibrary("weaver") pins the dylib
             * already under java.library.path; copying afterward cannot refresh it.
             */
            ProcessBuilder b = new ProcessBuilder(
                    "rm", "-rf", connect25.resolve("build/mtpg").toString());
            b.inheritIO().start().waitFor();

            b = new ProcessBuilder("cp", "-rf",
                    connect25.getParent().resolve("build_test/mtpg").toString(),
                    connect25.resolve("build/mtpg").toString());
            b.inheritIO().start().waitFor();

            if (!DirectWeaverInitializer.isBackendLoaded()) {
                b = new ProcessBuilder("rm", "-rf", DB_DIR);
                b.inheritIO().start().waitFor();

                b = new ProcessBuilder(
                        connect25.resolve("build/mtpg/bin/initdb").toString(), "-D", DB_DIR);
                b.inheritIO().start().waitFor();

                Properties prop = new Properties();
                prop.setProperty("datadir", DB_DIR);
                prop.setProperty("start_delay", "10");
                prop.setProperty("stdlog", "TRUE");
                prop.setProperty("disable_crc", "TRUE");
                /*
                 * SortMem (kB) backs maintenance_work_mem for IVFFlat kmeans.
                 * Default 512 is too small for non-trivial lists; allow override.
                 */
                prop.setProperty("sortmem",
                        System.getProperty("weaver.sortmem", "131072"));
                DirectWeaverInitializer.initialize(prop);
            }
            initialized = true;
        }
        resetAbortedTransaction();
    }

    /** Clear failed txn state left by earlier tests (e.g. DirectInitTest.testBadBind). */
    private static void resetAbortedTransaction() {
        try (DBReference conn = DBReference.connect("template1");
                Statement s = conn.statement("rollback")) {
            s.execute();
        } catch (Exception ignored) {
            /* no open transaction */
        }
    }

    static synchronized void shutdownIfInitialized() {
        if (!initialized) {
            return;
        }
        DirectWeaverInitializer.forceShutdown();
        initialized = false;
    }
}
