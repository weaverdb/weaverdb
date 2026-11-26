package org.weaverdb;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.concurrent.TimeUnit;

/**
 * Shared native tree. Gradle writes {@code BUILD_COMMIT} next to
 * {@code bin/} and {@code lib/}. Tests read that tree; they do not copy it.
 */
public final class NativeBuild {

    private NativeBuild() {
    }

    public static Path repoRoot() {
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

    /** {@code build_test/mtpg}, or {@code weaver.native.home} when Gradle sets it. */
    public static Path nativeHome() {
        String prop = System.getProperty("weaver.native.home");
        if (prop != null && !prop.isBlank()) {
            return Path.of(prop);
        }
        return repoRoot().resolve("build_test/mtpg");
    }

    /** Per-task datadir from {@code weaver.datadir}, else {@code build/testdb}. */
    public static Path dataDir() {
        String prop = System.getProperty("weaver.datadir");
        if (prop != null && !prop.isBlank()) {
            return Path.of(prop);
        }
        return Path.of(System.getProperty("user.dir"), "build", "testdb");
    }

    public static void assertCurrent(Path repo) throws Exception {
        Path stamp = nativeHome().resolve("BUILD_COMMIT");
        if (!Files.isRegularFile(stamp)) {
            throw new IllegalStateException("missing " + stamp
                    + "; run ./gradlew buildNative before tests");
        }
        String built = Files.readString(stamp).trim();
        String head = gitHead(repo);
        if (!built.equals(head)) {
            throw new IllegalStateException("native build " + built
                    + " does not match HEAD " + head
                    + "; run ./gradlew buildNative");
        }
    }

    /** Append child output to {@code weaver.harness.log} when Gradle set it. */
    public static void appendLog(String text) {
        String path = System.getProperty("weaver.harness.log");
        if (path == null || path.isBlank() || text == null) {
            return;
        }
        try {
            Path log = Path.of(path);
            Path parent = log.getParent();
            if (parent != null) {
                Files.createDirectories(parent);
            }
            Files.writeString(log, text + "\n", StandardCharsets.UTF_8,
                    java.nio.file.StandardOpenOption.CREATE,
                    java.nio.file.StandardOpenOption.APPEND);
        } catch (Exception ignored) {
        }
    }

    /**
     * initdb in its own session, so a bootstrap spin can be killed with
     * the postgres child instead of being waited on forever.
     */
    public static void initdb(Path initdb, Path data, int timeoutSeconds) throws Exception {
        Path parent = data.getParent();
        if (parent != null) {
            Files.createDirectories(parent);
        }
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
