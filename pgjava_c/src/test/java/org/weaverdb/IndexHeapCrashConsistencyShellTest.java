/*-------------------------------------------------------------------------
 *
 * Gradle wrapper for the C embed index/heap crash-consistency harness.
 * The script SIGKILLs a weaver_embed_sql process (multiuser + pg_shadowlog);
 * it does not load crashers into production code or into this JVM.
 *
 * Java-API equivalent (child JVM + WeaverInitializer):
 * {@link IndexHeapCrashConsistencyJavaTest} and {@code ./gradlew :pgjava_c:indexHeapCrashLong}.
 *
 *-------------------------------------------------------------------------
 */

package org.weaverdb;

import java.nio.file.Files;
import java.nio.file.Path;
import org.junit.jupiter.api.Assertions;
import org.junit.jupiter.api.Test;

public class IndexHeapCrashConsistencyShellTest {

    @Test
    public void indexHeapCrashConsistencySmoke() throws Exception {
        Path repoRoot = Path.of(System.getProperty("weaver.repo.root",
                Path.of(System.getProperty("user.dir")).toAbsolutePath().getParent().toString()));
        Path src = repoRoot.resolve("build_test/mtpg");
        Assertions.assertTrue(Files.isExecutable(src.resolve("bin/weaver_embed_sql")),
                "build_test/mtpg missing weaver_embed_sql; run: cmake --build build_test --target postgres");

        Path script = repoRoot.resolve("mtpgsql/scripts/index_heap_crash_consistency.sh");
        Assertions.assertTrue(Files.isRegularFile(script), "missing " + script);

        ProcessBuilder pb = new ProcessBuilder("/bin/bash", script.toString());
        pb.directory(repoRoot.toFile());
        pb.environment().put("PGVECTOR_BUILD_DIR", repoRoot.resolve("build_test").toString());
        pb.environment().put("WEAVER_CRASH_SMOKE", "1");
        pb.environment().remove("WEAVER_CRASH_SCENARIO");
        pb.environment().remove("WEAVER_CRASH_INCLUDE_WRITES");
        pb.environment().remove("DYLD_INSERT_LIBRARIES");
        pb.environment().remove("DYLD_FORCE_FLAT_NAMESPACE");
        pb.environment().remove("LD_PRELOAD");
        pb.environment().remove("WEAVER_CRASH_AFTER_WRITES");
        pb.environment().remove("WEAVER_CRASH_MIN_BYTES");
        pb.environment().remove("WEAVER_CRASH_GATE");
        pb.redirectErrorStream(true);

        Process p = pb.start();
        String output = new String(p.getInputStream().readAllBytes());
        int code = p.waitFor();

        if (code != 0) {
            System.err.println(output);
        }
        Assertions.assertEquals(0, code, "index_heap_crash_consistency.sh failed:\n" + output);
        Assertions.assertTrue(output.contains("index_heap_crash_consistency: OK"),
                "expected success marker in:\n" + output);
    }
}
