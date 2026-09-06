/*-------------------------------------------------------------------------
 *
 * Parent orchestrator for Java multiuser index/heap crash-consistency.
 *
 * Each round:
 *   1. initdb (CLI; bootstrap xid is all-or-nothing)
 *   2. setup JVM — WeaverInitializer / GoMultiuser, durable schema, wrapup
 *   3. crash JVM — same multiuser path; SIGKILL or write-interceptor kill
 *   4. recover JVM — shadow-log replay, VACUUM, index TID vs heap checks
 *
 * The Gradle test JVM never loads the engine. Killing a child does not
 * take down JUnit. Production code has no crash points.
 *
 *-------------------------------------------------------------------------
 */

package org.weaverdb;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.time.Duration;
import java.time.Instant;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.Random;
import java.util.concurrent.TimeUnit;

public final class IndexHeapCrashHarness {

    public static final class Config {
        public Path repoRoot;
        public Path mtpg;
        public long seed = Instant.now().getEpochSecond();
        public int rounds = 25;
        public Integer durationSec;
        public int startN = 24;
        public int maxId = 48;
        public int burst = 400;
        public int timeoutSec = 90;
        public boolean includeWrites = true;
        public boolean keepOnFail;
        public String scenarioFilter;
        public boolean smoke;
        public IndexHeapCrashSupport.Scenario[] scenarioPool;

        public static Config smoke() {
            Config c = new Config();
            c.smoke = true;
            c.rounds = 3;
            c.startN = 12;
            c.maxId = 24;
            c.burst = 80;
            c.includeWrites = false;
            c.scenarioPool = IndexHeapCrashSupport.Scenario.timedOnly();
            return c;
        }

        public static Config longRun() {
            return fromSystemProperties(new Config());
        }

        public static Config fromSystemProperties(Config defaults) {
            Config c = defaults;
            String v;
            if ((v = System.getProperty("weaver.crash.seed")) != null) {
                c.seed = Long.parseLong(v);
            }
            if ((v = System.getProperty("weaver.crash.rounds")) != null) {
                c.rounds = Integer.parseInt(v);
            }
            if ((v = System.getProperty("weaver.crash.duration.sec")) != null && !v.isBlank()) {
                c.durationSec = Integer.parseInt(v);
            }
            if ((v = System.getProperty("weaver.crash.timeout.sec")) != null) {
                c.timeoutSec = Integer.parseInt(v);
            }
            if ((v = System.getProperty("weaver.crash.scenario")) != null && !v.isBlank()) {
                c.scenarioFilter = v;
            }
            if ((v = System.getProperty("weaver.crash.include.writes")) != null) {
                c.includeWrites = v.equals("1") || Boolean.parseBoolean(v);
            }
            if ((v = System.getProperty("weaver.crash.keep")) != null) {
                c.keepOnFail = v.equals("1") || Boolean.parseBoolean(v);
            }
            if ("1".equals(System.getProperty("weaver.crash.smoke"))) {
                c.smoke = true;
                if (System.getProperty("weaver.crash.rounds") == null) {
                    c.rounds = 3;
                }
                c.startN = 12;
                c.maxId = 24;
                c.burst = 80;
                if (System.getProperty("weaver.crash.include.writes") == null) {
                    c.includeWrites = false;
                }
                if (c.scenarioPool == null) {
                    c.scenarioPool = IndexHeapCrashSupport.Scenario.timedOnly();
                }
            }
            return c;
        }
    }

    private IndexHeapCrashHarness() {
    }

    public static void main(String[] args) throws Exception {
        Config c = Config.longRun();
        for (String a : args) {
            if ("--smoke".equals(a)) {
                c = Config.smoke();
            }
        }
        System.exit(run(c));
    }

    public static int run(Config cfg) throws Exception {
        resolvePaths(cfg);
        Path initdb = cfg.mtpg.resolve("bin/initdb");
        if (!Files.isExecutable(initdb)) {
            System.err.println("missing " + initdb + "; run: cmake --build build_test --target postgres");
            return 1;
        }
        Path injector = injectorLib(cfg.mtpg);
        boolean hasInjector = injector != null;
        if (cfg.includeWrites && !hasInjector) {
            System.err.println("write-interceptor library missing; timed SIGKILL only");
            cfg.includeWrites = false;
        }

        Random rng = new Random(cfg.seed);
        Instant start = Instant.now();
        System.out.println("index_heap_crash_java: seed=" + cfg.seed
                + " rounds=" + cfg.rounds
                + " mtpg=" + cfg.mtpg
                + " injector=" + (hasInjector ? 1 : 0)
                + " mode=multiuser shadowlog=on"
                + (cfg.smoke ? " smoke=1" : ""));

        int completed = 0;
        for (int round = 1; round <= cfg.rounds; round++) {
            if (cfg.durationSec != null
                    && Duration.between(start, Instant.now()).getSeconds() >= cfg.durationSec) {
                System.out.println("duration cap " + cfg.durationSec + "s reached after "
                        + completed + " rounds");
                break;
            }
            int retries = 0;
            while (true) {
                int rc = runRound(cfg, rng, round, injector);
                if (rc == IndexHeapCrashSupport.RecoverCode.BOOTSTRAP_XID.exit) {
                    retries++;
                    if (retries >= 5) {
                        System.err.println("FAIL: round " + round
                                + ": recover missing bootstrap xid after 5 initdb retries");
                        return 1;
                    }
                    System.out.println("  re-running initdb for round " + round
                            + " (attempt " + (retries + 1) + ")");
                    continue;
                }
                if (rc != 0) {
                    return rc;
                }
                break;
            }
            completed++;
            System.out.println("OK round " + round);
        }
        System.out.println("index_heap_crash_java: OK seed=" + cfg.seed + " rounds=" + completed);
        return 0;
    }

    static void resolvePaths(Config cfg) {
        if (cfg.repoRoot == null) {
            String prop = System.getProperty("weaver.repo.root");
            Path user = Path.of(System.getProperty("user.dir")).toAbsolutePath();
            cfg.repoRoot = prop != null ? Path.of(prop).toAbsolutePath()
                    : "pgjava_c".equals(user.getFileName().toString()) ? user.getParent() : user;
        }
        if (cfg.mtpg == null) {
            Path test = cfg.repoRoot.resolve("build_test/mtpg");
            cfg.mtpg = Files.isDirectory(test) ? test : cfg.repoRoot.resolve("build/mtpg");
        }
    }

    private static int runRound(Config cfg, Random rng, int round, Path injector) throws Exception {
        IndexHeapCrashSupport.Scenario scenario = pickScenario(cfg, rng);
        Path datadir = Files.createTempDirectory("weaver-ihc-java-");
        Path workdir = Files.createTempDirectory("weaver-ihc-java-sql-");
        boolean keep = false;
        try {
            System.out.println("-- round " + round + " scenario=" + scenario.name().toLowerCase(Locale.ROOT)
                    + " --");
            int init = runProcess(new ProcessBuilder(cfg.mtpg.resolve("bin/initdb").toString(),
                    "-D", datadir.toString()),
                    workdir.resolve("initdb.out"), cfg.timeoutSec);
            if (init != 0) {
                System.err.println(read(workdir.resolve("initdb.out")));
                System.err.println("FAIL: initdb exited " + init);
                keep = cfg.keepOnFail;
                return 1;
            }

            int setup = spawnWorker(cfg, datadir, workdir, "setup", scenario, cfg.seed + round,
                    null, false, injector);
            System.out.println("  setup rc=" + setup);
            if (setup != 0) {
                dump(workdir, "setup");
                keep = cfg.keepOnFail;
                return isBootstrapXidResult(setup, read(workdir.resolve("setup.out")))
                        ? IndexHeapCrashSupport.RecoverCode.BOOTSTRAP_XID.exit : 1;
            }
            System.out.print(read(workdir.resolve("setup.out")));
            int probe = spawnWorker(cfg, datadir, workdir, "probe", scenario, cfg.seed + round,
                    null, false, injector);
            if (probe != 0) {
                dump(workdir, "setup");
                dump(workdir, "probe");
                keep = cfg.keepOnFail;
                System.err.println("FAIL: setup rows were not durable after wrapup");
                return isBootstrapXidResult(probe, read(workdir.resolve("probe.out")))
                        ? IndexHeapCrashSupport.RecoverCode.BOOTSTRAP_XID.exit : 1;
            }
            System.out.print(read(workdir.resolve("probe.out")));

            Path gate = workdir.resolve("crash.gate");
            Files.deleteIfExists(gate);
            Process crash = startWorker(cfg, datadir, workdir, "crash", scenario,
                    cfg.seed + round * 17L, gate, scenario.usesWrites(), injector);
            boolean ready = waitReady(crash, gate, Duration.ofSeconds(Math.min(cfg.timeoutSec, 20)));
            if (!ready && !crash.isAlive()) {
                dump(workdir, "crash");
                System.err.println("FAIL: crash worker died before ready");
                keep = cfg.keepOnFail;
                return 1;
            }
            if (scenario.usesWrites()) {
                if (!crash.waitFor(cfg.timeoutSec, TimeUnit.SECONDS)) {
                    crash.destroyForcibly();
                    crash.waitFor(5, TimeUnit.SECONDS);
                }
            } else {
                double delay = 0.4 + rng.nextInt(1200) / 1000.0;
                System.out.println("  crash mode=timed delay=" + String.format(Locale.ROOT, "%.3f", delay)
                        + "s (after ready)");
                Thread.sleep((long) (delay * 1000));
                crash.destroyForcibly();
                crash.waitFor(5, TimeUnit.SECONDS);
            }
            System.out.println("  child exited rc=" + crash.exitValue());
            Thread.sleep(150);

            int recover = spawnWorker(cfg, datadir, workdir, "recover", scenario,
                    cfg.seed + round * 31L, null, false, injector);
            String recoverOut = read(workdir.resolve("recover.out"));
            System.out.print(recoverOut);
            if (isBootstrapXidResult(recover, recoverOut)) {
                return IndexHeapCrashSupport.RecoverCode.BOOTSTRAP_XID.exit;
            }
            if (recover != 0 || !recoverOut.contains("IHC: CONSISTENT")) {
                dump(workdir, "recover");
                System.err.println("FAIL: recover did not confirm index/heap consistency");
                keep = cfg.keepOnFail;
                return 1;
            }
            return 0;
        } finally {
            if (!keep) {
                deleteQuiet(datadir);
                deleteQuiet(workdir);
            } else {
                System.err.println("keeping datadir " + datadir + " workdir " + workdir);
            }
        }
    }

    private static IndexHeapCrashSupport.Scenario pickScenario(Config cfg, Random rng) {
        if (cfg.scenarioFilter != null && !cfg.scenarioFilter.isBlank()) {
            return IndexHeapCrashSupport.Scenario.parse(cfg.scenarioFilter);
        }
        IndexHeapCrashSupport.Scenario[] pool = cfg.scenarioPool;
        if (pool == null || pool.length == 0) {
            pool = cfg.includeWrites
                    ? IndexHeapCrashSupport.Scenario.all()
                    : IndexHeapCrashSupport.Scenario.timedOnly();
        }
        return pool[rng.nextInt(pool.length)];
    }

    private static boolean isBootstrapXidResult(int rc, String out) {
        if (rc == IndexHeapCrashSupport.RecoverCode.BOOTSTRAP_XID.exit) {
            return true;
        }
        if (out == null) {
            return false;
        }
        return out.contains("IHC: BOOTSTRAP_XID")
                || IndexHeapCrashSupport.containsIgnoreCase(out, "this should not be happening");
    }

    private static int spawnWorker(Config cfg, Path datadir, Path workdir, String role,
            IndexHeapCrashSupport.Scenario scenario, long seed, Path gate, boolean injectWrites,
            Path injector) throws Exception {
        Process p = startWorker(cfg, datadir, workdir, role, scenario, seed, gate, injectWrites, injector);
        if (!p.waitFor(cfg.timeoutSec, TimeUnit.SECONDS)) {
            p.destroyForcibly();
            p.waitFor(5, TimeUnit.SECONDS);
            dump(workdir, role);
            System.err.println("FAIL: " + role + " worker timed out");
            return 1;
        }
        return p.exitValue();
    }

    private static Process startWorker(Config cfg, Path datadir, Path workdir, String role,
            IndexHeapCrashSupport.Scenario scenario, long seed, Path gate, boolean injectWrites,
            Path injector) throws IOException {
        Path java = Path.of(System.getProperty("java.home"), "bin", "java");
        List<String> cmd = new ArrayList<>();
        cmd.add(java.toString());
        cmd.add("--enable-native-access=ALL-UNNAMED");
        cmd.add("-Djava.library.path=" + cfg.mtpg.resolve("lib"));
        cmd.add("-Xmx512m");
        String cp = System.getProperty("java.class.path");
        if (cp != null && !cp.isBlank()) {
            cmd.add("-cp");
            cmd.add(cp);
        }
        cmd.add("org.weaverdb.IndexHeapCrashWorker");
        cmd.add(role);
        cmd.add(datadir.toAbsolutePath().toString());
        cmd.add("--scenario=" + scenario.name());
        cmd.add("--seed=" + seed);
        cmd.add("--startN=" + cfg.startN);
        cmd.add("--maxId=" + cfg.maxId);
        cmd.add("--burst=" + cfg.burst);
        if (gate != null) {
            cmd.add("--gate=" + gate.toAbsolutePath());
        }
        cmd.add("--trace=" + workdir.resolve(role + ".trace").toAbsolutePath());

        ProcessBuilder pb = new ProcessBuilder(cmd);
        pb.directory(cfg.repoRoot.toFile());
        pb.redirectErrorStream(true);
        pb.redirectOutput(workdir.resolve(role + ".out").toFile());
        pb.environment().remove("DYLD_INSERT_LIBRARIES");
        pb.environment().remove("DYLD_FORCE_FLAT_NAMESPACE");
        pb.environment().remove("LD_PRELOAD");
        pb.environment().remove("WEAVER_CRASH_AFTER_WRITES");
        pb.environment().remove("WEAVER_CRASH_MIN_BYTES");
        pb.environment().remove("WEAVER_CRASH_GATE");
        String lib = cfg.mtpg.resolve("lib").toString();
        prependEnv(pb, "DYLD_LIBRARY_PATH", lib);
        prependEnv(pb, "LD_LIBRARY_PATH", lib);
        if (injectWrites && injector != null && gate != null) {
            int writes = 40 + new Random(seed).nextInt(80);
            System.out.println("  crash mode=writes after_writes=" + writes + " injector=" + injector);
            pb.environment().put("WEAVER_CRASH_AFTER_WRITES", Integer.toString(writes));
            pb.environment().put("WEAVER_CRASH_MIN_BYTES", "8192");
            pb.environment().put("WEAVER_CRASH_GATE", gate.toAbsolutePath().toString());
            if (isDarwin()) {
                pb.environment().put("DYLD_INSERT_LIBRARIES", injector.toString());
            } else {
                pb.environment().put("LD_PRELOAD", injector.toString());
            }
        } else if (gate != null) {
            pb.environment().put("WEAVER_CRASH_GATE", gate.toAbsolutePath().toString());
        }
        return pb.start();
    }

    private static boolean waitReady(Process p, Path gate, Duration limit) throws InterruptedException {
        Instant deadline = Instant.now().plus(limit);
        while (Instant.now().isBefore(deadline)) {
            if (Files.isRegularFile(gate)) {
                return true;
            }
            if (!p.isAlive()) {
                return false;
            }
            Thread.sleep(50);
        }
        return Files.isRegularFile(gate);
    }

    private static Path injectorLib(Path mtpg) {
        String ext = isDarwin() ? "dylib" : "so";
        Path p = mtpg.resolve("lib/libweaver_crash_injector." + ext);
        return Files.isRegularFile(p) ? p : null;
    }

    private static boolean isDarwin() {
        return System.getProperty("os.name", "").toLowerCase(Locale.ROOT).contains("mac");
    }

    private static void prependEnv(ProcessBuilder pb, String key, String value) {
        String cur = pb.environment().get(key);
        pb.environment().put(key, cur == null || cur.isBlank() ? value : value + ":" + cur);
    }

    private static int runProcess(ProcessBuilder pb, Path out, int timeoutSec)
            throws Exception {
        pb.redirectErrorStream(true);
        pb.redirectOutput(out.toFile());
        Process p = pb.start();
        if (!p.waitFor(timeoutSec, TimeUnit.SECONDS)) {
            p.destroyForcibly();
            p.waitFor(5, TimeUnit.SECONDS);
            return 124;
        }
        return p.exitValue();
    }

    private static String read(Path p) throws IOException {
        if (!Files.isRegularFile(p)) {
            return "";
        }
        return Files.readString(p, StandardCharsets.UTF_8);
    }

    private static void dump(Path workdir, String role) {
        try {
            System.err.println("---- " + role + " output ----");
            System.err.println(read(workdir.resolve(role + ".out")));
            System.err.println("---- " + role + " trace ----");
            System.err.println(read(workdir.resolve(role + ".trace")));
        } catch (IOException ignored) {
        }
    }

    private static void deleteQuiet(Path dir) {
        if (dir == null || !Files.exists(dir)) {
            return;
        }
        try (var walk = Files.walk(dir)) {
            walk.sorted((a, b) -> b.getNameCount() - a.getNameCount())
                    .forEach(p -> {
                        try {
                            Files.deleteIfExists(p);
                        } catch (IOException ignored) {
                        }
                    });
        } catch (IOException ignored) {
        }
    }
}
