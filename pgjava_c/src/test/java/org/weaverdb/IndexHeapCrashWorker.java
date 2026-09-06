/*-------------------------------------------------------------------------
 *
 * Child JVM for the Java index/heap crash harness.
 *
 * Roles:
 *   setup   — create DB + durable schema, wrapup, exit
 *   crash   — mutate until SIGKILL (no wrapup)
 *   recover — cold start of this process only; never wrapup or re-init.
 *             The parent starts a new JVM if this one dies at init.
 *
 *-------------------------------------------------------------------------
 */

package org.weaverdb;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Random;

public final class IndexHeapCrashWorker {

    private IndexHeapCrashWorker() {
    }

    public static void main(String[] args) {
        int code = 1;
        try {
            code = run(args);
        } catch (Throwable t) {
            if (IndexHeapCrashSupport.isBootstrapXid(t)) {
                System.out.println("IHC: BOOTSTRAP_XID");
                System.out.flush();
                code = IndexHeapCrashSupport.RecoverCode.BOOTSTRAP_XID.exit;
            } else {
                t.printStackTrace(System.err);
                System.err.flush();
                System.out.println("IHC: FAIL " + IndexHeapCrashSupport.message(t));
                System.out.flush();
                code = 1;
            }
            try {
                for (String a : args) {
                    if (a.startsWith("--trace=")) {
                        trace(Path.of(a.substring("--trace=".length())),
                                "throwable " + IndexHeapCrashSupport.message(t));
                    }
                }
            } catch (Exception ignored) {
            }
        } finally {
            System.exit(code);
        }
    }

    static int run(String[] args) throws Exception {
        if (args.length < 2) {
            System.err.println("usage: IndexHeapCrashWorker setup|crash|recover <datadir> [options]");
            return 3;
        }
        String role = args[0];
        Path datadir = Path.of(args[1]);
        IndexHeapCrashSupport.Scenario scenario = IndexHeapCrashSupport.Scenario.DML_TIMED;
        long seed = 1L;
        int startN = 12;
        int maxId = 24;
        int burst = 80;
        Path gate = null;
        Path trace = null;
        for (int i = 2; i < args.length; i++) {
            String a = args[i];
            int eq = a.indexOf('=');
            if (!a.startsWith("--") || eq < 0) {
                continue;
            }
            String key = a.substring(2, eq);
            String val = a.substring(eq + 1);
            switch (key) {
                case "scenario" -> scenario = IndexHeapCrashSupport.Scenario.parse(val);
                case "seed" -> seed = Long.parseLong(val);
                case "startN" -> startN = Integer.parseInt(val);
                case "maxId" -> maxId = Integer.parseInt(val);
                case "burst" -> burst = Integer.parseInt(val);
                case "gate" -> gate = Path.of(val);
                case "trace" -> trace = Path.of(val);
                default -> {
                }
            }
        }

        trace(trace, "start role=" + role + " datadir=" + datadir);
        IndexHeapCrashSupport.initEngine(datadir);
        trace(trace, "engine ready");
        Random rng = new Random(seed);

        switch (role) {
            case "setup" -> {
                IndexHeapCrashSupport.createDatabase();
                trace(trace, "database created");
                IndexHeapCrashSupport.setupSchema(scenario, startN);
                trace(trace, "schema ready");
                int n = IndexHeapCrashSupport.countRows();
                trace(trace, "setup rows=" + n);
                IndexHeapCrashSupport.shutdownEngine();
                trace(trace, "shutdown complete");
                System.out.println("IHC: SETUP_ROWS=" + n);
                System.out.println("IHC: SETUP_OK");
                System.out.flush();
                return n > 0 ? 0 : 1;
            }
            case "probe" -> {
                int n = IndexHeapCrashSupport.countRows();
                IndexHeapCrashSupport.shutdownEngine();
                System.out.println("IHC: SETUP_ROWS=" + n);
                if (n <= 0) {
                    System.out.println("IHC: FAIL setup not durable after wrapup");
                    return 1;
                }
                System.out.println("IHC: PROBE_OK");
                return 0;
            }
            case "crash" -> {
                int n = IndexHeapCrashSupport.countRows();
                System.out.println("IHC: SETUP_ROWS=" + n);
                System.out.flush();
                if (n <= 0) {
                    System.out.println("IHC: FAIL setup not durable after wrapup");
                    System.out.flush();
                    return 1;
                }
                IndexHeapCrashSupport.markReady(gate);
                IndexHeapCrashSupport.runCrashWorkload(scenario, rng, startN, maxId, burst);
                return 0;
            }
            case "recover" -> {
                IndexHeapCrashSupport.CheckResult r = IndexHeapCrashSupport.recoverAndCheck(maxId, rng);
                System.out.print(r.log);
                if (r.code == IndexHeapCrashSupport.RecoverCode.OK) {
                    System.out.println("IHC: CONSISTENT");
                } else if (r.code == IndexHeapCrashSupport.RecoverCode.BOOTSTRAP_XID) {
                    System.out.println("IHC: BOOTSTRAP_XID");
                } else {
                    System.out.println("IHC: FAIL");
                }
                System.out.flush();
                /* Discard this JVM. Do not wrapup or initialize() again. */
                return r.code.exit;
            }
            default -> {
                System.err.println("unknown role: " + role);
                return 3;
            }
        }
    }

    private static void trace(Path trace, String line) {
        if (trace == null) {
            return;
        }
        try {
            Files.writeString(trace, line + "\n",
                    java.nio.charset.StandardCharsets.UTF_8,
                    java.nio.file.StandardOpenOption.CREATE,
                    java.nio.file.StandardOpenOption.APPEND);
        } catch (Exception ignored) {
        }
    }
}
