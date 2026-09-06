/*-------------------------------------------------------------------------
 *
 * Java multiuser index/heap crash-consistency test.
 *
 * Orchestrates child JVMs. Recover is always a new process against the
 * crashed datadir (no wrapup+re-init). The JUnit JVM does not load the engine.
 *
 * Smoke (default Gradle test): a few timed-kill rounds.
 * Overnight:
 *   ./gradlew :pgjava_c:indexHeapCrashLong
 *   ./gradlew :pgjava_c:indexHeapCrashLong -Dweaver.crash.duration.sec=3600
 *
 *-------------------------------------------------------------------------
 */

package org.weaverdb;

import org.junit.jupiter.api.Assertions;
import org.junit.jupiter.api.Tag;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.Timeout;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.concurrent.TimeUnit;

public class IndexHeapCrashConsistencyJavaTest {

    @Test
    @Timeout(value = 6, unit = TimeUnit.MINUTES)
    public void javaMultiuserCrashSmoke() throws Exception {
        Path repoRoot = Path.of(System.getProperty("weaver.repo.root",
                Path.of(System.getProperty("user.dir")).toAbsolutePath().getParent().toString()));
        Assertions.assertTrue(Files.isExecutable(repoRoot.resolve("build_test/mtpg/bin/initdb")),
                "build_test/mtpg missing initdb; run: cmake --build build_test --target postgres");

        IndexHeapCrashHarness.Config cfg = IndexHeapCrashHarness.Config.smoke();
        cfg.repoRoot = repoRoot;
        if (System.getProperty("weaver.crash.rounds") != null) {
            cfg.rounds = Integer.parseInt(System.getProperty("weaver.crash.rounds"));
        }
        if (System.getProperty("weaver.crash.scenario") != null) {
            cfg.scenarioFilter = System.getProperty("weaver.crash.scenario");
        }
        int rc = IndexHeapCrashHarness.run(cfg);
        Assertions.assertEquals(0, rc, "Java index/heap crash smoke failed (see stdout)");
    }

    @Test
    @Tag("crash-long")
    @Timeout(value = 8, unit = TimeUnit.HOURS)
    public void javaMultiuserCrashLong() throws Exception {
        Path repoRoot = Path.of(System.getProperty("weaver.repo.root",
                Path.of(System.getProperty("user.dir")).toAbsolutePath().getParent().toString()));
        Assertions.assertTrue(Files.isExecutable(repoRoot.resolve("build_test/mtpg/bin/initdb")),
                "build_test/mtpg missing initdb; run: cmake --build build_test --target postgres");

        IndexHeapCrashHarness.Config cfg = IndexHeapCrashHarness.Config.longRun();
        cfg.repoRoot = repoRoot;
        int rc = IndexHeapCrashHarness.run(cfg);
        Assertions.assertEquals(0, rc, "Java index/heap crash long run failed (see stdout)");
    }
}
