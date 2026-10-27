package org.weaverdb;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.concurrent.TimeUnit;

/**
 * The battle tests run whatever postgres and libweaver are already in
 * build_test/mtpg. Gradle writes BUILD_COMMIT after it builds those
 * binaries, and initdb is killed if it does not finish.
 */
public final class NativeBuild {

    private NativeBuild() {
    }

    public static void assertCurrent(Path repo) throws Exception {
        Path stamp = repo.resolve("build_test/mtpg/BUILD_COMMIT");
        if (!Files.isRegularFile(stamp)) {
            throw new IllegalStateException("missing " + stamp
                    + "; build weaver and postgres before running tests");
        }
        String built = Files.readString(stamp).trim();
        String head = gitHead(repo);
        if (!built.equals(head)) {
            throw new IllegalStateException("native build " + built
                    + " does not match HEAD " + head
                    + "; rebuild build_test/mtpg before running tests");
        }
    }

    /**
     * initdb in its own session, so a bootstrap spin can be killed with
     * the postgres child instead of being waited on forever.
     */
    public static void initdb(Path initdb, Path data, int timeoutSeconds) throws Exception {
        ProcessBuilder init = new ProcessBuilder("setsid", initdb.toString(), "-D", data.toString());
        init.redirectErrorStream(true);
        Process proc = init.start();
        String output = read(proc, timeoutSeconds);
        if (proc.isAlive()) {
            killGroup(proc.pid());
            proc.destroyForcibly();
            proc.waitFor(5, TimeUnit.SECONDS);
            throw new IllegalStateException("initdb timed out\n" + output);
        }
        int rc = proc.exitValue();
        if (rc != 0) {
            throw new IllegalStateException("initdb failed " + rc + "\n" + output);
        }
    }

    private static String gitHead(Path repo) throws Exception {
        Process proc = new ProcessBuilder("git", "-C", repo.toString(), "rev-parse", "HEAD").start();
        String head = read(proc, 10).trim();
        if (!proc.waitFor(5, TimeUnit.SECONDS) || proc.exitValue() != 0 || head.isEmpty()) {
            throw new IllegalStateException("git rev-parse HEAD failed in " + repo);
        }
        return head;
    }

    private static void killGroup(long pid) throws Exception {
        new ProcessBuilder("kill", "-KILL", "--", "-" + pid).start().waitFor(2, TimeUnit.SECONDS);
    }

    private static String read(Process process, int timeoutSeconds) throws Exception {
        ByteArrayOutputStream buf = new ByteArrayOutputStream();
        Thread pump = new Thread(() -> {
            try (InputStream in = process.getInputStream()) {
                in.transferTo(buf);
            } catch (Exception ignored) {
            }
        }, "native-build-pump");
        pump.setDaemon(true);
        pump.start();
        process.waitFor(timeoutSeconds, TimeUnit.SECONDS);
        pump.join(2_000);
        return buf.toString(StandardCharsets.UTF_8);
    }
}
