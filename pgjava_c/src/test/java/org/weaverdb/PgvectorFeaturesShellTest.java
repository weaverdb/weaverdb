/*-------------------------------------------------------------------------
 *
 * Runs pgvector broad feature shell regressions from Gradle (pgjava_c / connect21).
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
public class PgvectorFeaturesShellTest {

    @BeforeAll
    public static void ensureMtpgBuild() throws Exception {
        Path src = NativeBuild.nativeHome();
        Assertions.assertTrue(Files.isExecutable(src.resolve("bin/postgres")),
                "missing " + src + "; run ./gradlew buildNative");
    }

    @Test
    public void featuresShellRegression() throws Exception {
        Path repoRoot = NativeBuild.repoRoot();
        Path script = repoRoot.resolve("mtpgsql/scripts/pgvector_features_smoke.sh");

        ProcessBuilder pb = new ProcessBuilder("/bin/bash", script.toString());
        pb.directory(repoRoot.toFile());
        pb.environment().put("PGVECTOR_BUILD_DIR", NativeBuild.nativeHome().getParent().toString());
        pb.redirectErrorStream(true);

        Process p = pb.start();
        String output = new String(p.getInputStream().readAllBytes());
        int code = p.waitFor();

        NativeBuild.appendLog(output);
        if (code != 0) {
            System.err.println(output);
        }
        Assertions.assertEquals(0, code, "pgvector_features_smoke.sh failed:\n" + output);
        Assertions.assertTrue(output.contains("pgvector features smoke test passed"),
                "expected success marker in:\n" + output);
    }
}
