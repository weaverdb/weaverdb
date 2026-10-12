package org.weaverdb.battle;

import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;
import java.util.Properties;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;
import org.weaverdb.DBReference;
import org.weaverdb.ExecutionException;
import org.weaverdb.FunctionInstaller;
import org.weaverdb.Input;
import org.weaverdb.Output;
import org.weaverdb.Statement;
import org.weaverdb.WeaverInitializer;

/**
 * Subprocess entry point so crashes, shutdown, and lock waits cannot take
 * down the JUnit worker that is running the rest of the suite.
 *
 * <pre>BattleChildMain &lt;jni|ffm&gt; &lt;datadir&gt; &lt;scenario&gt;</pre>
 *
 * Exit codes: 0 success, 2 handled failure, 3 feature did not do what the
 * API describes, 4 timed out, 1 unexpected exception.
 */
public final class BattleChildMain {

    private BattleChildMain() {
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 3) {
            System.err.println("usage: BattleChildMain <jni|ffm> <datadir> <scenario>");
            System.exit(2);
        }
        String mode = args[0];
        String datadir = args[1];
        String scenario = args[2];
        System.out.println("BATTLE CHILD start mode=" + mode + " scenario=" + scenario + " dir=" + datadir);
        try {
            switch (scenario) {
                case "restart" -> restart(mode, datadir);
                case "deadlock" -> deadlock(mode, datadir);
                case "shared" -> shared(mode, datadir);
                case "cancel" -> cancel(mode, datadir);
                case "abort-idle" -> abortIdle(mode, datadir);
                case "constraints" -> constraints(mode, datadir);
                case "helper" -> helper(mode, datadir);
                case "hash-index" -> hashIndex(mode, datadir);
                case "null-int" -> nullInt(mode, datadir);
                case "char-bind" -> charBind(mode, datadir);
                case "autocommit-error" -> autocommitError(mode, datadir);
                case "multipage" -> multipage(mode, datadir);
                default -> {
                    System.err.println("unknown scenario " + scenario);
                    System.exit(2);
                }
            }
            System.out.println("BATTLE CHILD done scenario=" + scenario);
        } catch (Throwable t) {
            t.printStackTrace(System.out);
            System.exit(1);
        }
    }

    private static void restart(String mode, String datadir) throws Exception {
        boot(mode, datadir);
        ensureDatabase();
        try (DBReference conn = open()) {
            conn.execute("create table bt_restart (id int4, note varchar(64))");
            try (Statement s = conn.statement("insert into bt_restart (id, note) values ($id, $note)")) {
                s.linkInput("id", Integer.class).set(7);
                s.linkInput("note", String.class).set("persisted");
                long n = s.execute();
                if (n != 1) {
                    throw new IllegalStateException("insert count " + n);
                }
            }
        }
        System.out.println("BATTLE CHILD phase=shutdown");
        shutdown(mode);
        System.out.println("BATTLE CHILD phase=reinit");
        boot(mode, datadir);
        try (DBReference conn = open()) {
            try (Statement s = conn.statement("select note from bt_restart where id = 7")) {
                Output<String> note = s.linkOutput(1, String.class);
                s.execute();
                if (!s.fetch()) {
                    throw new IllegalStateException("row missing after re-init");
                }
                if (!"persisted".equals(note.get())) {
                    throw new IllegalStateException("note=" + note.get());
                }
            }
        }
        System.out.println("BATTLE CHILD phase=shutdown2");
        shutdown(mode);
        System.out.println("BATTLE CHILD restart ok");
    }

    private static void deadlock(String mode, String datadir) throws Exception {
        boot(mode, datadir);
        ensureDatabase();
        try (DBReference conn = open()) {
            conn.execute("create table bt_dl (id int4, v int4)");
            conn.execute("insert into bt_dl (id, v) values (1, 0)");
            conn.execute("insert into bt_dl (id, v) values (2, 0)");
        }
        CountDownLatch bothLocked = new CountDownLatch(2);
        CountDownLatch goSecond = new CountDownLatch(1);
        AtomicReference<Throwable> aErr = new AtomicReference<>();
        AtomicReference<Throwable> bErr = new AtomicReference<>();
        AtomicInteger finished = new AtomicInteger();

        Thread a = new Thread(() -> lockPair(1, 2, bothLocked, goSecond, aErr, finished), "bt-dl-a");
        Thread b = new Thread(() -> lockPair(2, 1, bothLocked, goSecond, bErr, finished), "bt-dl-b");
        a.start();
        b.start();
        boolean started = bothLocked.await(8, TimeUnit.SECONDS);
        goSecond.countDown();
        a.join(10_000);
        b.join(10_000);
        boolean hung = a.isAlive() || b.isAlive();
        System.out.println("BATTLE CHILD deadlock started=" + started
                + " hung=" + hung
                + " finished=" + finished.get()
                + " aErr=" + text(aErr.get())
                + " bErr=" + text(bErr.get()));
        if (hung) {
            System.exit(4);
        }
        if (!started) {
            System.exit(1);
        }
        if (aErr.get() == null && bErr.get() == null) {
            System.out.println("BATTLE CHILD deadlock both committed (lock wait resolved without an error)");
        }
    }

    /**
     * One row is updated while another transaction holds its lock. The waiter
     * then writes the heap tuple (old page and new page) together with the
     * btree and hash index entries. After shutdown the same values must be
     * visible through the id index and the value indexes.
     */
    private static void multipage(String mode, String datadir) throws Exception {
        boot(mode, datadir);
        ensureDatabase();
        String filler = "p".repeat(180);
        try (DBReference conn = open()) {
            conn.execute("create table bt_mp (id int4, v int4, payload varchar(4000))");
            conn.execute("create index bt_mp_id on bt_mp using btree (id)");
            conn.execute("create index bt_mp_v on bt_mp using btree (v)");
            conn.execute("create index bt_mp_hash on bt_mp using hash (v)");
            for (int i = 1; i <= 400; i++) {
                conn.execute("insert into bt_mp (id, v, payload) values ("
                        + i + ", " + i + ", '" + filler + "')");
            }
        }

        CountDownLatch held = new CountDownLatch(1);
        CountDownLatch waiting = new CountDownLatch(1);
        AtomicReference<Throwable> holderErr = new AtomicReference<>();
        AtomicReference<Throwable> waiterErr = new AtomicReference<>();

        Thread holder = new Thread(() -> {
            try (DBReference conn = open()) {
                conn.begin();
                conn.execute("update bt_mp set v = v + 1 where id = 1");
                held.countDown();
                if (!waiting.await(15, TimeUnit.SECONDS)) {
                    throw new IllegalStateException("waiter did not start");
                }
                Thread.sleep(500);
                conn.commit();
            } catch (Throwable t) {
                holderErr.set(t);
                held.countDown();
            }
        }, "bt-mp-holder");

        Thread waiter = new Thread(() -> {
            try (DBReference conn = open()) {
                if (!held.await(15, TimeUnit.SECONDS)) {
                    throw new IllegalStateException("holder did not lock");
                }
                waiting.countDown();
                String wide = "w".repeat(3000);
                conn.execute("update bt_mp set v = v + 10000, payload = '" + wide
                        + "' where id = 1");
            } catch (Throwable t) {
                waiterErr.set(t);
                waiting.countDown();
            }
        }, "bt-mp-waiter");

        holder.start();
        waiter.start();
        holder.join(25_000);
        waiter.join(25_000);
        if (holder.isAlive() || waiter.isAlive()) {
            System.out.println("BATTLE CHILD multipage hung holder=" + holder.isAlive()
                    + " waiter=" + waiter.isAlive());
            System.exit(4);
        }
        if (holderErr.get() != null || waiterErr.get() != null) {
            System.out.println("BATTLE CHILD multipage holder=" + text(holderErr.get())
                    + " waiter=" + text(waiterErr.get()));
            if (holderErr.get() != null) {
                holderErr.get().printStackTrace(System.out);
            }
            if (waiterErr.get() != null) {
                waiterErr.get().printStackTrace(System.out);
            }
            System.exit(1);
        }

        checkMultipage(false);
        System.out.println("BATTLE CHILD multipage phase=shutdown");
        shutdown(mode);
        boot(mode, datadir);
        checkMultipage(true);
        shutdown(mode);
        System.out.println("BATTLE CHILD multipage ok");
    }

    private static void checkMultipage(boolean recovered) throws Exception {
        try (DBReference conn = open()) {
            int warnings = scalarInt(conn, "select cio_depth_warnings()");
            int value = scalarInt(conn, "select v from bt_mp where id = 1");
            String payload = scalarText(conn, "select payload from bt_mp where id = 1");
            int byValue = count(conn, "select id from bt_mp where v = " + value);
            int rows = count(conn, "select id from bt_mp");
            System.out.println("BATTLE CHILD multipage recovered=" + recovered
                    + " v=" + value
                    + " payloadLen=" + (payload == null ? -1 : payload.length())
                    + " byValue=" + byValue
                    + " rows=" + rows
                    + " cioWarnings=" + warnings);
            if (warnings != 0) {
                throw new IllegalStateException("cio_depth_warnings=" + warnings);
            }
            if (rows != 400) {
                throw new IllegalStateException("rows=" + rows);
            }
            if (value != 10002) {
                throw new IllegalStateException("v=" + value + " after contended update");
            }
            if (payload == null || payload.length() != 3000) {
                throw new IllegalStateException("payload length "
                        + (payload == null ? -1 : payload.length()));
            }
            if (byValue != 1) {
                throw new IllegalStateException("index lookup count " + byValue + " for v=" + value);
            }
        }
    }

    private static int scalarInt(DBReference conn, String sql) throws Exception {
        try (Statement s = conn.statement(sql)) {
            Output<Integer> out = s.linkOutput(1, Integer.class);
            s.execute();
            if (!s.fetch() || out.get() == null) {
                throw new IllegalStateException("no integer for " + sql);
            }
            return out.get();
        }
    }

    private static String scalarText(DBReference conn, String sql) throws Exception {
        try (Statement s = conn.statement(sql)) {
            Output<String> out = s.linkOutput(1, String.class);
            s.execute();
            if (!s.fetch()) {
                return null;
            }
            return out.get();
        }
    }

    private static void lockPair(int first, int second, CountDownLatch bothLocked, CountDownLatch goSecond,
            AtomicReference<Throwable> error, AtomicInteger finished) {
        try (DBReference conn = open()) {
            conn.begin();
            try (Statement s = conn.statement("update bt_dl set v = v + 1 where id = $id")) {
                Input<Integer> id = s.linkInput("id", Integer.class);
                id.set(first);
                s.execute();
                bothLocked.countDown();
                if (!goSecond.await(8, TimeUnit.SECONDS)) {
                    throw new IllegalStateException("partner did not lock");
                }
                id.set(second);
                s.execute();
            }
            conn.commit();
            finished.incrementAndGet();
        } catch (Throwable t) {
            error.set(t);
        }
    }

    /**
     * Two threads. The second thread must call {@code helper()} itself;
     * sharing one connection without a helper is not supported.
     */
    private static void shared(String mode, String datadir) throws Exception {
        boot(mode, datadir);
        ensureDatabase();
        try (DBReference conn = open()) {
            conn.execute("create table bt_shared (id int4, note varchar(32))");
            AtomicInteger failures = new AtomicInteger();
            AtomicReference<Throwable> first = new AtomicReference<>();
            CountDownLatch helperReady = new CountDownLatch(1);
            Thread t2 = new Thread(() -> {
                DBReference helper = null;
                try {
                    helper = conn.helper();
                    helperReady.countDown();
                    hammer(helper, 1000, failures, first);
                } catch (Throwable t) {
                    failures.incrementAndGet();
                    first.compareAndSet(null, t);
                } finally {
                    helperReady.countDown();
                    if (helper != null) {
                        try {
                            helper.close();
                        } catch (Throwable t) {
                            failures.incrementAndGet();
                            first.compareAndSet(null, t);
                        }
                    }
                }
            }, "bt-sh-2");
            t2.start();
            if (!helperReady.await(20, TimeUnit.SECONDS)) {
                System.out.println("BATTLE CHILD shared helper timed out");
                System.exit(4);
            }
            Thread t1 = new Thread(() -> hammer(conn, 0, failures, first), "bt-sh-1");
            t1.start();
            t1.join(20_000);
            t2.join(20_000);
            if (t1.isAlive() || t2.isAlive()) {
                System.out.println("BATTLE CHILD shared hung");
                System.exit(4);
            }
            int rows = count(conn, "select id from bt_shared");
            System.out.println("BATTLE CHILD shared rows=" + rows + " failures=" + failures.get()
                    + " first=" + text(first.get()));
            if (failures.get() > 0) {
                System.exit(2);
            }
            if (rows != 40) {
                throw new IllegalStateException("expected 40 rows, got " + rows);
            }
        }
    }

    private static void hammer(DBReference conn, int base, AtomicInteger failures, AtomicReference<Throwable> first) {
        try {
            for (int i = 0; i < 20; i++) {
                try (Statement s = conn.statement("insert into bt_shared (id, note) values ($id, $note)")) {
                    s.linkInput("id", Integer.class).set(base + i);
                    s.linkInput("note", String.class).set("n" + (base + i));
                    s.execute();
                }
            }
        } catch (Throwable t) {
            failures.incrementAndGet();
            first.compareAndSet(null, t);
        }
    }

    private static void cancel(String mode, String datadir) throws Exception {
        boot(mode, datadir);
        ensureDatabase();
        try (DBReference conn = open()) {
            new FunctionInstaller(conn).installFunction("bt_sleep",
                    MethodHandles.lookup().findStatic(BattleFunctions.class, "sleepMs",
                            MethodType.methodType(int.class, int.class)));
            AtomicReference<Throwable> error = new AtomicReference<>();
            AtomicInteger returned = new AtomicInteger(Integer.MIN_VALUE);
            Thread worker = new Thread(() -> {
                try (Statement s = conn.statement("select bt_sleep($ms)")) {
                    s.linkInput("ms", Integer.class).set(6000);
                    Output<Integer> out = s.linkOutput(1, Integer.class);
                    s.execute();
                    if (s.fetch() && out.get() != null) {
                        returned.set(out.get());
                    }
                } catch (Throwable t) {
                    error.set(t);
                }
            }, "bt-cancel-worker");
            long start = System.nanoTime();
            worker.start();
            Thread.sleep(400);
            try {
                conn.cancel();
            } catch (Throwable t) {
                System.out.println("BATTLE CHILD cancel() threw " + t);
            }
            worker.join(3_000);
            long elapsedMs = (System.nanoTime() - start) / 1_000_000L;
            System.out.println("BATTLE CHILD cancel alive=" + worker.isAlive()
                    + " elapsedMs=" + elapsedMs
                    + " returned=" + returned.get()
                    + " err=" + text(error.get()));
            if (worker.isAlive()) {
                System.exit(4);
            }
            if (returned.get() == 6000) {
                System.exit(3);
            }
        }
    }

    /**
     * {@code abort()} with no {@code begin()}, then one more statement.
     * A native crash exits via the JVM; a handled panic exits 2.
     */
    private static void abortIdle(String mode, String datadir) throws Exception {
        boot(mode, datadir);
        ensureDatabase();
        try (DBReference conn = open()) {
            boolean threw = false;
            try {
                conn.abort();
                System.out.println("BATTLE CHILD abort-idle returned valid=" + conn.isValid());
            } catch (Throwable t) {
                threw = true;
                System.out.println("BATTLE CHILD abort-idle threw " + text(t));
            }
            try (Statement s = conn.statement("select 1")) {
                Output<Integer> out = s.linkOutput(1, Integer.class);
                s.execute();
                if (!s.fetch()) {
                    throw new IllegalStateException("no row after idle abort");
                }
                System.out.println("BATTLE CHILD abort-idle followup=" + out.get());
            }
            if (threw) {
                System.exit(2);
            }
        }
    }

    /**
     * Bind a null {@code Integer} into an int4 column and read it back.
     * Isolated because the FFM upcall throws an uncaught {@code NullPointerException}
     * and the JVM exits.
     */
    private static void nullInt(String mode, String datadir) throws Exception {
        boot(mode, datadir);
        ensureDatabase();
        try (DBReference conn = open()) {
            conn.execute("create table bt_nullint (id int4, qty int4, r float4, bi int8)");
            try (Statement s = conn.statement(
                    "insert into bt_nullint (id, qty, r, bi) values ($id, $qty, $r, $bi)")) {
                s.linkInput("id", Integer.class).set(1);
                s.linkInput("qty", Integer.class).set(null);
                s.linkInput("r", Float.class).set(null);
                s.linkInput("bi", Long.class).set(null);
                long n = s.execute();
                System.out.println("BATTLE CHILD null-int insert count=" + n);
            }
            try (Statement s = conn.statement("select qty, r, bi from bt_nullint where id = 1")) {
                Output<Integer> qty = s.linkOutput(1, Integer.class);
                Output<Float> r = s.linkOutput(2, Float.class);
                Output<Long> bi = s.linkOutput(3, Long.class);
                s.execute();
                if (!s.fetch()) {
                    throw new IllegalStateException("null-int select returned no row");
                }
                Integer value = qty.get();
                Float real = r.get();
                Long wide = bi.get();
                System.out.println("BATTLE CHILD null-int qty=" + value + " r=" + real + " bi=" + wide);
                if (value != null || real != null || wide != null) {
                    System.exit(3);
                }
            }
        }
    }

    /**
     * Bind a {@code Character} into char(1). Isolated because the FFM upcall
     * writes two bytes into a one-byte slot and the JVM exits.
     */
    private static void charBind(String mode, String datadir) throws Exception {
        boot(mode, datadir);
        ensureDatabase();
        try (DBReference conn = open()) {
            conn.execute("create table bt_char (c char(1))");
            try (Statement s = conn.statement("insert into bt_char (c) values ($c)")) {
                s.linkInput("c", Character.class).set('Q');
                long n = s.execute();
                System.out.println("BATTLE CHILD char-bind insert count=" + n);
            }
            try (Statement s = conn.statement("select c from bt_char")) {
                Output<Character> out = s.linkOutput(1, Character.class);
                s.execute();
                if (!s.fetch()) {
                    throw new IllegalStateException("char-bind select returned no row");
                }
                System.out.println("BATTLE CHILD char-bind value=" + out.get());
                if (out.get() == null || out.get() != 'Q') {
                    System.exit(3);
                }
            }
        }
    }

    /**
     * A failed autocommit statement must leave the same connection usable.
     */
    private static void autocommitError(String mode, String datadir) throws Exception {
        boot(mode, datadir);
        ensureDatabase();
        try (DBReference conn = open()) {
            try {
                conn.execute("select * from bt_no_such_table");
                System.out.println("BATTLE CHILD autocommit-error accepted bad sql");
                System.exit(3);
            } catch (ExecutionException ex) {
                System.out.println("BATTLE CHILD autocommit-error: " + ex.getMessage());
            }
            try (Statement s = conn.statement("select 1")) {
                Output<Integer> out = s.linkOutput(1, Integer.class);
                s.execute();
                if (!s.fetch() || out.get() == null || out.get() != 1) {
                    System.out.println("BATTLE CHILD autocommit-error followup=" + out.get());
                    System.exit(3);
                }
                System.out.println("BATTLE CHILD autocommit-error followup=" + out.get());
            }
        }
    }

    private static void hashIndex(String mode, String datadir) throws Exception {
        boot(mode, datadir);
        ensureDatabase();
        try (DBReference conn = open()) {
            conn.execute("create table bt_hash (id int4)");
            conn.execute("insert into bt_hash (id) values (4)");
            System.out.println("BATTLE CHILD hash creating index");
            conn.execute("create index bt_hash_id on bt_hash using hash (id)");
            System.out.println("BATTLE CHILD hash index created");
        }
    }

    private static void helper(String mode, String datadir) throws Exception {
        boot(mode, datadir);
        ensureDatabase();
        try (DBReference conn = open()) {
            try (DBReference child = conn.helper()) {
                System.out.println("BATTLE CHILD helper class=" + child.getClass().getName()
                        + " valid=" + child.isValid());
                try (Statement s = child.statement("select 1")) {
                    Output<Integer> out = s.linkOutput(1, Integer.class);
                    s.execute();
                    if (!s.fetch()) {
                        throw new IllegalStateException("helper select returned no row");
                    }
                    System.out.println("BATTLE CHILD helper select=" + out.get());
                }
            }
        }
    }

    /** Primary key, NOT NULL, defaults, truncate, and vacuum. Isolated because this sequence SIGSEGV'd in-process. */
    private static void constraints(String mode, String datadir) throws Exception {
        boot(mode, datadir);
        ensureDatabase();
        try (DBReference conn = open()) {
            conn.execute("create table bt_pk (id int4 primary key, name varchar(16) not null)");
            conn.execute("insert into bt_pk (id, name) values (1, 'one')");
            try {
                conn.execute("insert into bt_pk (id, name) values (1, 'two')");
                System.out.println("BATTLE CHILD pk duplicate accepted");
                System.exit(3);
            } catch (ExecutionException ex) {
                System.out.println("BATTLE CHILD pk duplicate: " + ex.getMessage());
            }
        }
        try (DBReference conn = open()) {
            try {
                conn.execute("insert into bt_pk (id, name) values (2, null)");
                System.out.println("BATTLE CHILD null into not-null accepted");
                System.exit(3);
            } catch (ExecutionException ex) {
                System.out.println("BATTLE CHILD not null: " + ex.getMessage());
            }
            conn.execute("create table bt_def (id int4 default 5, name varchar(16) default 'anon')");
            conn.execute("insert into bt_def (name) values ('kept')");
            try (Statement s = conn.statement("select id, name from bt_def")) {
                Output<Integer> id = s.linkOutput(1, Integer.class);
                Output<String> name = s.linkOutput(2, String.class);
                s.execute();
                if (!s.fetch() || id.get() == null || id.get() != 5 || !"kept".equals(name.get())) {
                    throw new IllegalStateException("defaults id=" + id.get() + " name=" + name.get());
                }
            }
            conn.execute("insert into bt_pk (id, name) values (9, 'z')");
            conn.execute("truncate table bt_pk");
            int n = count(conn, "select id from bt_pk");
            System.out.println("BATTLE CHILD truncate rows=" + n);
            if (n != 0) {
                System.exit(3);
            }
            conn.execute("vacuum bt_pk");
            System.out.println("BATTLE CHILD constraints ok");
        }
    }

    private static void boot(String mode, String datadir) throws Exception {
        Properties props = new Properties();
        props.setProperty("datadir", datadir);
        props.setProperty("stdlog", "TRUE");
        props.setProperty("disable_crc", "TRUE");
        props.setProperty("index_corruption", "IGNORE");
        props.setProperty("heap_corruption", "IGNORE");
        if ("ffm".equals(mode)) {
            Class<?> cls = Class.forName("org.weaverdb.direct.DirectWeaverInitializer");
            cls.getMethod("initialize", Properties.class).invoke(null, props);
        } else if ("jni".equals(mode)) {
            WeaverInitializer.initialize(props);
        } else {
            throw new IllegalArgumentException(mode);
        }
        System.out.println("BATTLE CHILD booted " + mode);
    }

    private static void shutdown(String mode) throws Exception {
        if ("ffm".equals(mode)) {
            Class<?> cls = Class.forName("org.weaverdb.direct.DirectWeaverInitializer");
            cls.getMethod("forceShutdown").invoke(null);
        } else {
            WeaverInitializer.forceShutdown();
        }
    }

    private static void ensureDatabase() throws Exception {
        try (DBReference conn = DBReference.connect("template1")) {
            if (conn == null) {
                throw new IllegalStateException("template1 connect returned null");
            }
            try {
                conn.execute("create database bttest");
            } catch (ExecutionException ex) {
                System.out.println("BATTLE CHILD create database: " + ex.getMessage());
            }
        }
    }

    private static DBReference open() throws Exception {
        DBReference conn = DBReference.connect("bttest");
        if (conn == null || !conn.isValid()) {
            throw new IllegalStateException("bttest connect failed: " + conn);
        }
        return conn;
    }

    private static int count(DBReference conn, String sql) throws Exception {
        try (Statement s = conn.statement(sql)) {
            s.linkOutput(1, Object.class);
            s.execute();
            int n = 0;
            while (s.fetch()) {
                n++;
            }
            return n;
        }
    }

    private static String text(Throwable t) {
        if (t == null) {
            return "none";
        }
        String msg = t.getMessage();
        return t.getClass().getSimpleName() + (msg == null ? "" : (": " + msg));
    }
}
