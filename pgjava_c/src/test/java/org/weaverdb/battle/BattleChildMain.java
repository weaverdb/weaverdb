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
                case "vacuum-group" -> vacuumGroup(mode, datadir);
                case "prefetch-drop" -> prefetchDrop(mode, datadir);
                case "freespace-extend" -> freespaceExtend(mode, datadir);
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
            int value = scalarInt(conn, "select v from bt_mp where id = 1");
            String payload = scalarText(conn, "select payload from bt_mp where id = 1");
            int byValue = count(conn, "select id from bt_mp where v = " + value);
            int rows = count(conn, "select id from bt_mp");
            Integer warnings = depthWarnings();
            System.out.println("BATTLE CHILD multipage recovered=" + recovered
                    + " v=" + value
                    + " payloadLen=" + (payload == null ? -1 : payload.length())
                    + " byValue=" + byValue
                    + " rows=" + rows
                    + " cioWarnings=" + (warnings == null ? "absent" : warnings));
            if (warnings != null && warnings != 0) {
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

    /**
     * Dead tuples are vacuumed with a btree index present, then a row is
     * widened so the heap tuple moves to another page. The heap page and
     * its index entries are one write group; shutdown must preserve both.
     */
    private static void vacuumGroup(String mode, String datadir) throws Exception {
        boot(mode, datadir);
        ensureDatabase();
        String filler = "p".repeat(180);
        String wide = "w".repeat(3000);
        try (DBReference conn = open()) {
            conn.execute("create table bt_vac (id int4, v int4, payload varchar(4000))");
            conn.execute("create index bt_vac_id on bt_vac using btree (id)");
            conn.execute("create index bt_vac_v on bt_vac using btree (v)");
            for (int i = 1; i <= 80; i++) {
                conn.execute("insert into bt_vac (id, v, payload) values ("
                        + i + ", " + i + ", '" + filler + "')");
            }
            conn.execute("delete from bt_vac where id % 3 = 0");
            conn.execute("vacuum bt_vac");
            conn.execute("update bt_vac set v = 5000, payload = '" + wide + "' where id = 2");
            conn.execute("update bt_vac set v = 6, payload = 's' where id = 4");
        }
        checkVacuumGroup(false);
        System.out.println("BATTLE CHILD vacuum-group phase=shutdown");
        shutdown(mode);
        boot(mode, datadir);
        checkVacuumGroup(true);
        shutdown(mode);
        System.out.println("BATTLE CHILD vacuum-group ok");
    }

    private static void checkVacuumGroup(boolean recovered) throws Exception {
        try (DBReference conn = open()) {
            int rows = count(conn, "select id from bt_vac");
            int byValue = count(conn, "select id from bt_vac where v = 5000");
            int shrunk = count(conn, "select id from bt_vac where id = 4 and v = 6");
            String payload = scalarText(conn, "select payload from bt_vac where id = 2");
            String shortPayload = scalarText(conn, "select payload from bt_vac where id = 4");
            Integer warnings = depthWarnings();
            System.out.println("BATTLE CHILD vacuum-group recovered=" + recovered
                    + " rows=" + rows
                    + " byValue=" + byValue
                    + " shrunk=" + shrunk
                    + " payloadLen=" + (payload == null ? -1 : payload.length())
                    + " shortLen=" + (shortPayload == null ? -1 : shortPayload.length())
                    + " cioWarnings=" + (warnings == null ? "absent" : warnings));
            if (warnings != null && warnings != 0) {
                throw new IllegalStateException("cio_depth_warnings=" + warnings);
            }
            if (rows != 54) {
                throw new IllegalStateException("rows=" + rows);
            }
            if (byValue != 1) {
                throw new IllegalStateException("index lookup count " + byValue);
            }
            if (shrunk != 1) {
                throw new IllegalStateException("shrunk row missing");
            }
            if (payload == null || payload.length() != 3000) {
                throw new IllegalStateException("payload length "
                        + (payload == null ? -1 : payload.length()));
            }
            if (shortPayload == null || shortPayload.length() != 1) {
                throw new IllegalStateException("short payload " + shortPayload);
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

    /**
     * Main has no cio_depth_warnings catalog entry. A missing function is
     * reported as absent; a present function still has to return zero.
     */
    private static Integer depthWarnings() {
        try (DBReference conn = open()) {
            return scalarInt(conn, "select cio_depth_warnings()");
        } catch (Exception ex) {
            return null;
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
     * A second thread must not use the parent's connection. That call has
     * to throw. A helper sub-connection is a different scenario.
     */
    private static void shared(String mode, String datadir) throws Exception {
        boot(mode, datadir);
        ensureDatabase();
        try (DBReference conn = open()) {
            conn.execute("create table bt_shared (id int4)");
            conn.execute("insert into bt_shared (id) values (1)");
            AtomicReference<Throwable> foreign = new AtomicReference<>();
            Thread t2 = new Thread(() -> {
                try {
                    conn.execute("insert into bt_shared (id) values (2)");
                    foreign.set(new IllegalStateException("foreign thread insert succeeded"));
                } catch (Throwable t) {
                    foreign.set(t);
                }
            }, "bt-sh-2");
            t2.start();
            t2.join(20_000);
            if (t2.isAlive()) {
                System.out.println("BATTLE CHILD shared hung");
                System.exit(4);
            }
            Throwable thrown = foreign.get();
            String msg = text(thrown);
            System.out.println("BATTLE CHILD shared foreign=" + msg);
            if (thrown == null || !msg.contains("owned by a different thread")) {
                System.exit(3);
            }
            int rows = count(conn, "select id from bt_shared");
            System.out.println("BATTLE CHILD shared rows=" + rows);
            if (rows != 1) {
                System.exit(3);
            }
        }
    }

    /**
     * The worker opens its own connection and runs {@code bt_sleep(6000)} on it.
     * {@code WCancel} only sets the env cancel flag and does not check
     * {@code thread_owner}, so another thread may call {@code cancel()} on
     * that connection while the owner is inside the statement.
     */
    private static void cancel(String mode, String datadir) throws Exception {
        boot(mode, datadir);
        ensureDatabase();
        try (DBReference setup = open()) {
            new FunctionInstaller(setup).installFunction("bt_sleep",
                    MethodHandles.lookup().findStatic(BattleFunctions.class, "sleepMs",
                            MethodType.methodType(int.class, int.class)));
        }
        BattleFunctions.sleepEntered = false;
        AtomicReference<DBReference> holder = new AtomicReference<>();
        CountDownLatch opened = new CountDownLatch(1);
        AtomicReference<Throwable> error = new AtomicReference<>();
        AtomicInteger returned = new AtomicInteger(Integer.MIN_VALUE);
        AtomicReference<String> followup = new AtomicReference<>("missing");
        Thread worker = new Thread(() -> {
            try (DBReference own = open()) {
                holder.set(own);
                opened.countDown();
                try (Statement s = own.statement("select bt_sleep($ms)")) {
                    s.linkInput("ms", Integer.class).set(6000);
                    Output<Integer> out = s.linkOutput(1, Integer.class);
                    s.execute();
                    if (s.fetch() && out.get() != null) {
                        returned.set(out.get());
                    }
                } catch (Throwable t) {
                    error.set(t);
                }
                try (Statement s = own.statement("select 1")) {
                    Output<Integer> out = s.linkOutput(1, Integer.class);
                    s.execute();
                    if (!s.fetch() || out.get() == null || out.get() != 1) {
                        followup.set("bad");
                    } else {
                        followup.set(String.valueOf(out.get()));
                    }
                }
            } catch (Throwable t) {
                if (error.get() == null) {
                    error.set(t);
                }
                opened.countDown();
            }
        }, "bt-cancel-worker");
        worker.start();
        if (!opened.await(10, TimeUnit.SECONDS) || holder.get() == null) {
            System.out.println("BATTLE CHILD cancel worker did not open");
            worker.join(1_000);
            System.exit(4);
        }
        long waitStart = System.nanoTime();
        while (!BattleFunctions.sleepEntered) {
            if (!worker.isAlive()) {
                System.out.println("BATTLE CHILD cancel sleep never started err=" + text(error.get()));
                System.exit(3);
            }
            if ((System.nanoTime() - waitStart) / 1_000_000L > 5_000) {
                System.out.println("BATTLE CHILD cancel sleep never started");
                System.exit(3);
            }
            Thread.sleep(20);
        }
        System.out.println("BATTLE CHILD cancel sleep started");
        long start = System.nanoTime();
        try {
            holder.get().cancel();
        } catch (Throwable t) {
            System.out.println("BATTLE CHILD cancel() threw " + text(t));
            System.exit(3);
        }
        worker.join(3_000);
        long elapsedMs = (System.nanoTime() - start) / 1_000_000L;
        String err = text(error.get());
        System.out.println("BATTLE CHILD cancel alive=" + worker.isAlive()
                + " elapsedMs=" + elapsedMs
                + " returned=" + returned.get()
                + " err=" + err);
        System.out.println("BATTLE CHILD cancel followup=" + followup.get());
        if (worker.isAlive()) {
            System.exit(4);
        }
        if (returned.get() == 6000 || elapsedMs >= 2_500) {
            System.exit(3);
        }
        if (!err.toLowerCase().contains("cancel")) {
            System.exit(3);
        }
        if (!"1".equals(followup.get())) {
            System.exit(3);
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
        }
        /*
         * A failed autocommit statement leaves the connection abort-only.
         * That is a separate scenario. Defaults, truncate, and vacuum run
         * on a new connection, as the duplicate-key check already does.
         *
         * CREATE TABLE ... PRIMARY KEY is the table plus an index build in
         * one transaction. A conflicting index name fails the index step
         * and must roll the table back with it.
         */
        try (DBReference conn = open()) {
            conn.execute("create index bt_bad_pkey on bt_pk (id)");
            try {
                conn.execute("create table bt_bad (id int4 primary key)");
                System.out.println("BATTLE CHILD conflicting pkey index accepted");
                System.exit(3);
            } catch (ExecutionException ex) {
                System.out.println("BATTLE CHILD index step: " + ex.getMessage());
            }
        }
        try (DBReference conn = open()) {
            try {
                conn.execute("select id from bt_bad");
                System.out.println("BATTLE CHILD rolled-back table still exists");
                System.exit(3);
            } catch (ExecutionException ex) {
                String msg = ex.getMessage() == null ? "" : ex.getMessage();
                System.out.println("BATTLE CHILD index rollback: " + msg);
                if (!msg.contains("not found") && !msg.contains("does not exist")) {
                    System.exit(3);
                }
            }
        }
        try (DBReference conn = open()) {
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

    /**
     * Two inits in one process used to multiply the xid prefetch
     * (8 → 8192 → 8388608) and persist that high-water. The next boot's
     * first commit, here a DROP INDEX, then read pg_log past EOF, DBWriter
     * FATAL'd, and shutdown joined SyncWriter forever.
     */
    private static void prefetchDrop(String mode, String datadir) throws Exception {
        boot(mode, datadir);
        ensureDatabase();
        try (DBReference conn = open()) {
            conn.execute("create table bt_prefetch (id int4)");
            conn.execute("create index bt_prefetch_hash on bt_prefetch using hash (id)");
            conn.execute("insert into bt_prefetch (id) values (1)");
        }
        shutdown(mode);
        boot(mode, datadir);
        try (DBReference conn = open()) {
            conn.execute("insert into bt_prefetch (id) values (2)");
        }
        shutdown(mode);
        boot(mode, datadir);
        try (DBReference conn = open()) {
            conn.execute("drop index bt_prefetch_hash");
            conn.execute("insert into bt_prefetch (id) values (3)");
            int n = count(conn, "select id from bt_prefetch");
            System.out.println("BATTLE CHILD prefetch-drop rows=" + n);
            if (n != 3) {
                throw new IllegalStateException("rows=" + n);
            }
        }
        shutdown(mode);
        System.out.println("BATTLE CHILD prefetch-drop ok");
    }

    /**
     * Concurrent autocommit inserts, each on its own connection. The first
     * insert used to extend the heap with ReadBuffer(P_NEW) while another
     * connection was in PerformAllocation, which asserted that it alone had
     * grown the file.
     */
    private static void freespaceExtend(String mode, String datadir) throws Exception {
        boot(mode, datadir);
        ensureDatabase();
        try (DBReference conn = open()) {
            conn.execute("create table bt_fs (id int4, payload varchar(200))");
        }
        int writers = 6;
        int perWriter = 40;
        CountDownLatch start = new CountDownLatch(1);
        AtomicReference<Throwable> failure = new AtomicReference<>();
        Thread[] threads = new Thread[writers];
        for (int w = 0; w < writers; w++) {
            int base = w * perWriter;
            threads[w] = new Thread(() -> {
                try {
                    if (!start.await(15, TimeUnit.SECONDS)) {
                        throw new IllegalStateException("start");
                    }
                    try (DBReference conn = open()) {
                        for (int i = 0; i < perWriter; i++) {
                            conn.execute("insert into bt_fs (id, payload) values ("
                                    + (base + i) + ", '" + "x".repeat(180) + "')");
                        }
                    }
                } catch (Throwable t) {
                    failure.compareAndSet(null, t);
                }
            }, "bt-fs-" + w);
            threads[w].start();
        }
        start.countDown();
        for (Thread thread : threads) {
            thread.join(60_000);
        }
        for (Thread thread : threads) {
            if (thread.isAlive()) {
                System.out.println("BATTLE CHILD freespace-extend hung");
                System.exit(4);
            }
        }
        if (failure.get() != null) {
            failure.get().printStackTrace(System.out);
            System.exit(1);
        }
        try (DBReference conn = open()) {
            int n = count(conn, "select id from bt_fs");
            System.out.println("BATTLE CHILD freespace-extend rows=" + n);
            if (n != writers * perWriter) {
                throw new IllegalStateException("rows=" + n);
            }
        }
        shutdown(mode);
        System.out.println("BATTLE CHILD freespace-extend ok");
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
