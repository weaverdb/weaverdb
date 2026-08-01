/*-------------------------------------------------------------------------
 *
 * Runs pgvector ORDER BY shell regressions from Gradle (pgjava_c / connect21).
 *
 *-------------------------------------------------------------------------
 */

package org.weaverdb;

import java.nio.file.Files;
import java.nio.file.Path;
import org.junit.jupiter.api.Assertions;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.extension.ExtendWith;

@ExtendWith(InstallNative.class)
public class PgvectorOrderByShellTest {

    @BeforeAll
    public static void ensureMtpgBuild() throws Exception {
        Path repoRoot = Path.of(System.getProperty("user.dir")).toAbsolutePath().getParent();
        Path src = repoRoot.resolve("build_test/mtpg");
        Assertions.assertTrue(Files.isExecutable(src.resolve("bin/postgres")),
                "build_test/mtpg missing; run: cmake --build build_test --target postgres");
    }

    @Test
    public void orderByShellRegression() throws Exception {
        Path repoRoot = Path.of(System.getProperty("user.dir")).toAbsolutePath().getParent();
        Path script = repoRoot.resolve("mtpgsql/scripts/pgvector_orderby_smoke.sh");

        ProcessBuilder pb = new ProcessBuilder("/bin/bash", script.toString());
        pb.directory(repoRoot.toFile());
        pb.environment().put("PGVECTOR_BUILD_DIR", repoRoot.resolve("build_test").toString());
        pb.redirectErrorStream(true);

        Process p = pb.start();
        String output = new String(p.getInputStream().readAllBytes());
        int code = p.waitFor();

        if (code != 0) {
            System.err.println(output);
        }
        Assertions.assertEquals(0, code, "pgvector_orderby_smoke.sh failed:\n" + output);
        Assertions.assertTrue(output.contains("pgvector ORDER BY smoke test passed"),
                "expected success marker in:\n" + output);
    }
}
