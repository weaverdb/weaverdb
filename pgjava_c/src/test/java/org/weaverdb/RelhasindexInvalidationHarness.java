/*-------------------------------------------------------------------------
 *
 * Two connections, no crash. Connection B inserts and updates a table
 * it already has open while connection A creates an index. After A
 * commits, every heap row must have an index entry, in this process
 * and again after a clean shutdown and boot.
 *
 *-------------------------------------------------------------------------
 */

package org.weaverdb;

import java.nio.file.Path;
import java.time.Duration;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Properties;
import java.util.Set;
import java.util.TreeSet;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;

/**
 * Engine boot/shutdown is supplied by the caller so the same scenario
 * runs on the JNI initializer and the FFM initializer.
 */
public final class RelhasindexInvalidationHarness {

    public interface Engine {
        void initialize(Properties props) throws Exception;

        void shutdown() throws Exception;
    }

    private static final int PRE = 32;
    private static final int POST = 32;

    private final Engine engine;
    private final Path dataDir;
    private final StringBuilder report = new StringBuilder();
    private int failures;

    private RelhasindexInvalidationHarness(Path dataDir, Engine engine) {
        this.dataDir = dataDir;
        this.engine = engine;
    }

    public static int run(Path dataDir, int trials, Engine engine) {
        RelhasindexInvalidationHarness harness = new RelhasindexInvalidationHarness(dataDir, engine);
        try {
            harness.execute(trials);
        } catch (Throwable t) {
            harness.failures++;
            harness.line("HARNESS exception " + t);
            t.printStackTrace(System.out);
        }
        harness.line(harness.failures == 0
                ? "RELHAS OK trials=" + trials
                : "RELHAS FAIL trials=" + trials + " failedPhases=" + harness.failures);
        System.out.print(harness.report);
        System.out.flush();
        return harness.failures == 0 ? 0 : 1;
    }

    public static void main(String[] args) {
        if (args.length != 2) {
            System.err.println("usage: RelhasindexInvalidationHarness <datadir> <trials>");
            System.exit(2);
        }
        int rc = run(Path.of(args[0]), Integer.parseInt(args[1]), new Engine() {
            @Override
            public void initialize(Properties props) {
                WeaverInitializer.initialize(props);
            }

            @Override
            public void shutdown() throws Exception {
                WeaverInitializer.shutdown(Duration.ofSeconds(30));
            }
        });
        System.exit(rc);
    }

    private void execute(int trials) throws Exception {
        engine.initialize(bootProps());
        try {
            for (int trial = 1; trial <= trials; trial++) {
                phaseIdle(trial);
                phaseOverlap(trial);
            }
        } finally {
            engine.shutdown();
        }
    }

    /**
     * B caches the table, waits outside any statement, A commits CREATE INDEX,
     * then B inserts again.
     */
    private void phaseIdle(int trial) throws Exception {
        String table = "rh_idle_" + trial;
        String index = table + "_idx";
        List<Integer> pre = new ArrayList<>();
        List<Integer> post = new ArrayList<>();
        try (DBReference a = DBReference.connect("template1")) {
            exec(a, "create table " + table + " (id int4, val text)");
            try (DBReference b = DBReference.connect("template1")) {
                for (int id = 1; id <= PRE; id++) {
                    exec(b, "insert into " + table + " values (" + id + ", 'pre')");
                    pre.add(id);
                }
                exec(a, "create unique index " + index + " on " + table + " (id)");
                for (int id = 1; id <= PRE; id += 2) {
                    exec(b, "update " + table + " set val = 'pre-upd' where id = " + id);
                }
                for (int id = PRE + 1; id <= PRE + POST; id++) {
                    exec(b, "insert into " + table + " values (" + id + ", 'post')");
                    post.add(id);
                }
            }
        }
        check(trial, "idle", table, pre, List.of(), post);
    }

    /**
     * B keeps inserting and updating on its own connection while A builds
     * the index, then inserts once more after A returns. Repeated so a
     * commit-window race shows up as a miss rate.
     */
    private void phaseOverlap(int trial) throws Exception {
        int cycles = 25;
        int misses = 0;
        String lastTable = null;
        List<Integer> lastPre = List.of();
        List<Integer> lastMid = List.of();
        List<Integer> lastPost = List.of();
        for (int cycle = 1; cycle <= cycles; cycle++) {
            OverlapResult result = overlapCycle(trial, cycle);
            lastTable = result.table;
            lastPre = result.pre;
            lastMid = result.mid;
            lastPost = result.post;
            if (result.report.contains("MISSING")) {
                misses++;
                failures++;
            }
            line("trial=" + trial + " phase=overlap cycle=" + cycle + " inproc " + result.report);
        }
        line("trial=" + trial + " overlapCycles=" + cycles + " cyclesWithMissing=" + misses);
        if (lastTable != null) {
            engine.shutdown();
            engine.initialize(bootProps());
            String restarted = verify(lastTable, lastPre, lastMid, lastPost);
            line("trial=" + trial + " phase=overlap restart " + restarted);
            if (restarted.contains("MISSING")) {
                failures++;
            }
        }
    }

    private OverlapResult overlapCycle(int trial, int cycle) throws Exception {
        String table = "rh_over_" + trial + "_" + cycle;
        String index = table + "_idx";
        List<Integer> pre = Collections.synchronizedList(new ArrayList<>());
        List<Integer> mid = Collections.synchronizedList(new ArrayList<>());
        List<Integer> post = Collections.synchronizedList(new ArrayList<>());
        AtomicReference<Throwable> fatal = new AtomicReference<>();
        CountDownLatch looping = new CountDownLatch(1);
        AtomicBoolean indexDone = new AtomicBoolean(false);
        AtomicInteger nextId = new AtomicInteger(100_000 + cycle * 10_000);

        Thread bThread = new Thread(() -> overlapClient(table, pre, mid, post, fatal,
                looping, indexDone, nextId), "relhas-b-" + trial + "-" + cycle);
        try (DBReference a = DBReference.connect("template1")) {
            exec(a, "create table " + table + " (id int4, val text)");
            for (int id = 1; id <= PRE; id++) {
                exec(a, "insert into " + table + " values (" + id + ", 'seed')");
                pre.add(id);
            }
            bThread.start();
            if (!looping.await(30, TimeUnit.SECONDS)) {
                throw new IllegalStateException("B did not start inserting");
            }
            execRetry(a, "create unique index " + index + " on " + table + " (id)");
        } finally {
            indexDone.set(true);
        }
        bThread.join(60_000);
        Throwable err = fatal.get();
        if (err != null) {
            throw err instanceof Exception e ? e : new IllegalStateException(err);
        }
        if (bThread.isAlive()) {
            throw new IllegalStateException("B did not stop after CREATE INDEX");
        }
        OverlapResult result = new OverlapResult();
        result.table = table;
        result.pre = snapshot(pre);
        result.mid = snapshot(mid);
        result.post = snapshot(post);
        result.report = verify(table, result.pre, result.mid, result.post);
        return result;
    }

    private static final class OverlapResult {
        String table;
        List<Integer> pre;
        List<Integer> mid;
        List<Integer> post;
        String report;
    }

    private void overlapClient(String table, List<Integer> pre, List<Integer> mid, List<Integer> post,
            AtomicReference<Throwable> fatal, CountDownLatch looping, AtomicBoolean indexDone,
            AtomicInteger nextId) {
        DBReference b = null;
        try {
            b = DBReference.connect("template1");
            /* Touch the table so this connection caches relhasindex=false. */
            execRetry(b, "update " + table + " set val = 'warm' where id = 1");
            looping.countDown();
            while (!indexDone.get()) {
                int id = nextId.getAndIncrement();
                execRetry(b, "insert into " + table + " values (" + id + ", 'mid')");
                mid.add(id);
                if ((id & 3) == 0) {
                    execRetry(b, "update " + table + " set val = 'mid' where id = 1");
                }
            }
            execRetry(b, "update " + table + " set val = 'post' where id = 1");
            int postBase = nextId.get();
            for (int n = 0; n < POST; n++) {
                int id = postBase + n;
                execRetry(b, "insert into " + table + " values (" + id + ", 'post')");
                post.add(id);
            }
        } catch (Throwable t) {
            fatal.compareAndSet(null, t);
            looping.countDown();
        } finally {
            if (b != null) {
                try {
                    b.close();
                } catch (Exception ignored) {
                }
            }
        }
    }

    private void check(int trial, String phase, String table,
            List<Integer> pre, List<Integer> mid, List<Integer> post) throws Exception {
        String live = verify(table, pre, mid, post);
        line("trial=" + trial + " phase=" + phase + " inproc " + live);
        if (live.contains("MISSING")) {
            failures++;
        }
        engine.shutdown();
        engine.initialize(bootProps());
        String restarted = verify(table, pre, mid, post);
        line("trial=" + trial + " phase=" + phase + " restart " + restarted);
        if (restarted.contains("MISSING")) {
            failures++;
        }
    }

    private String verify(String table, List<Integer> pre, List<Integer> mid, List<Integer> post)
            throws Exception {
        try (DBReference c = DBReference.connect("template1")) {
            Boolean flag = relhasindex(c, table);
            Set<Integer> heap = queryIds(c, "select id from " + table);
            exec(c, "set enable_seqscan=off");
            exec(c, "set enable_indexscan=on");
            Set<Integer> indexed = queryIds(c, "select id from " + table);
            Set<Integer> missing = new TreeSet<>(heap);
            missing.removeAll(indexed);
            int lookupMiss = 0;
            for (int id : concat(pre, mid, post)) {
                Set<Integer> one = queryIds(c, "select id from " + table + " where id = " + id);
                if (!one.contains(id)) {
                    lookupMiss++;
                }
            }
            int heapOnly = 0;
            for (int id : concat(pre, mid, post)) {
                if (!heap.contains(id)) {
                    heapOnly++;
                }
            }
            String status = missing.isEmpty() && lookupMiss == 0 && heapOnly == 0 ? "ok" : "MISSING";
            return status
                    + " relhasindex=" + flag
                    + " heap=" + heap.size()
                    + " index=" + indexed.size()
                    + " missing=" + missing.size()
                    + " lookupMiss=" + lookupMiss
                    + " absentFromHeap=" + heapOnly
                    + " preMiss=" + countMissing(pre, missing)
                    + "/" + pre.size()
                    + " midMiss=" + countMissing(mid, missing)
                    + "/" + mid.size()
                    + " postMiss=" + countMissing(post, missing)
                    + "/" + post.size()
                    + sample(missing);
        }
    }

    private Properties bootProps() {
        Properties props = new Properties();
        props.setProperty("datadir", dataDir.toString());
        props.setProperty("stdlog", "FALSE");
        props.setProperty("disable_crc", "TRUE");
        props.setProperty("synctimeout", "1000");
        props.setProperty("index_corruption", "IGNORE");
        props.setProperty("heap_corruption", "IGNORE");
        return props;
    }

    private static Boolean relhasindex(DBReference conn, String table) throws Exception {
        try (Statement statement = conn.statement(
                "select relhasindex from pg_class where relname = '" + table + "'")) {
            Output<Boolean> flag = statement.linkOutput(1, Boolean.class);
            statement.execute();
            if (!statement.fetch()) {
                return null;
            }
            return flag.get();
        }
    }

    private static Set<Integer> queryIds(DBReference conn, String sql) throws Exception {
        Set<Integer> ids = new TreeSet<>();
        try (Statement statement = conn.statement(sql)) {
            Output<Integer> id = statement.linkOutput(1, Integer.class);
            statement.execute();
            while (statement.fetch()) {
                Integer value = id.get();
                if (value != null) {
                    ids.add(value);
                }
            }
        }
        return ids;
    }

    private static void exec(DBReference conn, String sql) throws Exception {
        conn.execute(sql);
    }

    private static void execRetry(DBReference conn, String sql) throws Exception {
        ExecutionException last = null;
        for (int attempt = 0; attempt < 5; attempt++) {
            try {
                conn.execute(sql);
                return;
            } catch (ExecutionException e) {
                last = e;
                if (!isLock(e) || attempt == 4) {
                    throw e;
                }
            }
        }
        throw last;
    }

    private static boolean isLock(Throwable t) {
        String m = t.getMessage() == null ? "" : t.getMessage().toLowerCase();
        return m.contains("deadlock") || m.contains("lock failed") || m.contains("cancelled");
    }

    private static int countMissing(List<Integer> ids, Set<Integer> missing) {
        int n = 0;
        for (int id : ids) {
            if (missing.contains(id)) {
                n++;
            }
        }
        return n;
    }

    private static List<Integer> concat(List<Integer> a, List<Integer> b, List<Integer> c) {
        List<Integer> all = new ArrayList<>(a.size() + b.size() + c.size());
        all.addAll(a);
        all.addAll(b);
        all.addAll(c);
        return all;
    }

    private static List<Integer> snapshot(List<Integer> ids) {
        synchronized (ids) {
            return new ArrayList<>(ids);
        }
    }

    private static String sample(Set<Integer> missing) {
        if (missing.isEmpty()) {
            return "";
        }
        StringBuilder sb = new StringBuilder(" sample=");
        int n = 0;
        for (int id : missing) {
            if (n++ == 8) {
                sb.append("...");
                break;
            }
            if (n > 1) {
                sb.append(',');
            }
            sb.append(id);
        }
        return sb.toString();
    }

    private void line(String text) {
        report.append(text).append('\n');
        System.out.println(text);
        System.out.flush();
    }
}
