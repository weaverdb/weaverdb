package org.weaverdb.battle;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.time.Instant;
import java.util.ArrayList;
import java.util.Date;
import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;
import org.junit.jupiter.api.Assertions;
import org.weaverdb.DBReference;
import org.weaverdb.ExecutionException;
import org.weaverdb.FunctionInstaller;
import org.weaverdb.Input;
import org.weaverdb.Output;
import org.weaverdb.Statement;
import org.weaverdb.TransactionSequence;

/**
 * Binding-agnostic scenarios. JNI and FFM tests call the same methods after
 * their own initializer has started the engine, so failures can be compared.
 */
public final class BattleScenarios {

    public static final String DB = "bttest";

    private BattleScenarios() {
    }

    @FunctionalInterface
    public interface ScenarioBody {
        void run() throws Exception;
    }

    public static final class Scenario {
        private final String name;
        private final ScenarioBody body;

        Scenario(String name, ScenarioBody body) {
            this.name = name;
            this.body = body;
        }

        public String name() {
            return name;
        }

        public void run() throws Exception {
            System.out.println("BATTLE BEGIN " + name);
            body.run();
            System.out.println("BATTLE PASS " + name);
        }
    }

    public static List<Scenario> core() {
        List<Scenario> all = new ArrayList<>();
        all.add(scenario("catalog and version", BattleScenarios::catalogAndVersion));
        all.add(scenario("ddl create alter drop", BattleScenarios::ddl));
        all.add(scenario("insert update delete parameters", BattleScenarios::crud));
        all.add(scenario("scalar types and nulls", BattleScenarios::scalarTypes));
        all.add(scenario("dates", BattleScenarios::dates));
        all.add(scenario("instant", BattleScenarios::instants));
        all.add(scenario("binary bytea and blob", BattleScenarios::binary));
        all.add(scenario("transactions commit rollback isolation", BattleScenarios::transactions));
        all.add(scenario("procedures and transaction sequence", BattleScenarios::procedures));
        all.add(scenario("indexes constraints truncate", BattleScenarios::indexes));
        all.add(scenario("query features", BattleScenarios::queries));
        all.add(scenario("schema view sequence inheritance", BattleScenarios::schemaViewSequence));
        all.add(scenario("java function", BattleScenarios::javaFunction));
        all.add(scenario("errors and invalid input", BattleScenarios::errors));
        all.add(scenario("resource cleanup", BattleScenarios::cleanup));
        all.add(scenario("second database and helper", BattleScenarios::secondDatabaseAndHelper));
        all.add(scenario("optional sql probes", BattleScenarios::probes));
        return all;
    }

    public static List<Scenario> stress() {
        List<Scenario> all = new ArrayList<>();
        all.add(scenario("streaming large values", BattleScenarios::streaming));
        all.add(scenario("bulk insert index update delete", BattleScenarios::bulk));
        all.add(scenario("connection and statement churn", BattleScenarios::churn));
        all.add(scenario("concurrent connections", BattleScenarios::concurrentConnections));
        all.add(scenario("readers and writers", BattleScenarios::readersAndWriters));
        all.add(scenario("transaction soak", BattleScenarios::soak));
        return all;
    }

    public static void ensureDatabase() throws Exception {
        try (DBReference conn = open("template1")) {
            try {
                conn.execute("create database " + DB);
                System.out.println("BATTLE created database " + DB);
            } catch (ExecutionException ex) {
                System.out.println("BATTLE create database " + DB + ": " + ex.getMessage());
            }
        }
        try (DBReference conn = open(DB)) {
            System.out.println("BATTLE BINDING " + conn.getClass().getName());
        }
    }

    public static DBReference open() {
        return open(DB);
    }

    public static DBReference open(String database) {
        DBReference conn = DBReference.connect(database);
        if (conn == null || !conn.isValid()) {
            throw new IllegalStateException("connect(" + database + ") returned " + conn);
        }
        return conn;
    }

    private static Scenario scenario(String name, ScenarioBody body) {
        return new Scenario(name, body);
    }

    private static void catalogAndVersion() throws Exception {
        try (DBReference conn = open("template1")) {
            boolean sawTemplate = false;
            try (Statement s = conn.statement("select datname from pg_database")) {
                Output<String> name = s.linkOutput(1, String.class);
                s.execute();
                while (s.fetch()) {
                    if ("template1".equals(name.get()) || DB.equals(name.get())) {
                        sawTemplate = true;
                    }
                }
            }
            Assertions.assertTrue(sawTemplate, "pg_database did not list a known database");
            probe("version()", () -> {
                String version = scalarString(conn, "select version()");
                System.out.println("BATTLE version " + version);
                Assertions.assertNotNull(version);
                Assertions.assertFalse(version.isBlank());
            });
        }
        try (DBReference conn = open()) {
            Assertions.assertTrue(conn.isValid());
            Assertions.assertEquals(1, scalarNumber(conn, "select 1"));
        }
    }

    private static void ddl() throws Exception {
        try (DBReference conn = open()) {
            recreate(conn, "bt_ddl", "id int4, name varchar(64)");
            conn.execute("insert into bt_ddl (id, name) values (1, 'orig')");
            conn.execute("alter table bt_ddl add column extra varchar(32)");
            Assertions.assertNull(scalarString(conn, "select extra from bt_ddl where id = 1"));
            conn.execute("create index bt_ddl_id on bt_ddl (id)");
            conn.execute("drop index bt_ddl_id");
            conn.execute("drop table bt_ddl");
        }
        try (DBReference conn = open()) {
            conn.execute("create table bt_ddl2 (id int4, extra varchar(32))");
            try {
                conn.execute("alter table bt_ddl2 drop column extra restrict");
                System.out.println("BATTLE drop column ok");
            } catch (ExecutionException ex) {
                System.out.println("BATTLE drop column: " + ex.getMessage());
            }
            ExecutionException missing = Assertions.assertThrows(ExecutionException.class,
                    () -> conn.execute("select id from bt_ddl"));
            Assertions.assertNotNull(missing.getMessage());
        }
    }

    private static void crud() throws Exception {
        try (DBReference conn = open()) {
            recreate(conn, "bt_crud", "id int4, name varchar(128), qty int4");
            try (Statement s = conn.statement(
                    "insert into bt_crud (id, name, qty) values ($id, $name, $qty)")) {
                Input<Integer> id = s.linkInput("id", Integer.class);
                Input<String> name = s.linkInput("name", String.class);
                Input<Integer> qty = s.linkInput("qty", Integer.class);
                id.set(1);
                name.set("O'Brien");
                qty.set(3);
                Assertions.assertEquals(1, s.execute());
                id.set(2);
                name.set("héllo 你好");
                qty.set(9);
                Assertions.assertEquals(1, s.execute());
                id.set(3);
                name.set("");
                qty.set(0);
                Assertions.assertEquals(1, s.execute());
                // A null int4 is process-fatal on the FFM upcall, so it lives in
                // the isolated null-int child. A null varchar is probed here.
                id.set(4);
                name.set(null);
                qty.set(4);
                Assertions.assertEquals(1, s.execute());
            }
            try (Statement s = conn.statement(
                    "select name, qty from bt_crud where id = $id")) {
                Input<Integer> id = s.linkInput("id", Integer.class);
                Output<String> name = s.linkOutput(1, String.class);
                Output<Integer> qty = s.linkOutput(2, Integer.class);
                id.set(1);
                s.execute();
                Assertions.assertTrue(s.fetch());
                Assertions.assertEquals("O'Brien", name.get());
                Assertions.assertEquals(3, qty.get());
                Assertions.assertFalse(s.fetch());
                id.set(2);
                s.execute();
                Assertions.assertTrue(s.fetch());
                Assertions.assertEquals("héllo 你好", name.get());
                id.set(3);
                s.execute();
                Assertions.assertTrue(s.fetch());
                Assertions.assertEquals("", name.get());
                id.set(4);
                s.execute();
                Assertions.assertTrue(s.fetch());
                Assertions.assertNull(name.get());
                Assertions.assertEquals(4, qty.get());
            }
            try (Statement s = conn.statement("update bt_crud set qty = $qty where name = $name")) {
                s.linkInput("qty", Integer.class).set(11);
                s.linkInput("name", String.class).set("O'Brien");
                Assertions.assertEquals(1, s.execute());
            }
            Assertions.assertEquals(11, scalarNumber(conn, "select qty from bt_crud where id = 1"));
            try (Statement s = conn.statement("delete from bt_crud where id = $id")) {
                s.linkInput("id", Integer.class).set(2);
                Assertions.assertEquals(1, s.execute());
                s.linkInput("id", Integer.class).set(999);
                Assertions.assertEquals(0, s.execute());
            }
            Assertions.assertEquals(3, fetchCount(conn, "select id from bt_crud"));
            try (Statement s = conn.statement("select id, name from bt_crud where id = 1")) {
                s.execute();
                Output<Integer> id = s.linkOutput(1, Integer.class);
                Output<String> name = s.linkOutput(2, String.class);
                Assertions.assertTrue(s.fetch());
                Assertions.assertEquals(1, id.get());
                Assertions.assertEquals("O'Brien", name.get());
                Assertions.assertNotNull(name.getName());
            }
        }
    }

    private static void scalarTypes() throws Exception {
        try (DBReference conn = open()) {
            // char(1) is omitted here. The FFM binding writes a Java char (2 bytes)
            // into the 1-byte native slot and that IndexOutOfBoundsException is fatal
            // to the upcall. See the isolated char-bind child.
            recreate(conn, "bt_types",
                    "si int2, i int4, bi int8, r float4, d float8, b bool, t text, v varchar(32)");
            try (Statement s = conn.statement(
                    "insert into bt_types (si, i, bi, r, d, b, t, v) values ($si, $i, $bi, $r, $d, $b, $t, $v)")) {
                s.linkInput("si", Integer.class).set(-32768);
                s.linkInput("i", Integer.class).set(Integer.MIN_VALUE);
                s.linkInput("bi", Long.class).set(Long.MAX_VALUE);
                s.linkInput("r", Float.class).set(1.5f);
                s.linkInput("d", Double.class).set(-2.25d);
                s.linkInput("b", Boolean.class).set(Boolean.TRUE);
                s.linkInput("t", String.class).set("text-value");
                s.linkInput("v", String.class).set("varchar-value");
                s.execute();
            }
            try (Statement s = conn.statement(
                    "select si, i, bi, r, d, b, t, v from bt_types")) {
                Output<Integer> si = s.linkOutput(1, Integer.class);
                Output<Integer> i = s.linkOutput(2, Integer.class);
                Output<Long> bi = s.linkOutput(3, Long.class);
                Output<Float> r = s.linkOutput(4, Float.class);
                Output<Double> d = s.linkOutput(5, Double.class);
                Output<Boolean> b = s.linkOutput(6, Boolean.class);
                Output<String> t = s.linkOutput(7, String.class);
                Output<String> v = s.linkOutput(8, String.class);
                s.execute();
                Assertions.assertTrue(s.fetch());
                Assertions.assertEquals(Integer.valueOf(-32768), si.get());
                Assertions.assertEquals(Integer.MIN_VALUE, i.get());
                Assertions.assertEquals(Long.MAX_VALUE, bi.get());
                Assertions.assertEquals(1.5f, r.get(), 0.0001f);
                Assertions.assertEquals(-2.25d, d.get(), 0.0000001d);
                Assertions.assertEquals(Boolean.TRUE, b.get());
                Assertions.assertEquals("text-value", t.get());
                Assertions.assertEquals("varchar-value", v.get());
            }
            try (Statement s = conn.statement(
                    "insert into bt_types (si, i, bi, r, d, b, t, v) values ($si, $i, $bi, $r, $d, $b, $t, $v)")) {
                s.linkInput("si", Integer.class).set(32767);
                s.linkInput("i", Integer.class).set(Integer.MAX_VALUE);
                s.linkInput("bi", Long.class).set(Long.MIN_VALUE);
                s.linkInput("r", Float.class).set(0f);
                s.linkInput("d", Double.class).set(0d);
                s.linkInput("b", Boolean.class).set(Boolean.FALSE);
                s.linkInput("t", String.class).set(null);
                s.linkInput("v", String.class).set(null);
                s.execute();
            }
            Assertions.assertEquals(2, fetchCount(conn, "select i from bt_types"));
            probe("float NaN", () -> {
                recreate(conn, "bt_nan", "r float4, d float8");
                try (Statement s = conn.statement("insert into bt_nan (r, d) values ($r, $d)")) {
                    s.linkInput("r", Float.class).set(Float.NaN);
                    s.linkInput("d", Double.class).set(Double.POSITIVE_INFINITY);
                    s.execute();
                }
                try (Statement s = conn.statement("select r, d from bt_nan")) {
                    Output<Float> r = s.linkOutput(1, Float.class);
                    Output<Double> d = s.linkOutput(2, Double.class);
                    s.execute();
                    Assertions.assertTrue(s.fetch());
                    Assertions.assertTrue(Float.isNaN(r.get()), "float NaN round-trip");
                    Assertions.assertEquals(Double.POSITIVE_INFINITY, d.get());
                }
            });
        }
    }

    private static void dates() throws Exception {
        long millis = 1_700_000_000_000L;
        Date when = new Date(millis);
        try (DBReference conn = open()) {
            recreate(conn, "bt_dates", "ts timestamp, dt datetime, d date");
            try (Statement s = conn.statement(
                    "insert into bt_dates (ts, dt, d) values ($ts, $dt, $d)")) {
                s.linkInput("ts", Date.class).set(when);
                s.linkInput("dt", Date.class).set(when);
                s.linkInput("d", Date.class).set(when);
                s.execute();
            }
            try (Statement s = conn.statement("select ts, dt, d from bt_dates")) {
                Output<Date> ts = s.linkOutput(1, Date.class);
                Output<Date> dt = s.linkOutput(2, Date.class);
                Output<Date> d = s.linkOutput(3, Date.class);
                s.execute();
                Assertions.assertTrue(s.fetch());
                assertWithin(when, ts.get(), "timestamp", 2_000L);
                assertWithin(when, dt.get(), "datetime", 2_000L);
                assertWithin(when, d.get(), "date", 24L * 60L * 60L * 1000L);
                System.out.println("BATTLE date delta ms ts=" + delta(when, ts.get())
                        + " dt=" + delta(when, dt.get())
                        + " d=" + delta(when, d.get()));
            }
        }
    }

    private static void instants() throws Exception {
        long millis = 1_700_000_000_000L;
        try (DBReference conn = open()) {
            recreate(conn, "bt_inst", "ts timestamp");
            Instant instant = Instant.ofEpochMilli(millis);
            try (Statement s = conn.statement("insert into bt_inst (ts) values ($ts)")) {
                s.linkInput("ts", Instant.class).set(instant);
                s.execute();
            }
            try (Statement s = conn.statement("select ts from bt_inst")) {
                Output<Instant> out = s.linkOutput(1, Instant.class);
                s.execute();
                Assertions.assertTrue(s.fetch());
                Instant got = out.get();
                Assertions.assertNotNull(got);
                Assertions.assertTrue(Math.abs(got.toEpochMilli() - millis) < 2_000L,
                        "instant delta " + (got.toEpochMilli() - millis));
            }
        }
    }

    private static void binary() throws Exception {
        try (DBReference setup = open()) {
            recreate(setup, "bt_bin", "id int4, b bytea, bl blob");
            for (int size : new int[] {0, 1, 17, 256, 4096, 8192, 65536}) {
                byte[] payload = patterned(size);
                try (DBReference conn = open()) {
                    try (Statement s = conn.statement("delete from bt_bin")) {
                        s.execute();
                    }
                    try (Statement s = conn.statement("insert into bt_bin (id, b, bl) values ($id, $b, $bl)")) {
                        s.linkInput("id", Integer.class).set(size);
                        s.linkInput("b", byte[].class).set(payload);
                        s.linkInput("bl", byte[].class).set(payload);
                        s.execute();
                    }
                    try (Statement s = conn.statement("select b, bl from bt_bin where id = $id")) {
                        s.linkInput("id", Integer.class).set(size);
                        Output<byte[]> b = s.linkOutput(1, byte[].class);
                        Output<byte[]> bl = s.linkOutput(2, byte[].class);
                        s.execute();
                        Assertions.assertTrue(s.fetch());
                        Assertions.assertArrayEquals(payload, b.get(), "bytea size " + size);
                        Assertions.assertArrayEquals(payload, bl.get(), "blob size " + size);
                    }
                    System.out.println("BATTLE binary size " + size + " ok");
                } catch (ExecutionException ex) {
                    System.out.println("BATTLE binary size " + size + " rejected: " + ex.getMessage());
                    if (size <= 256) {
                        throw ex;
                    }
                }
            }
        }
    }

    private static void transactions() throws Exception {
        try (DBReference setup = open()) {
            recreate(setup, "bt_txn", "id int4, note varchar(32)");
        }
        try (DBReference writer = open()) {
            long before = writer.transaction();
            long id = writer.begin();
            Assertions.assertTrue(id > 0, "begin id " + id);
            Assertions.assertEquals(id, writer.transaction());
            writer.execute("insert into bt_txn (id, note) values (1, 'open')");
            try (DBReference reader = open()) {
                Assertions.assertEquals(0, fetchCount(reader, "select id from bt_txn"));
            }
            writer.commit();
            Assertions.assertEquals(0L, writer.transaction(), "transaction id after commit, before was " + before);
        }
        try (DBReference reader = open()) {
            Assertions.assertEquals(1, fetchCount(reader, "select id from bt_txn"));
            Assertions.assertEquals("open", scalarString(reader, "select note from bt_txn where id = 1"));
        }
        try (DBReference writer = open()) {
            writer.begin();
            writer.execute("insert into bt_txn (id, note) values (2, 'rolled')");
            writer.abort();
            Assertions.assertEquals(0L, writer.transaction());
        }
        try (DBReference reader = open()) {
            Assertions.assertEquals(1, fetchCount(reader, "select id from bt_txn"));
        }
        try (DBReference writer = open()) {
            writer.begin();
            writer.execute("create table bt_txn_ddl (id int4)");
            writer.abort();
            ExecutionException missing = Assertions.assertThrows(ExecutionException.class,
                    () -> writer.execute("select id from bt_txn_ddl"));
            System.out.println("BATTLE ddl rollback: " + missing.getMessage());
        }
        try (DBReference conn = open()) {
            ExecutionException nested = Assertions.assertThrows(ExecutionException.class, () -> {
                conn.begin();
                conn.begin();
            });
            System.out.println("BATTLE nested begin: " + nested.getMessage());
            quietRollback(conn);
        }
        try (DBReference conn = open()) {
            ExecutionException bare = Assertions.assertThrows(ExecutionException.class, conn::commit);
            System.out.println("BATTLE commit without begin: " + bare.getMessage());
        }
    }

    private static void procedures() throws Exception {
        try (DBReference conn = open()) {
            recreate(conn, "bt_proc", "id int4");
            conn.begin();
            conn.start();
            conn.execute("insert into bt_proc (id) values (1)");
            int inside = fetchCount(conn, "select id from bt_proc");
            conn.end();
            int afterEnd = fetchCount(conn, "select id from bt_proc");
            conn.commit();
            System.out.println("BATTLE procedure visibleInside=" + inside + " visibleAfterEnd=" + afterEnd);
            Assertions.assertEquals(1, afterEnd, "row should be visible after the procedure ends");
            try (DBReference other = open()) {
                Assertions.assertEquals(1, fetchCount(other, "select id from bt_proc"));
            }
            ExecutionException nested = Assertions.assertThrows(ExecutionException.class, () -> {
                conn.begin();
                conn.start();
                conn.start();
            });
            System.out.println("BATTLE nested procedure: " + nested.getMessage());
            quietRollback(conn);
        }
        try (DBReference conn = open()) {
            recreate(conn, "bt_seqtx", "id int4");
            try (TransactionSequence tx = new TransactionSequence(conn)) {
                try (TransactionSequence.Procedure proc = tx.start()) {
                    proc.execute("insert into bt_seqtx (id) values (4)");
                }
            }
            try (DBReference other = open()) {
                Assertions.assertEquals(1, fetchCount(other, "select id from bt_seqtx"));
            }
        }
    }

    private static void indexes() throws Exception {
        try (DBReference conn = open()) {
            recreate(conn, "bt_idx", "id int4, name varchar(32)");
            conn.execute("insert into bt_idx (id, name) values (1, 'a')");
            conn.execute("insert into bt_idx (id, name) values (2, 'b')");
            conn.execute("insert into bt_idx (id, name) values (3, 'c')");
            conn.execute("create index bt_idx_id on bt_idx using btree (id)");
            Assertions.assertEquals("b", scalarString(conn, "select name from bt_idx where id = 2"));
            conn.execute("create unique index bt_idx_name on bt_idx (name)");
            ExecutionException dup;
            try (DBReference bad = open()) {
                dup = Assertions.assertThrows(ExecutionException.class,
                        () -> bad.execute("insert into bt_idx (id, name) values (4, 'b')"));
            }
            System.out.println("BATTLE unique violation: " + dup.getMessage());
            Assertions.assertEquals(3, fetchCount(conn, "select id from bt_idx"));
            conn.execute("drop index bt_idx_name");
            conn.execute("drop index bt_idx_id");
            System.out.println("BATTLE btree indexes dropped");
        }
    }

    private static void queries() throws Exception {
        try (DBReference conn = open()) {
            recreate(conn, "bt_q", "id int4, name varchar(16), qty int4");
            recreate(conn, "bt_q2", "id int4, qty int4");
            conn.execute("insert into bt_q (id, name, qty) values (1, 'ab', 2)");
            conn.execute("insert into bt_q (id, name, qty) values (2, 'abc', 5)");
            conn.execute("insert into bt_q (id, name, qty) values (3, 'zz', 5)");
            conn.execute("insert into bt_q2 (id, qty) values (1, 10)");
            conn.execute("insert into bt_q2 (id, qty) values (2, 20)");
            Assertions.assertEquals(12, scalarNumber(conn, "select sum(qty) from bt_q"));
            Assertions.assertEquals(1, scalarNumber(conn, "select min(id) from bt_q"));
            Assertions.assertEquals(3, scalarNumber(conn, "select max(id) from bt_q"));
            Assertions.assertEquals(3, scalarNumber(conn, "select count(*) from bt_q"));
            List<Integer> desc = new ArrayList<>();
            try (Statement s = conn.statement("select id from bt_q order by id desc limit 2")) {
                Output<Integer> id = s.linkOutput(1, Integer.class);
                s.execute();
                while (s.fetch()) {
                    desc.add(id.get());
                }
            }
            Assertions.assertEquals(List.of(3, 2), desc);
            Assertions.assertEquals(2, fetchCount(conn, "select id from bt_q where name like 'ab%'"));
            Assertions.assertEquals(2, fetchCount(conn,
                    "select bt_q.id from bt_q, bt_q2 where bt_q.id = bt_q2.id"));
            Assertions.assertEquals(4, scalarNumber(conn, "select id + 1 from bt_q where id = 3"));
            try (Statement s = conn.statement(
                    "select name from bt_q group by name order by name")) {
                Output<String> name = s.linkOutput(1, String.class);
                s.execute();
                List<String> names = new ArrayList<>();
                while (s.fetch()) {
                    names.add(name.get());
                }
                Assertions.assertEquals(List.of("ab", "abc", "zz"), names);
            }
            ByteArrayOutputStream explain = new ByteArrayOutputStream();
            conn.setStandardOutput(explain);
            conn.stream("explain select * from bt_q where id = 1");
            String plan = explain.toString(StandardCharsets.UTF_8);
            System.out.println("BATTLE explain bytes=" + plan.length());
            Assertions.assertFalse(plan.isBlank(), "explain produced no output: [" + plan + "]");
        }
    }

    private static void schemaViewSequence() throws Exception {
        try (DBReference conn = open()) {
            quiet(DB, "drop table btsch/item");
            quiet(DB, "drop schema btsch");
            conn.execute("create schema btsch");
            conn.execute("create table btsch/item (id int4, name varchar(16))");
            conn.execute("insert into btsch/item (id, name) values (1, 'scoped')");
            Assertions.assertEquals("scoped", scalarString(conn, "select name from btsch/item where id = 1"));
            conn.execute("drop table btsch/item");
            conn.execute("drop schema btsch");

            recreate(conn, "bt_base", "id int4, name varchar(16)");
            conn.execute("insert into bt_base (id, name) values (1, 'viewed')");
            quiet(DB, "drop view bt_v");
            conn.execute("create view bt_v as select id, name from bt_base");
            Assertions.assertEquals("viewed", scalarString(conn, "select name from bt_v where id = 1"));
            conn.execute("drop view bt_v");

            quiet(DB, "drop sequence bt_seq");
            conn.execute("create sequence bt_seq increment 1");
            long first = scalarNumber(conn, "select nextval('bt_seq')");
            long second = scalarNumber(conn, "select nextval('bt_seq')");
            System.out.println("BATTLE sequence " + first + " then " + second);
            Assertions.assertEquals(first + 1, second);
            conn.execute("drop sequence bt_seq");

            quiet(DB, "drop table bt_child");
            quiet(DB, "drop table bt_parent");
            conn.execute("create table bt_parent (id int4)");
            conn.execute("create table bt_child (extra varchar(8)) inherits (bt_parent)");
            conn.execute("insert into bt_child (id, extra) values (7, 'kid')");
            // This server is PostgreSQL 7.0: a plain parent name scans only
            // that table. Children are included by the trailing '*' form.
            Assertions.assertEquals(7, scalarNumber(conn, "select id from bt_parent* where id = 7"));
            Assertions.assertEquals("kid", scalarString(conn, "select extra from bt_child where id = 7"));
        }
    }

    private static void javaFunction() throws Exception {
        try (DBReference conn = open()) {
            quiet(DB, "drop function bt_add(int4, int4)");
            quiet(DB, "drop function bt_ident(varchar)");
            FunctionInstaller installer = new FunctionInstaller(conn);
            installer.installFunction("bt_add", MethodHandles.lookup().findStatic(
                    BattleFunctions.class, "add", MethodType.methodType(int.class, int.class, int.class)));
            installer.installFunction("bt_ident", MethodHandles.lookup().findStatic(
                    BattleFunctions.class, "ident", MethodType.methodType(String.class, String.class)));
            Assertions.assertEquals(42, scalarNumber(conn, "select bt_add(20, 22)"));
            try (Statement s = conn.statement("select bt_ident($v)")) {
                s.linkInput("v", String.class).set("héllo");
                Output<String> out = s.linkOutput(1, String.class);
                s.execute();
                Assertions.assertTrue(s.fetch());
                Assertions.assertEquals("héllo", out.get());
                s.linkInput("v", String.class).set(null);
                s.execute();
                Assertions.assertTrue(s.fetch());
                Assertions.assertNull(out.get());
            }
        }
    }

    private static void errors() throws Exception {
        try (DBReference conn = open()) {
            ExecutionException bad = Assertions.assertThrows(ExecutionException.class,
                    () -> conn.execute("select * from bt_no_such_table"));
            Assertions.assertNotNull(bad.getMessage());
            Assertions.assertFalse(bad.getMessage().isBlank());
            System.out.println("BATTLE missing table: " + bad.getMessage());
        }
        try (DBReference conn = open()) {
            ExecutionException syntax = Assertions.assertThrows(ExecutionException.class,
                    () -> conn.execute("this is not sql"));
            System.out.println("BATTLE bad sql: " + syntax.getMessage());
        }
        try (DBReference conn = open()) {
            Throwable empty = Assertions.assertThrows(Exception.class, () -> conn.execute(""));
            System.out.println("BATTLE empty sql: " + empty.getClass().getSimpleName() + " " + empty.getMessage());
        }
        try (DBReference conn = open()) {
            Throwable nul = Assertions.assertThrows(Exception.class, () -> conn.statement(null));
            System.out.println("BATTLE null sql: " + nul.getClass().getSimpleName() + " " + nul.getMessage());
        }
        try (DBReference conn = open()) {
            recreate(conn, "bt_mis", "name varchar(16)");
            conn.execute("insert into bt_mis (name) values ('abc')");
            ExecutionException mismatch = Assertions.assertThrows(ExecutionException.class, () -> {
                try (Statement s = conn.statement("select name from bt_mis")) {
                    Output<Integer> badCol = s.linkOutput(1, Integer.class);
                    s.execute();
                    s.fetch();
                    badCol.get();
                }
            });
            System.out.println("BATTLE type mismatch: " + mismatch.getMessage());
        }
        DBReference missing = DBReference.connect("bt_no_such_database");
        System.out.println("BATTLE missing database connect=" + (missing == null ? "null" : missing.getClass().getName()));
        if (missing != null) {
            Assertions.assertFalse(missing.isValid(), "invalid database produced a live connection");
            missing.close();
        }
        DBReference user = DBReference.connect("nosuch", "nosuch", DB);
        System.out.println("BATTLE bad user connect=" + (user == null ? "null" : user.getClass().getName() + " valid=" + user.isValid()));
        Assertions.assertTrue(user == null || !user.isValid(), "bad user produced a live connection");
        if (user != null) {
            user.close();
        }
        try (DBReference conn = open()) {
            ByteArrayOutputStream err = new ByteArrayOutputStream();
            conn.setStandardOutput(err);
            ExecutionException streamed = Assertions.assertThrows(ExecutionException.class,
                    () -> conn.stream("select * from bt_no_such_table"));
            System.out.println("BATTLE stream error: " + streamed.getMessage()
                    + " out=" + err.toString(StandardCharsets.UTF_8));
        }
    }

    private static void cleanup() throws Exception {
        try (DBReference conn = open()) {
            recreate(conn, "bt_close", "id int4");
            conn.execute("insert into bt_close (id) values (1)");
            Statement stmt = conn.statement("select id from bt_close");
            Output<Integer> id = stmt.linkOutput(1, Integer.class);
            stmt.execute();
            Assertions.assertTrue(stmt.fetch());
            Assertions.assertEquals(1, id.get());
            Assertions.assertFalse(stmt.fetch());
            stmt.close();
            stmt.close();
            Assertions.assertFalse(stmt.fetch(), "fetch after close should not return a row");
            conn.close();
            conn.close();
            Assertions.assertFalse(conn.isValid());
            Assertions.assertThrows(Exception.class, () -> conn.statement("select 1"));
        }
        for (int i = 0; i < 25; i++) {
            try (DBReference conn = open()) {
                Assertions.assertEquals(1, scalarNumber(conn, "select 1"));
            }
        }
    }

    private static void secondDatabaseAndHelper() throws Exception {
        try (DBReference meta = open("template1")) {
            try {
                meta.execute("create database bttest2");
            } catch (ExecutionException ex) {
                System.out.println("BATTLE create bttest2: " + ex.getMessage());
            }
        }
        try (DBReference conn = open("bttest2")) {
            quiet("bttest2", "drop table bt_iso");
            conn.execute("create table bt_iso (id int4)");
            conn.execute("insert into bt_iso (id) values (5)");
            Assertions.assertEquals(5, scalarNumber(conn, "select id from bt_iso"));
        }
        try (DBReference conn = open()) {
            ExecutionException isolated = Assertions.assertThrows(ExecutionException.class,
                    () -> conn.execute("select id from bt_iso"));
            System.out.println("BATTLE cross-db: " + isolated.getMessage());
        }
    }

    private static void probes() throws Exception {
        probe("savepoint", () -> {
            try (DBReference conn = open()) {
                recreate(conn, "bt_sp", "id int4");
                conn.begin();
                conn.execute("insert into bt_sp (id) values (1)");
                conn.execute("savepoint bt_mark");
                conn.execute("insert into bt_sp (id) values (2)");
                conn.execute("rollback to bt_mark");
                int n = fetchCount(conn, "select id from bt_sp");
                conn.commit();
                Assertions.assertEquals(1, n, "savepoint should undo the second insert");
            }
        });
        probe("reindex", () -> {
            try (DBReference conn = open()) {
                recreate(conn, "bt_re", "id int4");
                conn.execute("insert into bt_re (id) values (1)");
                conn.execute("create index bt_re_id on bt_re (id)");
                conn.execute("reindex index bt_re_id");
                Assertions.assertEquals(1, scalarNumber(conn, "select id from bt_re where id = 1"));
                conn.execute("drop index bt_re_id");
            }
        });
        probe("case expression", () -> {
            try (DBReference conn = open()) {
                Assertions.assertEquals(2, scalarNumber(conn, "select case when 1 = 1 then 2 else 3 end"));
            }
        });
        probe("copy to stdout", () -> {
            try (DBReference conn = open()) {
                recreate(conn, "bt_copy", "id int4");
                conn.execute("insert into bt_copy (id) values (8)");
                ByteArrayOutputStream out = new ByteArrayOutputStream();
                conn.setStandardOutput(out);
                conn.stream("copy bt_copy to stdout");
                String text = out.toString(StandardCharsets.UTF_8);
                System.out.println("BATTLE copy out=" + text);
                Assertions.assertTrue(text.contains("8"), text);
            }
        });
        probe("report user memory", () -> {
            try (DBReference conn = open()) {
                ByteArrayOutputStream out = new ByteArrayOutputStream();
                conn.setStandardOutput(out);
                conn.stream("report user memory");
                System.out.println("BATTLE memory report bytes=" + out.size());
            }
        });
    }

    private static void streaming() throws Exception {
        int size = prop("weaver.battle.stream", 256 * 1024);
        byte[] payload = patterned(size);
        try (DBReference conn = open()) {
            recreate(conn, "bt_stream", "id int4, data blob");
            try (Statement s = conn.statement("insert into bt_stream (id, data) values ($id, $data)")) {
                s.linkInput("id", Integer.class).set(1);
                s.linkInputStream("data", (byte[] value, OutputStream sink) -> {
                    sink.write(value);
                    sink.flush();
                }).set(payload);
                s.execute();
            }
            try (Statement s = conn.statement("select data from bt_stream where id = 1")) {
                Output<byte[]> data = s.linkOutputStream(1, (InputStream source) -> source.readAllBytes());
                Assertions.assertTrue(s.fetch());
                Assertions.assertArrayEquals(payload, data.get());
            }
            System.out.println("BATTLE stream bytes=" + size + " ok");
        }
    }

    private static void bulk() throws Exception {
        int rows = prop("weaver.battle.bulk", 800);
        try (DBReference conn = open()) {
            recreate(conn, "bt_bulk", "id int4, note varchar(64)");
            long start = System.nanoTime();
            conn.begin();
            try (Statement s = conn.statement("insert into bt_bulk (id, note) values ($id, $note)")) {
                Input<Integer> id = s.linkInput("id", Integer.class);
                Input<String> note = s.linkInput("note", String.class);
                for (int i = 0; i < rows; i++) {
                    id.set(i);
                    note.set("row-" + i);
                    s.execute();
                }
            }
            conn.commit();
            long ms = (System.nanoTime() - start) / 1_000_000L;
            Assertions.assertEquals(rows, fetchCount(conn, "select id from bt_bulk"));
            conn.execute("create index bt_bulk_id on bt_bulk (id)");
            Assertions.assertEquals("row-" + (rows / 2),
                    scalarString(conn, "select note from bt_bulk where id = " + (rows / 2)));
            conn.begin();
            long updated = conn.execute("update bt_bulk set note = 'x' where id < 10");
            conn.commit();
            Assertions.assertEquals(10, updated);
            conn.execute("delete from bt_bulk where id >= " + (rows - 5));
            Assertions.assertEquals(rows - 5, fetchCount(conn, "select id from bt_bulk"));
            System.out.println("BATTLE bulk rows=" + rows + " insertMs=" + ms);
            conn.execute("drop index bt_bulk_id");
        }
    }

    private static void churn() throws Exception {
        int loops = prop("weaver.battle.churn", 40);
        try (DBReference conn = open()) {
            recreate(conn, "bt_churn", "id int4");
            conn.execute("insert into bt_churn (id) values (1)");
            for (int i = 0; i < loops; i++) {
                try (Statement s = conn.statement("select id from bt_churn where id = $id")) {
                    s.linkInput("id", Integer.class).set(1);
                    Output<Integer> id = s.linkOutput(1, Integer.class);
                    s.execute();
                    Assertions.assertTrue(s.fetch());
                    Assertions.assertEquals(1, id.get());
                }
            }
        }
        for (int i = 0; i < loops; i++) {
            try (DBReference conn = open()) {
                try (Statement s = conn.statement("select id from bt_churn")) {
                    Output<Integer> id = s.linkOutput(1, Integer.class);
                    s.execute();
                    Assertions.assertTrue(s.fetch());
                    Assertions.assertEquals(1, id.get());
                }
            }
        }
        System.out.println("BATTLE churn loops=" + loops);
    }

    private static void concurrentConnections() throws Exception {
        int threads = prop("weaver.battle.threads", 6);
        int perThread = prop("weaver.battle.perThread", 40);
        try (DBReference conn = open()) {
            recreate(conn, "bt_cc", "id int4, note varchar(32)");
        }
        ExecutorService pool = Executors.newFixedThreadPool(threads);
        CountDownLatch ready = new CountDownLatch(threads);
        CountDownLatch go = new CountDownLatch(1);
        List<Future<?>> futures = new ArrayList<>();
        for (int t = 0; t < threads; t++) {
            int base = t * perThread;
            futures.add(pool.submit(() -> {
                ready.countDown();
                if (!go.await(30, TimeUnit.SECONDS)) {
                    throw new IllegalStateException("start timeout");
                }
                try (DBReference conn = open()) {
                    conn.begin();
                    try (Statement s = conn.statement("insert into bt_cc (id, note) values ($id, $note)")) {
                        Input<Integer> id = s.linkInput("id", Integer.class);
                        Input<String> note = s.linkInput("note", String.class);
                        for (int i = 0; i < perThread; i++) {
                            id.set(base + i);
                            note.set("t" + base + "-" + i);
                            s.execute();
                        }
                    }
                    conn.commit();
                }
                return null;
            }));
        }
        Assertions.assertTrue(ready.await(30, TimeUnit.SECONDS));
        go.countDown();
        for (Future<?> future : futures) {
            future.get(120, TimeUnit.SECONDS);
        }
        pool.shutdown();
        try (DBReference conn = open()) {
            Assertions.assertEquals(threads * perThread, fetchCount(conn, "select id from bt_cc"));
            Assertions.assertEquals(0, scalarNumber(conn, "select id from bt_cc where id = 0"));
        }
        System.out.println("BATTLE concurrent threads=" + threads + " perThread=" + perThread);
    }

    private static void readersAndWriters() throws Exception {
        try (DBReference conn = open()) {
            recreate(conn, "bt_rw", "id int4");
        }
        int writers = 4;
        int perWriter = 30;
        AtomicInteger reads = new AtomicInteger();
        AtomicReference<Throwable> failure = new AtomicReference<>();
        CountDownLatch start = new CountDownLatch(1);
        List<Thread> threads = new ArrayList<>();
        for (int w = 0; w < writers; w++) {
            int base = w * perWriter;
            threads.add(new Thread(() -> {
                try {
                    if (!start.await(15, TimeUnit.SECONDS)) {
                        throw new IllegalStateException("start");
                    }
                    try (DBReference conn = open()) {
                        for (int i = 0; i < perWriter; i++) {
                            conn.execute("insert into bt_rw (id) values (" + (base + i) + ")");
                        }
                    }
                } catch (Throwable t) {
                    failure.compareAndSet(null, t);
                }
            }, "bt-writer-" + w));
        }
        for (int r = 0; r < 2; r++) {
            threads.add(new Thread(() -> {
                try {
                    if (!start.await(15, TimeUnit.SECONDS)) {
                        throw new IllegalStateException("start");
                    }
                    try (DBReference conn = open()) {
                        for (int i = 0; i < 20; i++) {
                            fetchCount(conn, "select id from bt_rw");
                            reads.incrementAndGet();
                        }
                    }
                } catch (Throwable t) {
                    failure.compareAndSet(null, t);
                }
            }, "bt-reader-" + r));
        }
        threads.forEach(Thread::start);
        start.countDown();
        for (Thread thread : threads) {
            thread.join(60_000);
            if (thread.isAlive()) {
                failure.compareAndSet(null, new IllegalStateException(thread.getName() + " hung"));
            }
        }
        if (failure.get() != null) {
            if (failure.get() instanceof Exception ex) {
                throw ex;
            }
            throw new RuntimeException(failure.get());
        }
        try (DBReference conn = open()) {
            Assertions.assertEquals(writers * perWriter, fetchCount(conn, "select id from bt_rw"));
        }
        System.out.println("BATTLE readers completedCycles=" + reads.get());
    }

    private static void soak() throws Exception {
        int commits = prop("weaver.battle.soak", 200);
        long rssBefore = rssKb();
        try (DBReference conn = open()) {
            recreate(conn, "bt_soak", "id int4, note varchar(24)");
            for (int i = 0; i < commits; i++) {
                conn.begin();
                conn.execute("insert into bt_soak (id, note) values (" + i + ", 'c')");
                conn.commit();
            }
            int beforeAbort = fetchCount(conn, "select id from bt_soak");
            for (int i = 0; i < 40; i++) {
                conn.begin();
                conn.execute("insert into bt_soak (id, note) values (" + (10_000 + i) + ", 'a')");
                conn.abort();
            }
            Assertions.assertEquals(commits, beforeAbort);
            Assertions.assertEquals(commits, fetchCount(conn, "select id from bt_soak"));
            for (int i = 0; i < 50; i++) {
                conn.execute("update bt_soak set note = 'u' where id = " + (i % commits));
            }
            int second = 30;
            for (int i = 0; i < second; i++) {
                conn.begin();
                conn.execute("insert into bt_soak (id, note) values (" + (20_000 + i) + ", 'r')");
                conn.commit();
            }
            Assertions.assertEquals(commits + second, fetchCount(conn, "select id from bt_soak"));
            conn.execute("delete from bt_soak where id >= 20000");
            Assertions.assertEquals(commits, fetchCount(conn, "select id from bt_soak"));
            System.out.println("BATTLE soak rounds=2");
        }
        long rssAfter = rssKb();
        System.out.println("BATTLE soak commits=" + commits + " rssKbBefore=" + rssBefore + " rssKbAfter=" + rssAfter
                + " deltaKb=" + (rssAfter - rssBefore));
        if (rssBefore > 0 && rssAfter - rssBefore > 400_000) {
            Assertions.fail("RSS grew by more than 400MB during soak: " + (rssAfter - rssBefore) + "kB");
        }
    }

    /**
     * Run a child JVM. Exit 0 is success. {@code accepted} lists extra exit
     * codes that are clean (non-crash) outcomes the caller wants to assert on.
     */
    public static ChildResult launchChild(String mode, String scenario, int timeoutSeconds) throws Exception {
        Path root = Path.of(System.getProperty("weaver.repo.root", "..")).toAbsolutePath();
        Path initdb = root.resolve("build_test/mtpg/bin/initdb");
        Path lib = root.resolve("build_test/mtpg/lib");
        Assertions.assertTrue(Files.isExecutable(initdb), "missing initdb at " + initdb);
        Path work = Files.createTempDirectory("weaver-battle-");
        Path data = work.resolve("data");
        ProcessBuilder init = new ProcessBuilder(initdb.toString(), "-D", data.toString());
        init.redirectErrorStream(true);
        Process initProc = init.start();
        String initOut = readAll(initProc, 60);
        int initRc = initProc.waitFor();
        if (initRc != 0) {
            throw new IllegalStateException("initdb failed " + initRc + "\n" + initOut);
        }
        List<String> cmd = List.of(
                Path.of(System.getProperty("java.home"), "bin", "java").toString(),
                "--enable-native-access=ALL-UNNAMED",
                "-Djava.library.path=" + lib,
                "-cp", System.getProperty("java.class.path"),
                "org.weaverdb.battle.BattleChildMain",
                mode,
                data.toString(),
                scenario);
        ProcessBuilder pb = new ProcessBuilder(cmd);
        pb.environment().put("LD_LIBRARY_PATH", lib.toString());
        pb.redirectErrorStream(true);
        Process child = pb.start();
        String output = readAll(child, timeoutSeconds);
        boolean finished = child.waitFor(1, TimeUnit.SECONDS);
        int code;
        if (!finished) {
            child.destroyForcibly();
            child.waitFor(5, TimeUnit.SECONDS);
            code = 4;
            output = output + "\nBATTLE parent killed child after timeout";
        } else {
            code = child.exitValue();
        }
        System.out.println("BATTLE CHILD " + mode + " " + scenario + " exit=" + code);
        System.out.println(output);
        return new ChildResult(code, output);
    }

    /**
     * SIGKILL the child after it has committed a seed and waited out
     * {@code synctimeout}, while a later statement is still running.
     * A second process then checks the hard-synced rows.
     */
    public static ChildResult killAfterSync(String mode, String phase, int timeoutSeconds) throws Exception {
        Path root = Path.of(System.getProperty("weaver.repo.root", "..")).toAbsolutePath();
        Path initdb = root.resolve("build_test/mtpg/bin/initdb");
        Path lib = root.resolve("build_test/mtpg/lib");
        Assertions.assertTrue(Files.isExecutable(initdb), "missing initdb at " + initdb);
        Path work = Files.createTempDirectory("weaver-battle-kill-");
        Path data = work.resolve("data");
        ProcessBuilder init = new ProcessBuilder(initdb.toString(), "-D", data.toString());
        init.redirectErrorStream(true);
        Process initProc = init.start();
        String initOut = readAll(initProc, 60);
        int initRc = initProc.waitFor();
        if (initRc != 0) {
            throw new IllegalStateException("initdb failed " + initRc + "\n" + initOut);
        }
        String marker = "BATTLE KILLWINDOW " + phase;
        Process victim = startChild(mode, lib, data, "kill-" + phase);
        StringBuilder victimBuf = new StringBuilder();
        Thread pump = pumpInto(victim, victimBuf);
        long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(timeoutSeconds);
        while (victim.isAlive() && !snapshot(victimBuf).contains(marker)) {
            if (System.nanoTime() > deadline) {
                victim.destroyForcibly();
                victim.waitFor(5, TimeUnit.SECONDS);
                pump.join(2_000);
                String output = snapshot(victimBuf) + "\nBATTLE kill missed window";
                System.out.println(output);
                return new ChildResult(4, output);
            }
            Thread.sleep(30);
        }
        String soFar = snapshot(victimBuf);
        if (!victim.isAlive()) {
            pump.join(2_000);
            String output = snapshot(victimBuf) + "\nBATTLE child exited before kill window";
            System.out.println(output);
            return new ChildResult(victim.exitValue(), output);
        }
        if (!soFar.contains("[DBWriter]sync timeout 1000")) {
            victim.destroyForcibly();
            victim.waitFor(5, TimeUnit.SECONDS);
            pump.join(2_000);
            String output = snapshot(victimBuf) + "\nBATTLE synctimeout 1000 was not applied";
            System.out.println(output);
            return new ChildResult(3, output);
        }
        Thread.sleep(300);
        boolean killed = false;
        if (victim.isAlive()) {
            victim.destroyForcibly();
            killed = true;
        }
        victim.waitFor(5, TimeUnit.SECONDS);
        pump.join(2_000);
        String victimOut = snapshot(victimBuf);
        System.out.println("BATTLE KILL " + mode + " " + phase + " killed=" + killed);
        System.out.println(victimOut);

        Process recover = startChild(mode, lib, data, "kill-recover-" + phase);
        String recoverOut = readAll(recover, timeoutSeconds);
        boolean finished = recover.waitFor(1, TimeUnit.SECONDS);
        int code;
        if (!finished) {
            recover.destroyForcibly();
            recover.waitFor(5, TimeUnit.SECONDS);
            code = 4;
            recoverOut = recoverOut + "\nBATTLE recover timed out";
        } else {
            code = recover.exitValue();
        }
        String output = victimOut + "\n----- recover -----\n" + recoverOut + "\nBATTLE killed=" + killed;
        System.out.println("BATTLE RECOVER " + mode + " " + phase + " exit=" + code);
        System.out.println(recoverOut);
        return new ChildResult(code, output);
    }

    private static Process startChild(String mode, Path lib, Path data, String scenario) throws Exception {
        List<String> cmd = List.of(
                Path.of(System.getProperty("java.home"), "bin", "java").toString(),
                "--enable-native-access=ALL-UNNAMED",
                "-Djava.library.path=" + lib,
                "-cp", System.getProperty("java.class.path"),
                "org.weaverdb.battle.BattleChildMain",
                mode,
                data.toString(),
                scenario);
        ProcessBuilder pb = new ProcessBuilder(cmd);
        pb.environment().put("LD_LIBRARY_PATH", lib.toString());
        pb.redirectErrorStream(true);
        return pb.start();
    }

    private static Thread pumpInto(Process process, StringBuilder buf) {
        Thread pump = new Thread(() -> {
            try (InputStream in = process.getInputStream()) {
                byte[] block = new byte[4096];
                int n;
                while ((n = in.read(block)) >= 0) {
                    synchronized (buf) {
                        buf.append(new String(block, 0, n, StandardCharsets.UTF_8));
                    }
                }
            } catch (Exception ignored) {
            }
        }, "battle-kill-pump");
        pump.setDaemon(true);
        pump.start();
        return pump;
    }

    private static String snapshot(StringBuilder buf) {
        synchronized (buf) {
            return buf.toString();
        }
    }

    /**
     * Fresh initdb into a temp directory, then a child that checks the
     * bootstrap catalogs. A hung initdb is killed instead of waiting forever.
     */
    public static ChildResult launchBootstrap(String mode, int timeoutSeconds) throws Exception {
        Path root = Path.of(System.getProperty("weaver.repo.root", "..")).toAbsolutePath();
        Path initdb = root.resolve("build_test/mtpg/bin/initdb");
        Path lib = root.resolve("build_test/mtpg/lib");
        Assertions.assertTrue(Files.isExecutable(initdb), "missing initdb at " + initdb);
        Path work = Files.createTempDirectory("weaver-battle-initdb-");
        Path data = work.resolve("data");
        ProcessBuilder init = new ProcessBuilder("setsid", initdb.toString(), "-D", data.toString());
        init.redirectErrorStream(true);
        Process initProc = init.start();
        String initOut = readAll(initProc, 40);
        if (initProc.isAlive()) {
            long pid = initProc.pid();
            new ProcessBuilder("kill", "-KILL", "--", "-" + pid).start().waitFor(2, TimeUnit.SECONDS);
            initProc.destroyForcibly();
            initProc.waitFor(5, TimeUnit.SECONDS);
            throw new IllegalStateException("initdb timed out\n" + initOut);
        }
        int initRc = initProc.exitValue();
        if (initRc != 0) {
            throw new IllegalStateException("initdb failed " + initRc + "\n" + initOut);
        }
        Process child = startChild(mode, lib, data, "initdb-catalog");
        String output = readAll(child, timeoutSeconds);
        boolean finished = child.waitFor(1, TimeUnit.SECONDS);
        int code;
        if (!finished) {
            child.destroyForcibly();
            child.waitFor(5, TimeUnit.SECONDS);
            code = 4;
            output = output + "\nBATTLE parent killed child after timeout";
        } else {
            code = child.exitValue();
        }
        System.out.println("BATTLE CHILD " + mode + " initdb-catalog exit=" + code);
        System.out.println(output);
        return new ChildResult(code, output);
    }

    public static final class ChildResult {
        public final int code;
        public final String output;

        ChildResult(int code, String output) {
            this.code = code;
            this.output = output;
        }
    }

    private static String readAll(Process process, int timeoutSeconds) throws Exception {
        ByteArrayOutputStream buf = new ByteArrayOutputStream();
        Thread pump = new Thread(() -> {
            try (InputStream in = process.getInputStream()) {
                in.transferTo(buf);
            } catch (Exception ignored) {
            }
        }, "battle-pump");
        pump.start();
        process.waitFor(timeoutSeconds, TimeUnit.SECONDS);
        pump.join(2_000);
        return buf.toString(StandardCharsets.UTF_8);
    }

    private static void probe(String name, ScenarioBody body) throws Exception {
        try {
            body.run();
            System.out.println("BATTLE PROBE " + name + " OK");
        } catch (ExecutionException ex) {
            System.out.println("BATTLE PROBE " + name + " UNSUPPORTED " + ex.getMessage());
        }
    }

    private static void recreate(DBReference conn, String table, String columns) throws Exception {
        quiet(DB, "drop table " + table);
        conn.execute("create table " + table + " (" + columns + ")");
    }

    /**
     * Run SQL that is allowed to fail, on a connection that is closed
     * afterward. A failed autocommit statement leaves the connection in
     * {@code TRAN_ABORTONLY}; closing it aborts the stuck transaction.
     * Issuing another statement, including {@code abort()}, on that same
     * connection panics the engine.
     */
    private static void quiet(String database, String sql) {
        DBReference conn = null;
        try {
            conn = open(database);
            conn.execute(sql);
        } catch (Exception ignored) {
        } finally {
            if (conn != null) {
                try {
                    conn.close();
                } catch (Throwable ignored) {
                }
            }
        }
    }

    /**
     * Roll back a transaction opened with {@code begin()}. Do not use this
     * after an autocommit failure: {@code abort()} then another statement
     * panics in {@code DeferredTriggerBeginXact}. Close that connection
     * instead.
     */
    private static void quietRollback(DBReference conn) {
        try {
            conn.abort();
        } catch (Throwable ignored) {
        }
    }

    private static int fetchCount(DBReference conn, String sql) throws Exception {
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

    private static long scalarNumber(DBReference conn, String sql) throws Exception {
        ExecutionException last = null;
        for (Class<?> type : new Class<?>[] {Integer.class, Long.class, Double.class, String.class, Object.class}) {
            try (Statement s = conn.statement(sql)) {
                Output<?> out = s.linkOutput(1, type);
                s.execute();
                if (!s.fetch()) {
                    throw new ExecutionException("no row for " + sql);
                }
                Object value = out.get();
                if (value == null) {
                    throw new ExecutionException("null for " + sql);
                }
                if (value instanceof Number number) {
                    return number.longValue();
                }
                String text = value.toString().trim();
                int space = text.indexOf(' ');
                if (space > 0) {
                    text = text.substring(0, space);
                }
                if (text.endsWith(".0")) {
                    text = text.substring(0, text.length() - 2);
                }
                return Long.parseLong(text);
            } catch (ExecutionException ex) {
                last = ex;
                break;
            } catch (NumberFormatException ex) {
                last = new ExecutionException(ex.getMessage(), ex);
                break;
            }
        }
        throw new ExecutionException("no numeric binding for " + sql, last);
    }

    public static long scalarLong(DBReference conn, String sql) throws Exception {
        return scalarNumber(conn, sql);
    }

    private static String scalarString(DBReference conn, String sql) throws Exception {
        try (Statement s = conn.statement(sql)) {
            Output<String> out = s.linkOutput(1, String.class);
            s.execute();
            if (!s.fetch()) {
                return null;
            }
            return out.get();
        }
    }

    private static byte[] patterned(int size) {
        byte[] data = new byte[size];
        for (int i = 0; i < size; i++) {
            data[i] = (byte) (i * 31 + 7);
        }
        return data;
    }

    private static void assertWithin(Date expected, Date actual, String label, long toleranceMs) {
        Assertions.assertNotNull(actual, label);
        long gap = Math.abs(actual.getTime() - expected.getTime());
        Assertions.assertTrue(gap <= toleranceMs,
                label + " delta " + gap + "ms expected " + expected.getTime() + " got " + actual.getTime());
    }

    private static long delta(Date expected, Date actual) {
        if (actual == null || expected == null) {
            return Long.MIN_VALUE;
        }
        return actual.getTime() - expected.getTime();
    }

    private static long rssKb() {
        try {
            for (String line : Files.readAllLines(Path.of("/proc/self/status"))) {
                if (line.startsWith("VmRSS:")) {
                    String digits = line.replaceAll("[^0-9]", "");
                    return digits.isEmpty() ? -1L : Long.parseLong(digits);
                }
            }
        } catch (Exception ignored) {
            return -1L;
        }
        return -1L;
    }

    private static int prop(String key, int fallback) {
        String value = System.getProperty(key);
        if (value == null || value.isBlank()) {
            return fallback;
        }
        return Integer.parseInt(value);
    }
}
