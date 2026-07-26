/*-------------------------------------------------------------------------
 *
 * Runs pgvector ORDER BY shell regressions from Gradle (uses connect25/build/mtpg).
 *
 *-------------------------------------------------------------------------
 */

package org.weaverdb.direct;

import java.nio.file.Files;
import java.nio.file.Path;
import org.junit.jupiter.api.Assertions;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.Test;

public class PgvectorOrderByShellTest {

    @BeforeAll
    public static void ensureMtpgBuild() throws Exception {
        Path connect25 = Path.of(System.getProperty("user.dir")).toAbsolutePath();
        Path mtpg = connect25.resolve("build/mtpg/bin/postgres");
        if (Files.isExecutable(mtpg)) {
            return;
        }
        Path src = connect25.getParent().resolve("build_test/mtpg");
        ProcessBuilder cp = new ProcessBuilder("cp", "-rf", src.toString(),
                connect25.resolve("build").toString());
        cp.inheritIO();
        Assertions.assertEquals(0, cp.start().waitFor(), "copy build_test/mtpg into connect25/build");
    }

    @Test
    public void orderByShellRegression() throws Exception {
        Path connect25 = Path.of(System.getProperty("user.dir")).toAbsolutePath();
        Path repoRoot = connect25.getParent();
        Path script = repoRoot.resolve("mtpgsql/scripts/pgvector_orderby_smoke.sh");
        Path buildDir = connect25.resolve("build");

        ProcessBuilder pb = new ProcessBuilder("/bin/bash", script.toString());
        pb.directory(repoRoot.toFile());
        pb.environment().put("PGVECTOR_BUILD_DIR", buildDir.toString());
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
