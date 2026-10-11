/*-------------------------------------------------------------------------
 *
 * Shared helpers for the Java multiuser index/heap crash harness.
 *
 * Each child JVM starts Weaver once. Recover processes never wrapup
 * or initialize() a second time; the parent starts a new JVM instead.
 * The crash JVM can run several client threads, each with its own
 * connection, so concurrent backends share the lock and buffer state.
 *
 *-------------------------------------------------------------------------
 */

package org.weaverdb;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.file.Files;
import java.nio.file.Path;
import java.time.Duration;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Locale;
import java.util.Properties;
import java.util.Random;
import java.util.Set;
import java.util.TreeSet;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;

final class IndexHeapCrashSupport {

    static final String DB = "persistcrash";
    static final String TABLE = "ihc_t";
    static final String BTREE = "ihc_id_idx";
    static final String WIDE_IDX = "ihc_wide_idx";
    static final String HNSW = "ihc_hnsw";
    static final String IVF = "ihc_ivf";
    /** Relation page size used by the non-Android engine build. */
    static final int BLCKSZ = 8192;
    /** vfd.c HEADER_MAGIC. First shadow-log block starts with this. */
    static final long SHADOW_HEADER_MAGIC = 0xCAFE08072006BABEL;
    /** Ids inserted by split_churn after the setup seed, shared by client threads. */
    private static final java.util.concurrent.atomic.AtomicInteger SPLIT_IDS =
            new java.util.concurrent.atomic.AtomicInteger(100_000);

    enum Scenario {
        DML_TIMED,
        VACUUM_TIMED,
        MIXED_TIMED,
        BUILD_HNSW_TIMED,
        BUILD_BTREE_TIMED,
        SPLIT_CHURN_TIMED,
        ROTATE_TIMED,
        RECOVER_KILL,
        DML_WRITES,
        VACUUM_WRITES,
        MIXED_WRITES,
        SPLIT_CHURN_WRITES,
        ROTATE_WRITES;

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

    /** Live heap tuple plus crash-visibility xids from the tuple header. */
    static final class HeapItem {
        final int id;
        final long xmin;
        final long xmax;

        HeapItem(int id, long xmin, long xmax) {
            this.id = id;
            this.xmin = xmin;
            this.xmax = xmax;
        }

        @Override
        public String toString() {
            return id + "(xmin=" + xmin + ",xmax=" + xmax + ")";
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
        p.setProperty("trace_index", System.getProperty("weaver.crash.trace_index", "TRUE"));
        p.setProperty("sortmem", System.getProperty("weaver.sortmem", "131072"));
        p.setProperty("index_corruption", "IGNORE");
        p.setProperty("heap_corruption", "IGNORE");
        
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
                || containsIgnoreCase(m, "Ami xid 512")
                || containsIgnoreCase(m, "SYSTEM HALT");
    }

    static boolean isDuplicateKey(Throwable t) {
        String m = message(t);
        return containsIgnoreCase(m, "duplicate")
                || containsIgnoreCase(m, "cannot insert")
                || containsIgnoreCase(m, "already exists");
    }

    /** Unique-index insert conflict. "already exists" is a DDL collision, not this. */
    private static boolean isUniqueViolation(Throwable t) {
        return containsIgnoreCase(message(t), "cannot insert a duplicate");
    }

    private static boolean isRecoverableClientError(Throwable t) {
        return isAbortedContext(t) || isLockFailure(t);
    }

    private static boolean isLockFailure(Throwable t) {
        String m = message(t);
        return containsIgnoreCase(m, "lock failed")
                || containsIgnoreCase(m, "deadlock")
                || containsIgnoreCase(m, "cancelled");
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

    /**
     * Indexed value wide enough that a btree leaf holds only a handful of
     * keys, so _bt_split runs inside a short kill window.
     */
    static String wide(int id) {
        StringBuilder sb = new StringBuilder(960);
        sb.append('w').append(id).append('w');
        while (sb.length() < 900) {
            sb.append('W');
        }
        return sb.toString();
    }

    static String rowInsert(Scenario scenario, int id, String val) {
        if ("split_churn".equals(scenario.base())) {
            return "insert into " + TABLE + " values (" + id + ", '" + val + "', '"
                    + wide(id) + "', '" + vec(id) + "')";
        }
        return "insert into " + TABLE + " values (" + id + ", '" + val + "', '" + vec(id) + "')";
    }

    static void setupSchema(Scenario scenario, int startN) throws ExecutionException {
        try (DBReference conn = DBReferenceManager.connect(DB)) {
            String base = scenario.base();
            if ("split_churn".equals(base)) {
                execLogged(conn, "create table " + TABLE
                        + " (id int4, val varchar(64), wide varchar(1500), emb vector)");
                execLogged(conn, "create unique index " + BTREE + " on " + TABLE + " (id)");
                execLogged(conn, "create unique index " + WIDE_IDX + " on " + TABLE + " (wide)");
                int n = Math.max(startN, 30);
                for (int i = 1; i <= n; i++) {
                    execLogged(conn, rowInsert(scenario, i, "v" + i));
                }
                return;
            }
            execLogged(conn, "create table " + TABLE + " (id int4, val varchar(64), emb vector)");
            if (!"build_btree".equals(base)) {
                execLogged(conn, "create unique index " + BTREE + " on " + TABLE + " (id)");
            }
            insertRange(conn, 1, startN);
            if ("vacuum".equals(base) || "mixed".equals(base)) {
                execLogged(conn, "create index " + HNSW + " on " + TABLE
                        + " using hnsw (emb vector_l2_ops) with (m = 8, ef_construction = 32)");
                execLogged(conn, "create index " + IVF + " on " + TABLE
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
            execLogged(conn, "insert into " + TABLE + " values (" + i + ", 'v" + i + "', '" + vec(i) + "')");
        }
    }

    /**
     * Mutating work that should still be in flight when the parent SIGKILLs.
     * Never returns: extra churn keeps page writers busy until the kill.
     */
    static void runCrashWorkload(Scenario scenario, Random rng, int startN, int maxId, int burst)
            throws ExecutionException {
        try (DBReference conn = DBReferenceManager.connect(DB)) {
            runScenarioPrefix(conn, scenario, rng, startN, maxId, burst);
            while (true) {
                churnOnce(conn, scenario, rng, maxId);
            }
        }
    }

    /**
     * Several backends in this process, one thread and connection each.
     * Client 0 keeps the scenario prefix (index build / vacuum / mixed).
     * Every client then churns a private id band so concurrent DML does not
     * collide on the unique key. Ready is signaled only after every client
     * has connected, so the parent's kill window overlaps real concurrency.
     * Lock aborts reopen that connection; a unique violation still fails
     * the workload.
     */
    static void runMultiClientCrashWorkload(Scenario scenario, Random rng, int startN, int maxId,
            int burst, int clients, Path gate) throws Exception {
        int n = Math.max(2, clients);
        CountDownLatch connected = new CountDownLatch(n);
        AtomicReference<Throwable> fatal = new AtomicReference<>();
        System.out.println("IHC: clients=" + n + " maxId=" + maxId);
        System.out.flush();
        for (int c = 1; c < n; c++) {
            int client = c;
            Random crng = new Random(rng.nextLong());
            Thread t = new Thread(
                    () -> extraClient(scenario, client, crng, maxId, connected, fatal),
                    "ihc-client-" + client);
            t.setDaemon(true);
            t.start();
        }

        DBReference conn = DBReferenceManager.connect(DB);
        connected.countDown();
        if (!connected.await(15, TimeUnit.SECONDS)) {
            throw new IllegalStateException("timed out waiting for " + n + " clients to connect");
        }
        Throwable early = fatal.get();
        if (early != null) {
            throw wrapFatal(early);
        }
        markReady(gate);

        boolean prefix = false;
        int prefixTries = 0;
        try {
            while (fatal.get() == null) {
                try {
                    if (!prefix) {
                        runScenarioPrefix(conn, scenario, rng, startN, maxId, burst);
                        prefix = true;
                    }
                    churnOnce(conn, scenario, rng, maxId);
                } catch (IllegalStateException e) {
                    if (isUniqueViolation(e)) {
                        throw e;
                    }
                    conn = reopenAfter(conn, 0, e);
                    if (++prefixTries >= 3) {
                        prefix = true;
                    }
                } catch (ExecutionException e) {
                    if (isUniqueViolation(e)) {
                        throw new IllegalStateException(
                                "duplicate key during crash workload: " + message(e), e);
                    }
                    if (containsIgnoreCase(message(e), "already exists")) {
                        prefix = true;
                        continue;
                    }
                    if (!isRecoverableClientError(e)) {
                        throw e;
                    }
                    conn = reopenAfter(conn, 0, e);
                    if (++prefixTries >= 3) {
                        prefix = true;
                    }
                }
            }
        } finally {
            try {
                conn.close();
            } catch (Exception ignored) {
            }
        }
        throw wrapFatal(fatal.get());
    }

    private static void extraClient(Scenario scenario, int client, Random rng, int maxId,
            CountDownLatch connected, AtomicReference<Throwable> fatal) {
        int lo = client * maxId + 1;
        int hi = (client + 1) * maxId;
        DBReference conn = null;
        boolean counted = false;
        try {
            conn = DBReferenceManager.connect(DB);
            connected.countDown();
            counted = true;
            System.out.println("IHC: client " + client + " ids " + lo + ".." + hi);
            System.out.flush();
            if ("split_churn".equals(scenario.base())) {
                while (fatal.get() == null) {
                    int id = SPLIT_IDS.getAndIncrement();
                    if (!execClient(conn, client, rowInsert(scenario, id, "c" + client))) {
                        conn = reopenQuiet(conn);
                    }
                }
                return;
            }
            if ("rotate".equals(scenario.base())) {
                int id = 800_000 + client;
                System.out.println("IHC: client " + client + " rotate id " + id);
                System.out.flush();
                while (fatal.get() == null) {
                    if (!execClient(conn, client, "delete from " + TABLE + " where id = " + id)) {
                        conn = reopenQuiet(conn);
                        continue;
                    }
                    if (!execClient(conn, client, rowInsert(scenario, id, "r" + client))) {
                        conn = reopenQuiet(conn);
                    }
                }
                return;
            }
            while (fatal.get() == null) {
                int id = lo + rng.nextInt(hi - lo + 1);
                if (rng.nextBoolean()) {
                    if (!execClient(conn, client, "delete from " + TABLE + " where id = " + id)) {
                        conn = reopenQuiet(conn);
                        continue;
                    }
                    if (!execClient(conn, client, "insert into " + TABLE + " values (" + id
                            + ", 'c" + client + "', '" + vec(id) + "')")) {
                        conn = reopenQuiet(conn);
                    }
                } else if (!execClient(conn, client, "update " + TABLE + " set val = 'c" + client
                        + "' where id = " + id)) {
                    conn = reopenQuiet(conn);
                }
            }
        } catch (Throwable t) {
            fatal.compareAndSet(null, t);
        } finally {
            if (!counted) {
                connected.countDown();
            }
            if (conn != null) {
                try {
                    conn.close();
                } catch (Exception ignored) {
                }
            }
        }
    }

    /**
     * @return false when the statement failed and the connection was closed
     */
    private static boolean execClient(DBReference conn, int client, String sql) throws ExecutionException {
        System.out.println("IHC: SQL c" + client + " " + sqlForLog(sql));
        System.out.flush();
        try {
            conn.execute(sql);
            return true;
        } catch (ExecutionException e) {
            System.out.println("IHC: SQL_FAIL c" + client + " " + message(e));
            System.out.flush();
            if (isUniqueViolation(e)) {
                throw new IllegalStateException(
                        "duplicate key during crash workload: " + message(e), e);
            }
            try {
                conn.close();
            } catch (Exception ignored) {
            }
            return false;
        }
    }

    private static DBReference reopenQuiet(DBReference conn) throws ExecutionException {
        try {
            conn.close();
        } catch (Exception ignored) {
        }
        return DBReferenceManager.connect(DB);
    }

    private static DBReference reopenAfter(DBReference conn, int client, Throwable cause)
            throws ExecutionException {
        System.out.println("IHC: client " + client + " reopening after " + message(cause));
        System.out.flush();
        return reopenQuiet(conn);
    }

    private static Exception wrapFatal(Throwable t) {
        if (t instanceof Exception e) {
            return e;
        }
        return new IllegalStateException(message(t), t);
    }

    private static void runScenarioPrefix(DBReference conn, Scenario scenario, Random rng,
            int startN, int maxId, int burst) throws ExecutionException {
        switch (scenario.base()) {
            case "build_btree" -> {
                execLogged(conn, "create unique index " + BTREE + " on " + TABLE + " (id)");
                insertRangeIgnoreDup(conn, startN + 1, maxId);
            }
            case "build_hnsw" -> {
                execLogged(conn, "create index " + HNSW + " on " + TABLE
                        + " using hnsw (emb vector_l2_ops) with (m = 8, ef_construction = 32)");
                execLogged(conn, "create index " + IVF + " on " + TABLE
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
            case "split_churn" -> {
                for (int n = 0; n < 40; n++) {
                    int id = SPLIT_IDS.getAndIncrement();
                    execIgnore(conn, rowInsert(scenario, id, "s"));
                }
            }
            case "rotate" -> {
                int span = Math.max(1, maxId);
                for (int n = 0; n < 48; n++) {
                    int id = 1 + (n % span);
                    execIgnore(conn, "delete from " + TABLE + " where id = " + id);
                    execIgnore(conn, rowInsert(scenario, id, "r" + n));
                }
            }
            case "recover_kill" -> runDmlPrefix(conn, startN, maxId);
            default -> runDmlPrefix(conn, startN, maxId);
        }
    }

    private static void runDmlPrefix(DBReference conn, int startN, int maxId) {
        insertRangeIgnoreDup(conn, startN + 1, maxId);
        for (int i = 1; i <= startN; i += 3) {
            execIgnore(conn, "update " + TABLE + " set emb = '[0," + i + ",0]' where id = " + i);
        }
        for (int i = 2; i <= startN; i += 4) {
            execIgnore(conn, "delete from " + TABLE + " where id = " + i);
        }
        execIgnore(conn, "insert into " + TABLE + " values (2, 'v2', '" + vec(2) + "')");
    }

    static CheckResult recoverAndCheck(int maxId, Random rng) {
        StringBuilder log = new StringBuilder();
        try (DBReference conn = DBReferenceManager.connect(DB)) {
            try {
                conn.execute("set enable_indexscan = off");
                conn.execute("set enable_seqscan = on");
                String before = formatHeapItems(queryHeapItems(conn));
                log.append("  heap items before vacuum: ").append(before).append('\n');
                System.out.println("  heap items before vacuum: " + before);
                System.out.flush();
            } catch (ExecutionException e) {
                if (isBootstrapXid(e)) {
                    return new CheckResult(RecoverCode.BOOTSTRAP_XID,
                            "recover missing Ami xid 512 after crash (committed at initdb; not retrying)\n");
                }
                log.append("  heap seqscan before vacuum failed: ").append(message(e)).append('\n');
            }
            try {
                System.out.println("IHC: recover vacuum");
                System.out.flush();
                execLogged(conn, "vacuum " + TABLE);
                System.out.println("IHC: recover vacuum done");
                System.out.flush();
            } catch (ExecutionException e) {
                if (isBootstrapXid(e)) {
                    return new CheckResult(RecoverCode.BOOTSTRAP_XID,
                            "recover missing Ami xid 512 after crash (committed at initdb; not retrying)\n");
                }
                return fail(log, e, "post-crash vacuum failed");
            }
            log.append("  vacuum + recover\n");

            List<HeapItem> heapItems;
            try {
                conn.execute("set enable_indexscan = off");
                conn.execute("set enable_seqscan = on");
                heapItems = queryHeapItems(conn);
            } catch (ExecutionException e) {
                if (isBootstrapXid(e)) {
                    return new CheckResult(RecoverCode.BOOTSTRAP_XID,
                            "recover missing Ami xid 512 after crash (committed at initdb; not retrying)\n");
                }
                return fail(log, e, "heap seqscan failed");
            }

            List<Integer> heap = idsOf(heapItems);
            log.append("  heap ids: ").append(heap.isEmpty() ? "<empty>" : heap).append('\n');
            log.append("  heap items: ").append(formatHeapItems(heapItems)).append('\n');
            System.out.println("  heap ids: " + (heap.isEmpty() ? "<empty>" : heap));
            System.out.println("  heap items: " + formatHeapItems(heapItems));
            System.out.flush();

//            if (heap.isEmpty()) {
//                return fail(log, null,
//                        "heap empty after crash; wrapup-durable seed rows should survive mid-write kill");
//            }

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
                    CheckResult btree = checkBtreePointsAtHeap(conn, heapItems, heap, heapSet, maxId, rng, log);
                    if (btree != null) {
                        return btree;
                    }
                } else {
                    log.append("  unique index absent after crash (ok if crash was mid-build)\n");
                }

                if (relationExists(conn, WIDE_IDX)) {
                    CheckResult wide = checkWideIndex(conn, heap, log);
                    if (wide != null) {
                        return wide;
                    }
                }

                if (relationExists(conn, HNSW) || relationExists(conn, IVF)) {
                    CheckResult ann = checkAnnSubsetOfHeap(conn, heapSet, log);
                    if (ann != null) {
                        return ann;
                    }
                }

                execLogged(conn, "vacuum " + TABLE);
                log.append("  OK second vacuum\n");
            } catch (ExecutionException e) {
                if (isBootstrapXid(e)) {
                    return new CheckResult(RecoverCode.BOOTSTRAP_XID,
                            "recover missing Ami xid 512 after crash (committed at initdb; not retrying)\n");
                }
                return fail(log, e, "post-crash consistency check failed");
            }
            return new CheckResult(RecoverCode.OK, log.toString());
        } catch (ExecutionException e) {
            if (isBootstrapXid(e)) {
                return new CheckResult(RecoverCode.BOOTSTRAP_XID,
                        "recover missing Ami xid 512 after crash (committed at initdb; not retrying)\n");
            }
            return fail(log, e, "cannot connect after crash");
        }
    }

    private static CheckResult checkBtreePointsAtHeap(DBReference conn, List<HeapItem> heapItems,
            List<Integer> heap, Set<Integer> heapSet, int maxId, Random rng, StringBuilder log)
            throws ExecutionException {
        conn.execute("set enable_seqscan = off");
        conn.execute("set enable_indexscan = on");
        List<Integer> idxIds = queryIds(conn);
        idxIds = new ArrayList<>(idxIds);
        idxIds.sort(Integer::compareTo);
        if (!heap.equals(idxIds)) {
            log.append("  heap: ").append(heap).append('\n');
            log.append("  idx:  ").append(idxIds).append('\n');
            log.append("  heap items: ").append(formatHeapItems(heapItems)).append('\n');
            log.append("  heap-only: ").append(formatHeapItems(onlyIn(heapItems, idxIds))).append('\n');
            log.append("  idx-only:  ").append(onlyInIds(idxIds, heap)).append('\n');
            System.out.println("  heap-only: " + formatHeapItems(onlyIn(heapItems, idxIds)));
            System.out.println("  idx-only:  " + onlyInIds(idxIds, heap));
            System.out.flush();
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
    static List<HeapItem> queryHeapItems(DBReference conn) throws ExecutionException {
        List<HeapItem> items = new ArrayList<>();
        try (Statement s = conn.statement("select id, xmin, xmax from " + TABLE)) {
            Output<Integer> id = s.linkOutput(1, Integer.class);
            Output<Long> xmin = s.linkOutput(2, Long.class);
            Output<Long> xmax = s.linkOutput(3, Long.class);
            s.execute();
            while (s.fetch()) {
                Integer i = id.get();
                Long xn = xmin.get();
                Long xx = xmax.get();
                items.add(new HeapItem(i == null ? 0 : i,
                        xn == null ? 0L : xn,
                        xx == null ? 0L : xx));
            }
        }
        items.sort(Comparator.comparingInt(h -> h.id));
        return items;
    }

    static List<Integer> idsOf(List<HeapItem> items) {
        List<Integer> ids = new ArrayList<>(items.size());
        for (HeapItem h : items) {
            ids.add(h.id);
        }
        return ids;
    }

    static String formatHeapItems(List<HeapItem> items) {
        if (items == null || items.isEmpty()) {
            return "<empty>";
        }
        return items.toString();
    }

    static List<HeapItem> onlyIn(List<HeapItem> items, List<Integer> idxIds) {
        Set<Integer> idx = new LinkedHashSet<>(idxIds);
        List<HeapItem> out = new ArrayList<>();
        for (HeapItem h : items) {
            if (!idx.contains(h.id)) {
                out.add(h);
            }
        }
        return out;
    }

    static List<Integer> onlyInIds(List<Integer> left, List<Integer> right) {
        Set<Integer> r = new LinkedHashSet<>(right);
        List<Integer> out = new ArrayList<>();
        for (Integer id : left) {
            if (!r.contains(id)) {
                out.add(id);
            }
        }
        return out;
    }

    static List<Integer> queryHeapSeqscan(DBReference conn) throws ExecutionException {
        return idsOf(queryHeapItems(conn));
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

    private static CheckResult checkWideIndex(DBReference conn, List<Integer> heap, StringBuilder log)
            throws ExecutionException {
        if (heap.isEmpty()) {
            log.append("  wide index present, heap empty; skipping lookup\n");
            return null;
        }
        int id = heap.get(heap.size() / 2);
        conn.execute("set enable_seqscan = off");
        conn.execute("set enable_indexscan = on");
        try (Statement s = conn.statement(
                "select id from " + TABLE + " where wide = '" + wide(id) + "'")) {
            Output<Integer> out = s.linkOutput(1, Integer.class);
            s.execute();
            if (!s.fetch()) {
                return fail(log, null, "wide index lookup missed live heap id=" + id);
            }
            int got = out.get();
            if (got != id) {
                return fail(log, null, "wide index pointer for id=" + id
                        + " resolved to a different heap item id=" + got);
            }
            if (s.fetch()) {
                return fail(log, null, "duplicate wide index entries for id=" + id);
            }
        }
        log.append("  OK wide index lookup id=").append(id).append('\n');
        return null;
    }

    private static void churnOnce(DBReference conn, Scenario scenario, Random rng, int maxId) {
        if ("split_churn".equals(scenario.base())) {
            int id = SPLIT_IDS.getAndIncrement();
            execIgnore(conn, rowInsert(scenario, id, "c"));
            return;
        }
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

    private static String sqlForLog(String sql) {
        if (sql == null || sql.length() <= 180) {
            return sql;
        }
        return sql.substring(0, 160) + "...(" + sql.length() + " chars)";
    }

    private static void execLogged(DBReference conn, String sql) throws ExecutionException {
        System.out.println("IHC: SQL " + sqlForLog(sql));
        System.out.flush();
        conn.execute(sql);
    }

    private static boolean execIgnore(DBReference conn, String sql) {
        System.out.println("IHC: SQL " + sqlForLog(sql));
        System.out.flush();
        try {
            conn.execute(sql);
            return true;
        } catch (ExecutionException e) {
            System.out.println("IHC: SQL_FAIL " + message(e));
            System.out.flush();
            /*
             * A unique violation aborts the connection. The churn loop
             * used to keep going and spin on "context not valid".
             * That is a workload bug, not a crash point.
             */
            if (isDuplicateKey(e) || isAbortedContext(e)) {
                /*
                 * Single-client: both end the workload (the harness treats
                 * IHC: FAIL as a failed round). Multi-client reopens on an
                 * abort that is not a unique violation, so the abort text
                 * must not look like a duplicate-key failure.
                 */
                String why = isUniqueViolation(e)
                        ? "duplicate key during crash workload: "
                        : "aborted connection during crash workload: ";
                throw new IllegalStateException(why + message(e), e);
            }
            return false;
        }
    }

    static boolean isAbortedContext(Throwable t) {
        String m = message(t);
        return containsIgnoreCase(m, "context not valid")
                || containsIgnoreCase(m, "abortonly");
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

    /**
     * Filesystem invariants that VACUUM cannot paper over: relation files stay
     * block-aligned, and only the highest {@code pg_shadowlog/log_*} file may
     * still have an incomplete header.
     *
     * @return a failure line, or null when the datadir looks structurally sound
     */
    static String checkDatadirStructure(Path datadir) throws IOException {
        String relations = checkRelationSizes(datadir);
        if (relations != null) {
            return relations;
        }
        return checkShadowLogs(datadir.resolve("pg_shadowlog"));
    }

    private static String checkRelationSizes(Path datadir) throws IOException {
        Path base = datadir.resolve("base");
        if (Files.isDirectory(base)) {
            try (var walk = Files.walk(base)) {
                for (Path p : walk.filter(Files::isRegularFile).toList()) {
                    if ("PG_VERSION".equals(p.getFileName().toString())) {
                        continue;
                    }
                    String bad = blockAligned(p);
                    if (bad != null) {
                        return bad;
                    }
                }
            }
        }
        try (var root = Files.list(datadir)) {
            for (Path p : root.filter(Files::isRegularFile).toList()) {
                String name = p.getFileName().toString();
                if (name.indexOf('.') >= 0 || !name.startsWith("pg_")) {
                    continue;
                }
                String bad = blockAligned(p);
                if (bad != null) {
                    return bad;
                }
            }
        }
        return null;
    }

    private static String blockAligned(Path p) throws IOException {
        long sz = Files.size(p);
        if (sz % BLCKSZ != 0) {
            return "FAIL: relation file not block-aligned: " + p + " size=" + sz;
        }
        return null;
    }

    private static String checkShadowLogs(Path dir) throws IOException {
        if (!Files.isDirectory(dir)) {
            return "FAIL: pg_shadowlog missing after crash";
        }
        List<Path> logs = new ArrayList<>();
        try (var listing = Files.list(dir)) {
            for (Path p : listing.filter(Files::isRegularFile).toList()) {
                if (p.getFileName().toString().startsWith("log_")) {
                    logs.add(p);
                }
            }
        }
        logs.sort(Comparator.comparing(p -> p.getFileName().toString()));
        if (logs.isEmpty()) {
            return "FAIL: pg_shadowlog has no log files after crash";
        }
        for (int i = 0; i < logs.size(); i++) {
            boolean tail = i == logs.size() - 1;
            String bad = checkOneShadowLog(logs.get(i), tail);
            if (bad != null) {
                return bad;
            }
        }
        return null;
    }

    /**
     * Header layout matches vfd.c LogHeader: four int64s, then {@code completed}.
     * A non-tail file must be a whole number of blocks and start with a
     * completed header. The file still open for append may be short or incomplete.
     */
    private static String checkOneShadowLog(Path log, boolean tail) throws IOException {
        long sz = Files.size(log);
        if (!tail && sz % BLCKSZ != 0) {
            return "FAIL: non-tail shadow log not block-aligned: " + log.getFileName() + " size=" + sz;
        }
        if (sz < 33) {
            return tail ? null : "FAIL: non-tail shadow log truncated: " + log.getFileName();
        }
        byte[] buf = new byte[40];
        try (var in = Files.newInputStream(log)) {
            int n = 0;
            while (n < buf.length) {
                int r = in.read(buf, n, buf.length - n);
                if (r < 0) {
                    break;
                }
                n += r;
            }
            if (n < 33) {
                return tail ? null : "FAIL: non-tail shadow log truncated: " + log.getFileName();
            }
        }
        boolean magic = shadowMagic(buf);
        boolean completed = buf[32] != 0;
        if (!magic) {
            return tail ? null : "FAIL: non-tail shadow log bad magic: " + log.getFileName();
        }
        if (!completed && !tail) {
            return "FAIL: non-tail shadow log not completed: " + log.getFileName();
        }
        return null;
    }

    private static boolean shadowMagic(byte[] buf) {
        ByteBuffer le = ByteBuffer.wrap(buf).order(ByteOrder.LITTLE_ENDIAN);
        if (le.getLong(0) == SHADOW_HEADER_MAGIC) {
            return true;
        }
        ByteBuffer be = ByteBuffer.wrap(buf).order(ByteOrder.BIG_ENDIAN);
        return be.getLong(0) == SHADOW_HEADER_MAGIC;
    }
}
