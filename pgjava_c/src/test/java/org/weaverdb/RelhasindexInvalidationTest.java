/*-------------------------------------------------------------------------
 *
 * JNI child for RelhasindexInvalidationHarness. The Gradle JVM does not
 * load the engine.
 *
 *-------------------------------------------------------------------------
 */

package org.weaverdb;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.concurrent.TimeUnit;

import org.junit.jupiter.api.Assertions;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.Timeout;

public class RelhasindexInvalidationTest {

    @Test
    @Timeout(value = 6, unit = TimeUnit.MINUTES)
    public void otherConnectionSeesIndexAfterCreate() throws Exception {
        int rc = runChild(RelhasindexInvalidationHarness.class.getName());
        Assertions.assertEquals(0, rc, "JNI relhasindex invalidation child failed");
    }

    public static int runChild(String mainClass) throws Exception {
        Path repo = repoRoot();
        NativeBuild.assertCurrent(repo);
        Path home = repo.resolve("build_test/mtpg");
        Assertions.assertTrue(Files.isExecutable(home.resolve("bin/initdb")),
                "missing initdb under " + home);

        String dataProp = System.getProperty("weaver.datadir");
        Path data = (dataProp == null || dataProp.isBlank()
                ? repo.resolve("build/relhas-db")
                : Path.of(dataProp)).resolve("relhas");
        if (Files.exists(data)) {
            new ProcessBuilder("rm", "-rf", data.toString()).inheritIO().start().waitFor();
        }
        Path parent = data.getParent();
        if (parent != null) {
            Files.createDirectories(parent);
        }
        NativeBuild.initdb(home.resolve("bin/initdb"), data, 60);

        String trials = System.getProperty("weaver.relhas.trials", "1");
        String lib = System.getProperty("java.library.path");
        Assertions.assertNotNull(lib, "java.library.path");
        ProcessBuilder child = new ProcessBuilder(
                Path.of(System.getProperty("java.home"), "bin", "java").toString(),
                "--enable-native-access=ALL-UNNAMED",
                "-Djava.library.path=" + lib,
                "-cp", System.getProperty("java.class.path"),
                mainClass,
                data.toString(),
                trials);
        child.redirectErrorStream(true);
        Process proc = child.start();
        StringBuilder output = new StringBuilder();
        Thread reader = new Thread(() -> {
            try {
                output.append(new String(proc.getInputStream().readAllBytes()));
            } catch (Exception e) {
                output.append(e);
            }
        });
        reader.setDaemon(true);
        reader.start();
        boolean finished = proc.waitFor(5, TimeUnit.MINUTES);
        if (!finished) {
            proc.destroyForcibly();
            reader.join(5_000);
            Assertions.fail("relhasindex child timed out\n" + output);
        }
        reader.join(5_000);
        String text = output.toString();
        System.out.println(text);
        Assertions.assertEquals(0, proc.exitValue(), text);
        Assertions.assertTrue(text.contains("RELHAS OK"), text);
        return proc.exitValue();
    }

    private static Path repoRoot() {
        String prop = System.getProperty("weaver.repo.root");
        if (prop != null && !prop.isBlank()) {
            return Path.of(prop).toAbsolutePath();
        }
        Path user = Path.of(System.getProperty("user.dir")).toAbsolutePath();
        String name = user.getFileName().toString();
        if (name.equals("pgjava_c") || name.equals("connect25") || name.equals("connect21")) {
            return user.getParent();
        }
        return user;
    }
}
