/*-------------------------------------------------------------------------
 *
 * Java multiuser index/heap crash-consistency test.
 *
 * Orchestrates child JVMs. Recover is always a new process against the
 * crashed datadir (no wrapup+re-init). The JUnit JVM does not load the engine.
 *
 * Smoke (default Gradle test): a few timed-kill rounds, 2 client threads.
 * Overnight uses 4 client threads (override with -Dweaver.crash.clients=N).
 * Each client is its own connection on its own thread inside the crash JVM.
 *   ./gradlew :pgjava_c:soakTest
 *   ./gradlew :pgjava_c:soakTest -Dweaver.crash.duration.sec=3600
 *   ./gradlew :pgjava_c:soakTest -Dweaver.crash.clients=4
 *
 * The long-run pool also includes split_churn (wide-key btree splits),
 * rotate (shadow-log fsync kills), and recover_kill (SIGKILL during replay).
 *
 *-------------------------------------------------------------------------
 */

package org.weaverdb;

import org.junit.jupiter.api.Assertions;
import org.junit.jupiter.api.Tag;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.Timeout;

import java.io.IOException;
import java.io.OutputStream;
import java.io.PrintStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;
import java.util.concurrent.TimeUnit;

public class IndexHeapCrashConsistencyJavaTest {

    @Test
    @Timeout(value = 6, unit = TimeUnit.MINUTES)
    public void javaMultiuserCrashSmoke() throws Exception {
        Path repoRoot = NativeBuild.repoRoot();
        Assertions.assertTrue(Files.isExecutable(NativeBuild.nativeHome().resolve("bin/initdb")),
                "missing initdb under " + NativeBuild.nativeHome() + "; run ./gradlew buildNative");

        IndexHeapCrashHarness.Config cfg = IndexHeapCrashHarness.Config.smoke();
        cfg.repoRoot = repoRoot;
        if (System.getProperty("weaver.crash.rounds") != null) {
            cfg.rounds = Integer.parseInt(System.getProperty("weaver.crash.rounds"));
        }
        if (System.getProperty("weaver.crash.scenario") != null) {
            cfg.scenarioFilter = System.getProperty("weaver.crash.scenario");
        }
        String keep = System.getProperty("weaver.crash.keep");
        if (keep != null && (keep.equals("1") || Boolean.parseBoolean(keep))) {
            cfg.keepOnFail = true;
        }
        int rc = runLogged(cfg);
        Assertions.assertEquals(0, rc, "Java index/heap crash smoke failed; see "
                + System.getProperty("weaver.harness.log", "stdout"));
    }

    @Test
    @Tag("crash-long")
    @Timeout(value = 8, unit = TimeUnit.HOURS)
    public void javaMultiuserCrashLong() throws Exception {
        Path repoRoot = NativeBuild.repoRoot();
        Assertions.assertTrue(Files.isExecutable(NativeBuild.nativeHome().resolve("bin/initdb")),
                "missing initdb under " + NativeBuild.nativeHome() + "; run ./gradlew buildNative");

        IndexHeapCrashHarness.Config cfg = IndexHeapCrashHarness.Config.longRun();
        cfg.repoRoot = repoRoot;
        int rc = runLogged(cfg);
        Assertions.assertEquals(0, rc, "Java index/heap crash long run failed; see "
                + System.getProperty("weaver.harness.log", "stdout"));
    }

    /** Copy harness stdout into weaver.harness.log so a failure keeps the child text. */
    private static int runLogged(IndexHeapCrashHarness.Config cfg) throws Exception {
        String path = System.getProperty("weaver.harness.log");
        if (path == null || path.isBlank()) {
            return IndexHeapCrashHarness.run(cfg);
        }
        Path log = Path.of(path);
        if (log.getParent() != null) {
            Files.createDirectories(log.getParent());
        }
        PrintStream previous = System.out;
        try (OutputStream file = Files.newOutputStream(log,
                StandardOpenOption.CREATE, StandardOpenOption.APPEND)) {
            System.setOut(new PrintStream(new Tee(previous, file), true));
            return IndexHeapCrashHarness.run(cfg);
        } finally {
            System.setOut(previous);
        }
    }

    private static final class Tee extends OutputStream {
        private final OutputStream first;
        private final OutputStream second;

        Tee(OutputStream first, OutputStream second) {
            this.first = first;
            this.second = second;
        }

        @Override
        public void write(int b) throws IOException {
            first.write(b);
            second.write(b);
        }

        @Override
        public void write(byte[] b, int off, int len) throws IOException {
            first.write(b, off, len);
            second.write(b, off, len);
        }

        @Override
        public void flush() throws IOException {
            first.flush();
            second.flush();
        }
    }
}
