/*-------------------------------------------------------------------------
 *
 * Parent orchestrator for Java multiuser index/heap crash-consistency.
 *
 * Each round uses a new process for every engine lifetime:
 *   1. initdb (CLI)
 *   2. setup JVM — schema, wrapup, process exit
 *   3. crash JVM — SIGKILL or write-interceptor kill (no wrapup)
 *   4. recover JVM — cold start on that datadir, VACUUM, consistency checks,
 *      then process exit without wrapup/re-init
 *
 * Recover never wrapup+initialize in the same JVM. If recover init FATALs,
 * the parent starts another recover JVM against the same datadir. That is
 * not treated as a failed initdb.
 *
 * Setup/crash init halt (Ami xid / SYSTEM HALT before READY) still retries
 * the whole round with a new datadir.
 *
 * The Gradle test JVM never loads the engine.
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
        public int recoverJvmAttempts = 5;
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
            if ((v = System.getProperty("weaver.crash.recover.jvms")) != null) {
                c.recoverJvmAttempts = Integer.parseInt(v);
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
                + " mode=multiuser shadowlog=on recover_jvms=" + cfg.recoverJvmAttempts
                + (cfg.smoke ? " smoke=1" : ""));

        int completed = 0;
        for (int round = 1; round <= cfg.rounds; round++) {
            if (cfg.durationSec != null
                    && Duration.between(start, Instant.now()).getSeconds() >= cfg.durationSec) {
                System.out.println("duration cap " + cfg.durationSec + "s reached after "
                        + completed + " rounds");
                break;
            }
            IndexHeapCrashSupport.Scenario scenario = pickScenario(cfg, rng);
            int retries = 0;
            while (true) {
                int rc = runRound(cfg, rng, round, retries, scenario, injector);
                if (rc == IndexHeapCrashSupport.RecoverCode.BOOTSTRAP_XID.exit) {
                    retries++;
                    if (retries >= 5) {
                        System.err.println("FAIL: round " + round
                                + ": missing bootstrap xid after 5 initdb retries");
                        return 1;
                    }
                    System.out.println("  re-running initdb for round " + round
                            + " scenario=" + scenario.name().toLowerCase(Locale.ROOT)
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

    private static int runRound(Config cfg, Random rng, int round, int retry,
            IndexHeapCrashSupport.Scenario scenario, Path injector) throws Exception {
        Path datadir = Files.createTempDirectory("weaver-ihc-java-");
        Path workdir = Files.createTempDirectory("weaver-ihc-java-sql-");
        boolean keep = false;
        try {
            System.out.println("-- round " + round + " scenario=" + scenario.name().toLowerCase(Locale.ROOT)
                    + (retry > 0 ? " retry=" + retry : "") + " --");
            int init = runProcess(new ProcessBuilder(cfg.mtpg.resolve("bin/initdb").toString(),
                    "-D", datadir.toString()),
                    workdir.resolve("initdb.out"), cfg.timeoutSec);
            if (init != 0) {
                System.err.println(read(workdir.resolve("initdb.out")));
                System.err.println("FAIL: initdb exited " + init);
                keep = cfg.keepOnFail;
                return 1;
            }
            syncFilesystem();

            int setup = spawnWorker(cfg, datadir, workdir, "setup", scenario, cfg.seed + round,
                    null, false, injector);
            System.out.println("  setup rc=" + setup);
            String setupOut = read(workdir.resolve("setup.out"));
            if (setup != 0) {
                dump(workdir, "setup");
                if (isInitHalt(setup, setupOut, "IHC: SETUP_OK")) {
                    return IndexHeapCrashSupport.RecoverCode.BOOTSTRAP_XID.exit;
                }
                keep = cfg.keepOnFail;
                return 1;
            }
            System.out.print(setupOut);
            syncFilesystem();

            Path gate = workdir.resolve("crash.gate");
            Files.deleteIfExists(gate);
            long crashSeed = cfg.seed + round * 17L + retry * 1009L;
            Process crash = startWorker(cfg, datadir, workdir, "crash", scenario,
                    crashSeed, gate, scenario.usesWrites(), injector);
            boolean ready = waitReady(crash, gate, Duration.ofSeconds(Math.min(cfg.timeoutSec, 20)));
            if (!ready) {
                if (crash.isAlive()) {
                    crash.destroyForcibly();
                    crash.waitFor(5, TimeUnit.SECONDS);
                }
                dump(workdir, "crash");
                String crashOut = read(workdir.resolve("crash.out"));
                if (isInitHalt(crashExit(crash), crashOut, "IHC: READY")) {
                    return IndexHeapCrashSupport.RecoverCode.BOOTSTRAP_XID.exit;
                }
                keep = cfg.keepOnFail;
                System.err.println("FAIL: crash worker died before ready");
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
            syncFilesystem();

            int recoverJvms = Math.max(1, cfg.recoverJvmAttempts);
            String lastRecoverOut = "";
            int lastRecoverRc = 1;
            for (int rj = 1; rj <= recoverJvms; rj++) {
                String stem = "recover." + rj;
                System.out.println("  recover JVM " + rj + "/" + recoverJvms
                        + " (new process, same datadir)");
                lastRecoverRc = spawnWorker(cfg, datadir, workdir, "recover", scenario,
                        cfg.seed + round * 31L + retry * 17L + rj * 13L, null, false, injector,
                        stem);
                lastRecoverOut = read(workdir.resolve(stem + ".out"));
                System.out.print(lastRecoverOut);
                if (lastRecoverRc == 0 && lastRecoverOut.contains("IHC: CONSISTENT")) {
                    return 0;
                }
                if (isInitHalt(lastRecoverRc, lastRecoverOut, "IHC: recover vacuum")) {
                    if (rj < recoverJvms) {
                        System.out.println("  recover JVM " + rj
                                + " died at init; starting a new JVM on the same datadir");
                        continue;
                    }
                    dump(workdir, stem);
                    System.err.println("FAIL: recover init failed after " + recoverJvms
                            + " new JVMs on the crashed datadir (not retrying initdb)");
                    keep = cfg.keepOnFail;
                    return 1;
                }
                dump(workdir, stem);
                System.err.println("FAIL: recover did not confirm index/heap consistency");
                keep = cfg.keepOnFail;
                return 1;
            }
            dump(workdir, "recover." + recoverJvms);
            System.err.println("FAIL: recover did not confirm index/heap consistency");
            keep = cfg.keepOnFail;
            return 1;
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

    /**
     * Child died before the engine reached {@code startedMarker}.
     * Setup/crash: parent retries the round with a new datadir.
     * Recover: parent starts a new JVM on the same datadir.
     */
    private static boolean isInitHalt(int rc, String out, String startedMarker) {
        boolean reached = out != null && startedMarker != null && out.contains(startedMarker);
        if (out != null) {
            if (out.contains("IHC: BOOTSTRAP_XID")
                    || IndexHeapCrashSupport.containsIgnoreCase(out, "this should not be happening")) {
                return true;
            }
            if (reached) {
                return false;
            }
            if (IndexHeapCrashSupport.containsIgnoreCase(out, "SYSTEM HALT")) {
                return true;
            }
        }
        if (reached) {
            return false;
        }
        return rc == IndexHeapCrashSupport.RecoverCode.BOOTSTRAP_XID.exit
                || rc == 134 || rc == 6;
    }

    private static int crashExit(Process crash) {
        try {
            return crash.exitValue();
        } catch (IllegalThreadStateException e) {
            return 1;
        }
    }

    private static void syncFilesystem() {
        try {
            Process p = new ProcessBuilder("sync")
                    .redirectOutput(ProcessBuilder.Redirect.DISCARD)
                    .redirectError(ProcessBuilder.Redirect.DISCARD)
                    .start();
            p.waitFor(10, TimeUnit.SECONDS);
        } catch (Exception ignored) {
        }
    }

    private static int spawnWorker(Config cfg, Path datadir, Path workdir, String role,
            IndexHeapCrashSupport.Scenario scenario, long seed, Path gate, boolean injectWrites,
            Path injector) throws Exception {
        return spawnWorker(cfg, datadir, workdir, role, scenario, seed, gate, injectWrites, injector, role);
    }

    private static int spawnWorker(Config cfg, Path datadir, Path workdir, String role,
            IndexHeapCrashSupport.Scenario scenario, long seed, Path gate, boolean injectWrites,
            Path injector, String outStem) throws Exception {
        Process p = startWorker(cfg, datadir, workdir, role, scenario, seed, gate, injectWrites, injector, outStem);
        if (!p.waitFor(cfg.timeoutSec, TimeUnit.SECONDS)) {
            p.destroyForcibly();
            p.waitFor(5, TimeUnit.SECONDS);
            dump(workdir, outStem);
            System.err.println("FAIL: " + role + " worker timed out");
            return 1;
        }
        return p.exitValue();
    }

    private static Process startWorker(Config cfg, Path datadir, Path workdir, String role,
            IndexHeapCrashSupport.Scenario scenario, long seed, Path gate, boolean injectWrites,
            Path injector) throws IOException {
        return startWorker(cfg, datadir, workdir, role, scenario, seed, gate, injectWrites, injector, role);
    }

    private static Process startWorker(Config cfg, Path datadir, Path workdir, String role,
            IndexHeapCrashSupport.Scenario scenario, long seed, Path gate, boolean injectWrites,
            Path injector, String outStem) throws IOException {
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
        cmd.add("--trace=" + workdir.resolve(outStem + ".trace").toAbsolutePath());

        ProcessBuilder pb = new ProcessBuilder(cmd);
        pb.directory(cfg.repoRoot.toFile());
        pb.redirectErrorStream(true);
        pb.redirectOutput(workdir.resolve(outStem + ".out").toFile());
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
