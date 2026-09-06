/*-------------------------------------------------------------------------
 *
 * Shared helpers for the Java multiuser index/heap crash harness.
 *
 * Each child JVM starts Weaver once. Recover processes never wrapup
 * or initialize() a second time; the parent starts a new JVM instead.
 *
 *-------------------------------------------------------------------------
 */

package org.weaverdb;

import java.nio.file.Files;
import java.nio.file.Path;
import java.time.Duration;
import java.util.ArrayList;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Locale;
import java.util.Properties;
import java.util.Random;
import java.util.Set;
import java.util.TreeSet;

final class IndexHeapCrashSupport {

    static final String DB = "persistcrash";
    static final String TABLE = "ihc_t";
    static final String BTREE = "ihc_id_idx";
    static final String HNSW = "ihc_hnsw";
    static final String IVF = "ihc_ivf";

    enum Scenario {
        DML_TIMED,
        VACUUM_TIMED,
        MIXED_TIMED,
        BUILD_HNSW_TIMED,
        BUILD_BTREE_TIMED,
        DML_WRITES,
        VACUUM_WRITES,
        MIXED_WRITES;

        boolean usesWrites() {
            return name().endsWith("_WRITES");
        }

        String base() {
            String n = name().toLowerCase(Locale.ROOT);
            n = n.replace("_timed", "").replace("_writes", "");
            return n;
        }

        static Scenario parse(String raw) {
            return Scenario.valueOf(raw.trim().toUpperCase(Locale.ROOT));
        }

        /** Timed SIGKILL scenarios including vacuum / vector-index builds. */
        static Scenario[] timedOnly() {
            return new Scenario[] {
                DML_TIMED, VACUUM_TIMED, MIXED_TIMED, BUILD_HNSW_TIMED, BUILD_BTREE_TIMED
            };
        }

        static Scenario[] btreeSafeTimed() {
            return new Scenario[] { DML_TIMED, BUILD_BTREE_TIMED };
        }

        static Scenario[] btreeSafeAll() {
            return new Scenario[] { DML_TIMED, BUILD_BTREE_TIMED, DML_WRITES };
        }

        static Scenario[] all() {
            return values();
        }
    }

    enum RecoverCode {
        OK(0),
        FAIL(1),
        BOOTSTRAP_XID(2);

        final int exit;

        RecoverCode(int exit) {
            this.exit = exit;
        }
    }

    static final class CheckResult {
        final RecoverCode code;
        final String log;

        CheckResult(RecoverCode code, String log) {
            this.code = code;
            this.log = log;
        }
    }

    private IndexHeapCrashSupport() {
    }

    static String vec(int id) {
        return "[" + id + ",0,0]";
    }

    static void initEngine(Path datadir) {
        Properties p = new Properties();
        p.setProperty("datadir", datadir.toAbsolutePath().toString());
        p.setProperty("allow_anonymous", "true");
        p.setProperty("disable_crc", "TRUE");
        p.setProperty("transcareful", "false");
        p.setProperty("stdlog", System.getProperty("weaver.crash.stdlog", "FALSE"));
        p.setProperty("sortmem", System.getProperty("weaver.sortmem", "131072"));
        WeaverInitializer.initialize(p);
    }

    static void shutdownEngine() {
        try {
            WeaverInitializer.shutdown(Duration.ofSeconds(30));
        } catch (Exception e) {
            WeaverInitializer.forceShutdown();
        }
    }

    static void markReady(Path gate) throws Exception {
        if (gate != null) {
            Files.createDirectories(gate.getParent());
            Files.writeString(gate, "ready\n");
        }
        System.err.println("IndexHeapCrashWorker: ready");
        System.err.flush();
        System.out.println("IHC: READY");
        System.out.flush();
    }

    static boolean isBootstrapXid(Throwable t) {
        String m = message(t);
        return containsIgnoreCase(m, "this should not be happening")
                || containsIgnoreCase(m, "SYSTEM HALT");
    }

    static boolean isDuplicateKey(Throwable t) {
        String m = message(t);
        return containsIgnoreCase(m, "duplicate")
                || containsIgnoreCase(m, "cannot insert")
                || containsIgnoreCase(m, "already exists");
    }

    static String message(Throwable t) {
        if (t == null) {
            return "";
        }
        StringBuilder sb = new StringBuilder();
        for (Throwable c = t; c != null; c = c.getCause()) {
            if (c.getMessage() != null) {
                if (sb.length() > 0) {
                    sb.append(" | ");
                }
                sb.append(c.getMessage());
            }
        }
        return sb.length() == 0 ? t.toString() : sb.toString();
    }

    static boolean containsIgnoreCase(String hay, String needle) {
        return hay != null && hay.toLowerCase(Locale.ROOT).contains(needle.toLowerCase(Locale.ROOT));
    }

    static void createDatabase() throws ExecutionException {
        try (DBReference conn = DBReferenceManager.connect("template1")) {
            try {
                conn.execute("create database " + DB);
            } catch (ExecutionException e) {
                if (!containsIgnoreCase(message(e), "already exists")) {
                    throw e;
                }
            }
        }
    }

    static void setupSchema(Scenario scenario, int startN) throws ExecutionException {
        try (DBReference conn = DBReferenceManager.connect(DB)) {
            String base = scenario.base();
            conn.execute("create table " + TABLE + " (id int4, val varchar(64), emb vector)");
            if (!"build_btree".equals(base)) {
                conn.execute("create unique index " + BTREE + " on " + TABLE + " (id)");
            }
            insertRange(conn, 1, startN);
            if ("vacuum".equals(base) || "mixed".equals(base)) {
                conn.execute("create index " + HNSW + " on " + TABLE
                        + " using hnsw (emb vector_l2_ops) with (m = 8, ef_construction = 32)");
                conn.execute("create index " + IVF + " on " + TABLE
                        + " using ivfflat (emb vector_l2_ops) with (lists = 2)");
            }
        }
    }

    static int countRows() throws ExecutionException {
        try (DBReference conn = DBReferenceManager.connect(DB)) {
            conn.execute("set enable_indexscan = off");
            conn.execute("set enable_seqscan = on");
            return queryHeapSeqscan(conn).size();
        }
    }

    static void insertRange(DBReference conn, int from, int to) throws ExecutionException {
        for (int i = from; i <= to; i++) {
            conn.execute("insert into " + TABLE + " values (" + i + ", 'v" + i + "', '" + vec(i) + "')");
        }
    }

    /**
     * Mutating work that should still be in flight when the parent SIGKILLs.
     * Never returns: extra churn keeps page writers busy until the kill.
     */
    static void runCrashWorkload(Scenario scenario, Random rng, int startN, int maxId, int burst)
            throws ExecutionException {
        try (DBReference conn = DBReferenceManager.connect(DB)) {
            switch (scenario.base()) {
                case "build_btree" -> {
                    conn.execute("create unique index " + BTREE + " on " + TABLE + " (id)");
                    insertRangeIgnoreDup(conn, startN + 1, maxId);
                }
                case "build_hnsw" -> {
                    conn.execute("create index " + HNSW + " on " + TABLE
                            + " using hnsw (emb vector_l2_ops) with (m = 8, ef_construction = 32)");
                    conn.execute("create index " + IVF + " on " + TABLE
                            + " using ivfflat (emb vector_l2_ops) with (lists = 2)");
                }
                case "vacuum" -> {
                    for (int i = 1; i <= startN; i += 2) {
                        execIgnore(conn, "delete from " + TABLE + " where id = " + i);
                    }
                    execIgnore(conn, "vacuum " + TABLE);
                    execIgnore(conn, "vacuum " + TABLE);
                }
                case "mixed" -> runMixed(conn, rng, maxId, burst);
                default -> {
                    insertRangeIgnoreDup(conn, startN + 1, maxId);
                    for (int i = 1; i <= startN; i += 3) {
                        execIgnore(conn, "update " + TABLE + " set emb = '[0," + i + ",0]' where id = " + i);
                    }
                    for (int i = 2; i <= startN; i += 4) {
                        execIgnore(conn, "delete from " + TABLE + " where id = " + i);
                    }
                    execIgnore(conn, "insert into " + TABLE + " values (2, 'v2', '" + vec(2) + "')");
                }
            }
            while (true) {
                churnOnce(conn, rng, maxId);
            }
        }
    }

    static CheckResult recoverAndCheck(int maxId, Random rng) {
        StringBuilder log = new StringBuilder();
        try (DBReference conn = DBReferenceManager.connect(DB)) {
            try {
                System.out.println("IHC: recover vacuum");
                System.out.flush();
                conn.execute("vacuum " + TABLE);
                System.out.println("IHC: recover vacuum done");
                System.out.flush();
            } catch (ExecutionException e) {
                if (isBootstrapXid(e)) {
                    return new CheckResult(RecoverCode.BOOTSTRAP_XID,
                            "recover missing bootstrap xid (initdb-durable; retry round)\n");
                }
                return fail(log, e, "post-crash vacuum failed");
            }
            log.append("  vacuum + recover\n");

            List<Integer> heap;
            try {
                conn.execute("set enable_indexscan = off");
                conn.execute("set enable_seqscan = on");
                heap = queryHeapSeqscan(conn);
            } catch (ExecutionException e) {
                if (isBootstrapXid(e)) {
                    return new CheckResult(RecoverCode.BOOTSTRAP_XID,
                            "recover missing bootstrap xid (initdb-durable; retry round)\n");
                }
                return fail(log, e, "heap seqscan failed");
            }

            log.append("  heap ids: ").append(heap.isEmpty() ? "<empty>" : heap).append('\n');

            if (heap.isEmpty()) {
                return fail(log, null,
                        "heap empty after crash; wrapup-durable seed rows should survive mid-write kill");
            }

            Set<Integer> heapSet = new LinkedHashSet<>();
            List<Integer> dups = new ArrayList<>();
            for (Integer id : heap) {
                if (!heapSet.add(id)) {
                    dups.add(id);
                }
            }
            if (!dups.isEmpty()) {
                return fail(log, null, "duplicate heap ids after crash/recover: " + dups);
            }

            try {
                if (relationExists(conn, BTREE)) {
                    CheckResult btree = checkBtreePointsAtHeap(conn, heap, heapSet, maxId, rng, log);
                    if (btree != null) {
                        return btree;
                    }
                } else {
                    log.append("  unique index absent after crash (ok if crash was mid-build)\n");
                }

                if (relationExists(conn, HNSW) || relationExists(conn, IVF)) {
                    CheckResult ann = checkAnnSubsetOfHeap(conn, heapSet, log);
                    if (ann != null) {
                        return ann;
                    }
                }

                conn.execute("vacuum " + TABLE);
                log.append("  OK second vacuum\n");
            } catch (ExecutionException e) {
                if (isBootstrapXid(e)) {
                    return new CheckResult(RecoverCode.BOOTSTRAP_XID,
                            "recover missing bootstrap xid (initdb-durable; retry round)\n");
                }
                return fail(log, e, "post-crash consistency check failed");
            }
            return new CheckResult(RecoverCode.OK, log.toString());
        } catch (ExecutionException e) {
            if (isBootstrapXid(e)) {
                return new CheckResult(RecoverCode.BOOTSTRAP_XID,
                        "recover missing bootstrap xid (initdb-durable; retry round)\n");
            }
            return fail(log, e, "cannot connect after crash");
        }
    }

    private static CheckResult checkBtreePointsAtHeap(DBReference conn, List<Integer> heap,
            Set<Integer> heapSet, int maxId, Random rng, StringBuilder log) throws ExecutionException {
        conn.execute("set enable_seqscan = off");
        conn.execute("set enable_indexscan = on");
        List<Integer> idxIds = queryIds(conn);
        idxIds = new ArrayList<>(idxIds);
        idxIds.sort(Integer::compareTo);
        if (!heap.equals(idxIds)) {
            log.append("  heap: ").append(heap).append('\n');
            log.append("  idx:  ").append(idxIds).append('\n');
            return fail(log, null, "unique btree scan disagrees with heap seqscan");
        }
        log.append("  OK btree ids match heap\n");

        for (int id : heap) {
            try (Statement s = conn.statement("select id from " + TABLE + " where id = " + id)) {
                Output<Integer> out = s.linkOutput(1, Integer.class);
                s.execute();
                if (!s.fetch()) {
                    return fail(log, null, "index lookup missed live heap id=" + id
                            + " (stale or missing index pointer)");
                }
                int got = out.get();
                if (got != id) {
                    return fail(log, null, "index pointer for id=" + id
                            + " resolved to a different heap item id=" + got);
                }
                if (s.fetch()) {
                    return fail(log, null, "duplicate index entries for id=" + id);
                }
            }
        }
        log.append("  OK every live key's index TID points at that heap row\n");

        TreeSet<Integer> missing = new TreeSet<>();
        for (int id = 1; id <= maxId; id++) {
            if (!heapSet.contains(id)) {
                missing.add(id);
            }
        }

        if (!heapSet.isEmpty()) {
            int probe = pick(heapSet, rng);
            CheckResult dup = withFreshConnection(c -> {
                try {
                    c.execute("insert into " + TABLE + " values (" + probe + ", 'dup', '" + vec(probe) + "')");
                    return fail(log, null, "unique index accepted duplicate id=" + probe);
                } catch (ExecutionException e) {
                    if (isBootstrapXid(e)) {
                        return new CheckResult(RecoverCode.BOOTSTRAP_XID, log.toString());
                    }
                    if (!isDuplicateKey(e)) {
                        return fail(log, e, "unique reject of id=" + probe + " failed with unexpected error");
                    }
                }
                return null;
            });
            if (dup != null) {
                return dup;
            }
            log.append("  OK unique reject id=").append(probe).append('\n');
        }

        if (!missing.isEmpty()) {
            int probe = pick(missing, rng);
            CheckResult ins = withFreshConnection(c -> {
                try {
                    c.execute("insert into " + TABLE + " values (" + probe + ", 're', '" + vec(probe) + "')");
                } catch (ExecutionException e) {
                    if (isBootstrapXid(e)) {
                        return new CheckResult(RecoverCode.BOOTSTRAP_XID, log.toString());
                    }
                    return fail(log, e, "stale unique TID blocked insert of absent id=" + probe);
                }
                c.execute("delete from " + TABLE + " where id = " + probe);
                return null;
            });
            if (ins != null) {
                return ins;
            }
            log.append("  OK reinsert absent id=").append(probe).append('\n');
        }
        return null;
    }

    @FunctionalInterface
    private interface ConnCheck {
        CheckResult run(DBReference conn) throws ExecutionException;
    }

    private static CheckResult withFreshConnection(ConnCheck body) throws ExecutionException {
        try (DBReference c = DBReferenceManager.connect(DB)) {
            return body.run(c);
        }
    }

    private static CheckResult checkAnnSubsetOfHeap(DBReference conn, Set<Integer> heapSet,
            StringBuilder log) throws ExecutionException {
        conn.execute("set enable_seqscan = off");
        conn.execute("set ivfflat.probes = 2");
        List<Integer> ann = new ArrayList<>();
        try (Statement s = conn.statement(
                "select id from " + TABLE + " order by emb <-> '[1,0,0]' limit 8")) {
            Output<Integer> id = s.linkOutput(1, Integer.class);
            s.execute();
            while (s.fetch()) {
                ann.add(id.get());
            }
        }
        for (Integer id : ann) {
            if (!heapSet.contains(id)) {
                return fail(log, null, "ANN returned id=" + id + " not in heap " + heapSet);
            }
        }
        log.append("  OK ANN ids subset of heap ").append(ann).append('\n');
        return null;
    }

    static List<Integer> queryIds(DBReference conn) throws ExecutionException {
        List<Integer> ids = new ArrayList<>();
        try (Statement s = conn.statement("select id from " + TABLE + " order by id")) {
            Output<Integer> id = s.linkOutput(1, Integer.class);
            s.execute();
            while (s.fetch()) {
                ids.add(id.get());
            }
        }
        return ids;
    }

    /**
     * Heap read that does not ORDER BY the unique key, so a torn btree cannot
     * masquerade as an empty seqscan.
     */
    static List<Integer> queryHeapSeqscan(DBReference conn) throws ExecutionException {
        List<Integer> ids = new ArrayList<>();
        try (Statement s = conn.statement("select id from " + TABLE)) {
            Output<Integer> id = s.linkOutput(1, Integer.class);
            s.execute();
            while (s.fetch()) {
                ids.add(id.get());
            }
        }
        ids.sort(Integer::compareTo);
        return ids;
    }

    static boolean relationExists(DBReference conn, String name) throws ExecutionException {
        try (Statement s = conn.statement(
                "select relname from pg_class where relname = '" + name + "'")) {
            s.linkOutput(1, String.class);
            s.execute();
            return s.fetch();
        }
    }

    private static void insertRangeIgnoreDup(DBReference conn, int from, int to) {
        for (int i = from; i <= to; i++) {
            execIgnore(conn, "insert into " + TABLE + " values (" + i + ", 'v" + i + "', '" + vec(i) + "')");
        }
    }

    private static void runMixed(DBReference conn, Random rng, int maxId, int burst) {
        Set<Integer> live = new TreeSet<>();
        try {
            conn.execute("set enable_indexscan = off");
            conn.execute("set enable_seqscan = on");
            live.addAll(queryIds(conn));
        } catch (ExecutionException e) {
            /* start from empty live set and let later ops fail-open */
        }
        for (int i = 0; i < burst; i++) {
            mixedOnce(conn, rng, maxId, live);
        }
    }

    private static void mixedOnce(DBReference conn, Random rng, int maxId, Set<Integer> live) {
        int op = rng.nextInt(4);
        if (live.size() <= 2) {
            op = 0;
        } else if (live.size() >= maxId) {
            op = 1 + rng.nextInt(3);
        }
        switch (op) {
            case 0 -> {
                List<Integer> absent = new ArrayList<>();
                for (int id = 1; id <= maxId; id++) {
                    if (!live.contains(id)) {
                        absent.add(id);
                    }
                }
                if (absent.isEmpty()) {
                    return;
                }
                int id = absent.get(rng.nextInt(absent.size()));
                if (execIgnore(conn, "insert into " + TABLE + " values (" + id + ", 'v" + id + "', '"
                        + vec(id) + "')")) {
                    live.add(id);
                }
            }
            case 1 -> {
                if (live.isEmpty()) {
                    return;
                }
                int id = pick(live, rng);
                if (execIgnore(conn, "delete from " + TABLE + " where id = " + id)) {
                    live.remove(id);
                }
            }
            case 2 -> {
                if (live.isEmpty()) {
                    return;
                }
                int id = pick(live, rng);
                execIgnore(conn, "update " + TABLE + " set val = 'u" + id + "', emb = '[0," + id
                        + ",0]' where id = " + id);
            }
            default -> execIgnore(conn, "vacuum " + TABLE);
        }
    }

    private static void churnOnce(DBReference conn, Random rng, int maxId) {
        int id = 1 + rng.nextInt(maxId);
        /* Do not VACUUM in the generic churn loop. A mid-VACUUM SIGKILL
         * plus recover vacuum can remove wrapup-durable seed rows; vacuum
         * crash coverage lives in the vacuum/mixed scenarios. */
        if (rng.nextBoolean()) {
            execIgnore(conn, "delete from " + TABLE + " where id = " + id);
            execIgnore(conn, "insert into " + TABLE + " values (" + id + ", 'c', '" + vec(id) + "')");
        } else {
            execIgnore(conn, "update " + TABLE + " set val = 'c' where id = " + id);
        }
    }

    private static boolean execIgnore(DBReference conn, String sql) {
        try {
            conn.execute(sql);
            return true;
        } catch (ExecutionException e) {
            return false;
        }
    }

    private static int pick(Set<Integer> ids, Random rng) {
        int n = rng.nextInt(ids.size());
        int i = 0;
        for (int id : ids) {
            if (i++ == n) {
                return id;
            }
        }
        throw new IllegalStateException("empty set");
    }

    private static CheckResult fail(StringBuilder log, Throwable t, String why) {
        log.append("FAIL: ").append(why);
        if (t != null) {
            log.append(": ").append(message(t));
        }
        log.append('\n');
        return new CheckResult(RecoverCode.FAIL, log.toString());
    }
}
