

package driver.weaver;

import driver.weaver.BaseWeaverConnection.Statement;
import driver.weaver.ResultSet.Column;
import driver.weaver.ResultSet.Row;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.io.Writer;
import java.nio.channels.Channels;
import java.security.DigestOutputStream;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.time.Duration;
import java.util.Properties;
import java.util.Random;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;
import java.util.function.Consumer;
import java.util.stream.IntStream;
import java.util.stream.Stream;
import org.junit.jupiter.api.Assertions;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 *
 * @author myronscott
 */
public class JNITest {
    
    public JNITest() {
    }

    @org.junit.jupiter.api.BeforeAll
    public static void setUpClass() throws Exception {
        ProcessBuilder b = new ProcessBuilder("pwd");
        b.inheritIO();
        Process p = b.start();
        p.waitFor();
        b = new ProcessBuilder("tar","xvf", "../mtpgsql/src/mtpg.tar.bz2","-C","build");
        b.inheritIO();
        p = b.start();
        p.waitFor();
        b = new ProcessBuilder("rm","-rf", System.getProperty("user.dir") + "/build/testdb");
        b.inheritIO();
        p = b.start();
        p.waitFor();
        b = new ProcessBuilder("build/mtpg/bin/initdb","-D", System.getProperty("user.dir") + "/build/testdb");
        b.inheritIO();
        p = b.start();
        p.waitFor();
        b = new ProcessBuilder("build/mtpg/bin/postgres","-D", System.getProperty("user.dir") + "/build/testdb","template1");
        p = b.start();
        try (Writer w = p.outputWriter()) {
            w.append("create database test;\n").flush();
        }
        p.waitFor();
        b = new ProcessBuilder("cp","libweaver.dylib", System.getProperty("user.dir") + "/build/libs/");
        b.inheritIO();
        p = b.start();
        p.waitFor();
        Properties prop = new Properties();
        prop.setProperty("datadir", System.getProperty("user.dir") + "/build/testdb");
        prop.setProperty("allow_anonymous", "true");
        prop.setProperty("start_delay", "10");
        prop.setProperty("debuglevel", "DEBUG");
        prop.setProperty("stdlog", "TRUE");
        WeaverInitializer.initialize(prop);
    }

    @org.junit.jupiter.api.AfterAll
    public static void tearDownClass() throws Exception {
        WeaverInitializer.close(true);
    }

    @org.junit.jupiter.api.BeforeEach
    public void setUp() throws Exception {
    }

    @org.junit.jupiter.api.AfterEach
    public void tearDown() throws Exception {
    }
    
    @org.junit.jupiter.api.Test
    public void test() throws Exception {
        try (BaseWeaverConnection conn = BaseWeaverConnection.connectAnonymously("test")) {
            try (Statement s = conn.statement("select * from pg_database;")) {
                Output<String> b = s.linkOutput(1, String.class);
                Output<String> c = s.linkOutput(2, String.class);
                Output<String> d = s.linkOutput(3, String.class);
                Output<String> e = s.linkOutput(4, String.class);
                Output<String> f = s.linkOutput(5, String.class);
                System.out.println(s.execute());
                s.fetch();
                System.out.println(b.getName() + "=" + b.get());
                System.out.println(c.getName() + "=" + c.get());
                System.out.println(d.getName() + "=" + d.get());
                System.out.println(e.getName() + "=" + e.get());
                System.out.println(f.getName() + "=" + f.get());
            }
        }
    }    
    @org.junit.jupiter.api.Test
    public void testBadBind() throws Exception {
        try (BaseWeaverConnection conn = BaseWeaverConnection.connectAnonymously("test")) {
            try (Statement s = conn.statement("select * from pg_database;")) {
                Output<Integer> b = s.linkOutput(1, Integer.class);
                Output<Integer> c = s.linkOutput(2, Integer.class);
                Output<Integer> d = s.linkOutput(3, Integer.class);
                Output<Integer> e = s.linkOutput(4, Integer.class);
                Output<Integer> f = s.linkOutput(5, Integer.class);
                System.out.println(s.execute());
                s.fetch();
                System.out.println(b.getName() + "=" + b.get());
                System.out.println(c.getName() + "=" + c.get());
                System.out.println(d.getName() + "=" + d.get());
                System.out.println(e.getName() + "=" + e.get());
                System.out.println(f.getName() + "=" + f.get());
            } catch (ExecutionException we) {
                we.printStackTrace();
                // expected
            }
        }
    }
    
    @org.junit.jupiter.api.Test
    public void testListTables() throws Exception {
        try (BaseWeaverConnection conn = BaseWeaverConnection.connectAnonymously("test")) {
            try (Statement s = conn.statement("select * from pg_class;")) {
                Output<String> b = s.linkOutput(1, String.class);
                Output<String> c = s.linkOutput(2, String.class);
                Output<String> d = s.linkOutput(3, String.class);
                Output<String> e = s.linkOutput(4, String.class);
                Output<String> f = s.linkOutput(5, String.class);
                System.out.println(s.execute());
                Consumer<Output> process = (o)->{
                    if (o.getName() != null) {
                        try {
                            System.out.println(o.getName() + "=" + o.get());
                        } catch (ExecutionException ee) {

                        }
                    }
                };
                while (s.fetch()) {
                    process.accept(b);
                    process.accept(c);
                    process.accept(d);
                    process.accept(e);
                    process.accept(f);
                    System.out.println("+++++++");
                }
            } catch (ExecutionException we) {
                we.printStackTrace();
                // expected
            }
        }
    }
    
    @org.junit.jupiter.api.Test
    public void testBadCloseSequence() throws Exception {
        BaseWeaverConnection conn = BaseWeaverConnection.connectAnonymously("test");
        Statement s = conn.statement("select * from pg_class;");
        Output<String> b = s.linkOutput(1, String.class);
        Output<String> c = s.linkOutput(2, String.class);
        Output<String> d = s.linkOutput(3, String.class);
        Output<String> e = s.linkOutput(4, String.class);
        Output<String> f = s.linkOutput(5, String.class);
        System.out.println(s.execute());
        Consumer<Output> process = (o)->{
            if (o.getName() != null) {
                try {
                    System.out.println(o.getName() + "=" + o.get());
                } catch (ExecutionException ee) {
                    
                }
            }
        };
        while (s.fetch()) {
            process.accept(b);
            process.accept(c);
            process.accept(d);
            process.accept(e);
            process.accept(f);
            System.out.println("+++++++");
        }

        conn.close();
        s.close();
        conn.close();
        s.close();
    }
    
    @org.junit.jupiter.api.Test
    public void testInputs() throws Exception {
        try (BaseWeaverConnection conn = BaseWeaverConnection.connectAnonymously("test")) {
            try (Statement s = conn.statement("create schema fortune")) {
                s.execute();
            }
            try (Statement s = conn.statement("create table fortune/cookie (id int4, name varchar(128))")) {
                s.execute();
            }
            try (Statement s = conn.statement("insert into fortune/cookie (id, name) values ($id, $name)")) {
                Input<Integer> id = s.linkInput("id", Integer.class);
                Input<String> name = s.linkInput("name", String.class);
                id.set(1);
                name.set("Marcus");
                s.execute();
            }
            try (Statement s = conn.statement("select * from fortune/cookie where name = $name")) {
                Input<String> name = s.linkInput("name", String.class);
                name.set("Marcus");
                Output<Integer> id = s.linkOutput(1, Integer.class);
                name.set("Marcus");
                s.execute();
                if (s.fetch()) {
                    System.out.println("Marcus=" + id.get());
                }
            }
            try (Statement s = conn.statement("update fortune/cookie set id=$id where name = $name")) {
                Input<String> name = s.linkInput("name", String.class);
                name.set("Marcus");
                Input<Integer> id = s.linkInput("id", Integer.class);
                id.set(3);
                s.execute();
            }
            try (Statement s = conn.statement("select * from fortune/cookie where name = $name")) {
                s.linkInput("name", String.class).set("Marcus");
                Output<Integer> id = s.linkOutput(1, Integer.class);
                s.execute();
                if (s.fetch()) {
                    System.out.println("Marcus=" + id.get());
                }
            }
        }
    }
    
    @org.junit.jupiter.api.Test
    public void testPiping() throws Exception {
        ExecutorService feeder = Executors.newVirtualThreadPerTaskExecutor();
        try (BaseWeaverConnection conn = BaseWeaverConnection.connectAnonymously("test")) {
            conn.execute("create schema winter");
            conn.execute("create table winter/streaming (id int4, data blob, data2 blob, data3 blob, data4 blob)");
            try (Statement s = conn.statement("insert into winter/streaming (id, data, data2, data3, data4) values ($id, $bin, $binb, $binc, $bind)")) {
                s.linkInput("id", Integer.class).set(1);
                Input.Channel<String> df =  (value, sink)->{
                    try (DataOutputStream out = new DataOutputStream(Channels.newOutputStream(sink))) {
                        out.writeUTF(value);
                    }
                };
                s.linkInputChannel("bin", df).value("hello to the panda");
                s.linkInputChannel("binb", df).value("hello to the panda2");
                s.linkInputChannel("binc", df).value("hello to the panda3");
                s.linkInputChannel("bind", df).value("hello to the panda4");
                
                s.execute();
            }
            try (Statement s = conn.statement("select data from winter/streaming where id=$id")) {
                s.linkInput("id", Integer.class).set(1);
                Output<String> data = s.linkOutputChannel(1, (source)->{
                    try (DataInputStream in = new DataInputStream(Channels.newInputStream(source))) {
                        return in.readUTF();
                    }
                });
                s.execute();
                s.fetch();
                assertEquals("hello to the panda", data.value());
            }
            feeder.shutdown();
        }
    }
    
        @org.junit.jupiter.api.Test
    public void testNullOut() throws Exception {
        try (BaseWeaverConnection conn = BaseWeaverConnection.connectAnonymously("test")) {
            conn.execute("create schema nullcheck");
            conn.execute("create table nullcheck/nullcheck (nstring varchar(256), nint int4)");
            conn.execute("insert into nullcheck/nullcheck (nstring) values ('fun times')");
            try (Statement s = conn.statement("select nstring, nint from nullcheck/nullcheck")) {
                Output<String> r = s.linkOutput(1, String.class);
                Output<Integer> i = s.linkOutput(2, Integer.class);
                s.execute();
                assertTrue(s.fetch());
                Assertions.assertEquals("fun times", r.get());
                Assertions.assertNull(i.get());
            }
        }
    }
    
        @org.junit.jupiter.api.Test
    public void testStreamingType() throws Exception {
        ExecutorService service = Executors.newVirtualThreadPerTaskExecutor();
        try (BaseWeaverConnection conn = BaseWeaverConnection.connectAnonymously("test")) {
            conn.execute("create schema streamtype");
            conn.execute("create table streamtype/foo (bar blob)");
            try (Statement s = conn.statement("insert into streamtype/foo (bar) values ($stream)")) {
                s.linkInputStream("stream", (String value, OutputStream source)->{
                    try (DataOutputStream dos = new DataOutputStream(source)) {
                        dos.writeUTF(value);
                    }
                }).value("this is a farse");
                s.execute();
            }
            try (Statement s = conn.statement("select bar from streamtype/foo")) {
                Output<String> b = s.linkOutputStream(1, (source)->{
                    try (DataInputStream dis = new DataInputStream(source)) {
                        return dis.readUTF();
                    }
                });
                assertTrue(s.fetch());
                assertEquals("this is a farse", b.get());
            }
        }
        service.shutdown();
    }

    @org.junit.jupiter.api.Test
    public void testStreamSpanning() throws Exception {
        try (BaseWeaverConnection conn = BaseWeaverConnection.connectAnonymously("test")) {
            conn.execute("create schema spanning");
            conn.execute("create table spanning/store (bar streaming)");
            conn.execute("create table spanning/main (id int4, data blob in spanning/store) inherits (spanning/store)");
            try (Statement s = conn.statement("insert into spanning/main (id, data) values ($id, $stream)")) {
                s.linkInput("id", Integer.class).value(1);
                s.linkInputStream("stream", (String value, OutputStream sink)->{
                    try (DataOutputStream dos = new DataOutputStream(sink)) {
                        dos.writeUTF(value);
                    }
                }).value("this is a farse");
                s.execute();
            }
            try (Statement s = conn.statement("select data from spanning/main where id=$id")) {
                s.linkInput("id", Integer.class).set(1);
                Output<String> value = s.linkOutputStream(1, (InputStream source)->{
                    try (DataInputStream dis = new DataInputStream(source)) {
                        return dis.readUTF();
                    }
                });
                assertTrue(s.fetch());
                assertEquals("this is a farse", value.get());
            }
        }
    }
    
    @org.junit.jupiter.api.Test
    public void testNullVsZeroLen() throws Exception {
        try (BaseWeaverConnection conn = BaseWeaverConnection.connectAnonymously("test")) {
            conn.execute("create table zerolen (value varchar(256))");
            try (Statement s = conn.statement("insert into zerolen (value) values ($checkit)")) {
                s.linkInput("checkit", String.class).value("");
                s.execute();
            }
            try (Statement s = conn.statement("select value from zerolen")) {
                Output<String> v = s.linkOutput(1, String.class);
                s.execute();
                s.fetch();
                Assertions.assertTrue(v.get().length() == 0);
            }
            conn.execute("delete from zerolen");
            try (Statement s = conn.statement("insert into zerolen (value) values ($checkit)")) {
                s.linkInput("checkit", String.class).value(null);
                s.execute();
            }
            try (Statement s = conn.statement("select value from zerolen")) {
                Output<String> v = s.linkOutput(1, String.class);
                s.execute();
                s.fetch();
                Assertions.assertNull(v.get());
            }
        }
    }
    
    @org.junit.jupiter.api.Test
    public void testNaturalTypes() throws Exception {
        try (BaseWeaverConnection conn = BaseWeaverConnection.connectAnonymously("test")) {
            conn.execute("create table naturaltypes (id int4, value varchar(256))");
            conn.execute("insert into naturaltypes (id, value) values (1, 'value')");
            try (Statement s = conn.statement("select id, value from naturaltypes")) {
                Output<Object> id = s.linkOutput(1, Object.class);
                Output<Object> v = s.linkOutput(2, Object.class);
                s.execute();
                s.fetch();
                Assertions.assertTrue(id.get() instanceof Integer);
                Assertions.assertTrue(v.get() instanceof String);
            }
        }
    }
    
        
    @org.junit.jupiter.api.Test
    public void testResultSetStream() throws Exception {
        try (BaseWeaverConnection conn = BaseWeaverConnection.connectAnonymously("test")) {
            conn.execute("create table resultset (id int4, value varchar(256))");
            conn.execute("insert into resultset (id, value) values (1, 'value')");
            conn.execute("insert into resultset (id, value) values (2, 'value2')");
            conn.execute("insert into resultset (id, value) values (3, 'value3')");
            conn.execute("insert into resultset (id, value) values (4, 'value4')");
            conn.execute("insert into resultset (id, value) values (5, 'value5')");
            try (Stream<Row> r = ResultSet.stream(conn.statement("select xmin, * from resultset"))) {
                Assertions.assertEquals(5, r.peek(os->{
                        for (Column o : os) {
                            System.out.println(o.getName() + "=" + o.get());
                        }
                }).count());
            }
        }
    }
    
    @org.junit.jupiter.api.Test
    public void testLongStream() throws Exception {
        try (BaseWeaverConnection conn = BaseWeaverConnection.connectAnonymously("test")) {
            Generator generate = new Generator(1024 * 1024);
            long now = System.nanoTime();
            conn.execute("create table longstream (data streaming)");
            conn.execute("create table mainstream (id int4, value blob in longstream) inherits (longstream)");
            try (Statement s = conn.statement("insert into mainstream(id, value) values ($id, $value)")) {
                Input<Integer> id = s.linkInput("id", Integer.class);
                Input<Generator> value = s.linkInputChannel("value", (g, w)-> {
                    byte[] next = g.read();
                    OutputStream out = Channels.newOutputStream(w);
                    while (next != null) {
                        out.write(next);
                        next = g.read();
                    }
                });
                id.set(1);
                value.set(generate);
                s.execute();
            }
            long tp = System.nanoTime() - now;
            double Gs = Long.valueOf(generate.current).doubleValue() / Long.valueOf(tp).doubleValue();
            now = System.nanoTime();
            System.out.println("write took " + Duration.ofNanos(tp).toSeconds() + "." + Duration.ofNanos(tp).toMillisPart());
            MessageDigest digest = MessageDigest.getInstance("SHA-256");
            try (Statement s = conn.statement("select value from mainstream where id = $id")) {
                s.linkInput("id", Integer.class).set(1);
                s.linkOutputChannel(1, ()->Channels.newChannel(new DigestOutputStream(OutputStream.nullOutputStream(), digest)));
                s.execute();
                s.fetch();
                tp = System.nanoTime() - now;
                double Grs = Long.valueOf(generate.current).doubleValue() / Long.valueOf(tp).doubleValue();
                System.out.println("read took " + Duration.ofNanos(tp).toSeconds() + "." + Duration.ofNanos(tp).toMillisPart());
                System.out.println("output " + Gs + " " + Grs);
                Assertions.assertArrayEquals(generate.getSignature(), digest.digest());
            }
            try (Statement s = conn.statement("select length(value) from mainstream where id = 1")) {
                Output<Long> len = s.linkOutput(1, Long.class);
                s.execute();
                s.fetch();
                Assertions.assertEquals(generate.current, len.get().longValue());
            }
            now = System.nanoTime();
            try (Statement s = conn.statement("select md5(value) from mainstream where id = 1")) {
                Output<String> len = s.linkOutput(1, String.class);
                s.execute();
                s.fetch();
                System.out.println(len.get());
            }
            tp = System.nanoTime() - now;
            System.out.println("md5 " + Duration.ofNanos(tp).toMillis());
        }
    }
    
    @org.junit.jupiter.api.Test
    public void testAutoCommit() throws Exception {
        try (BaseWeaverConnection conn = BaseWeaverConnection.connectAnonymously("test")) {
            conn.execute("create table autocom (id int4, data varchar(256))");
            conn.execute("insert into autocom (id, data) values (1, 'fortune')");
            try (Statement s = conn.statement("select id, data from autocom")) {
                Output<Integer> id = s.linkOutput(1, Integer.class);
                Output<String> data = s.linkOutput(2, String.class);
                s.execute();
                while (s.fetch()) {
                    System.out.println(id.get() + " " + data.get());
                }
            }
        }   
        try (BaseWeaverConnection conn = BaseWeaverConnection.connectAnonymously("test")) {
            try (Statement s = conn.statement("select id, data from autocom")) {
                Output<Integer> id = s.linkOutput(1, Integer.class);
                Output<String> data = s.linkOutput(2, String.class);
                s.execute();
                int count = 0;
                while (s.fetch()) {
                    System.out.println(id.get() + " " + data.get());
                    count++;
                }
                Assertions.assertEquals(1, count);
            }
        }  
    }
    
    @org.junit.jupiter.api.Test
    public void testAbort() throws Exception {
        try (BaseWeaverConnection conn = BaseWeaverConnection.connectAnonymously("test")) {
            conn.execute("create table test5 (id int4, data varchar(256))");
            conn.begin();
            conn.execute("insert into test5 (id, data) values (1, 'fortune')");
            conn.abort();
            try (Statement s = conn.statement("select id, data from test5")) {
                Output<Integer> id = s.linkOutput(1, Integer.class);
                Output<String> data = s.linkOutput(2, String.class);
                s.execute();
                int count = 0;
                while (s.fetch()) {
                    System.out.println(id.get() + " " + data.get());
                    count++;
                }
                Assertions.assertEquals(0, count);
            }
        }   
    }
    
    @org.junit.jupiter.api.Test
    public void testExecuteCount() throws Exception {
        try (BaseWeaverConnection conn = BaseWeaverConnection.connectAnonymously("test")) {
            conn.execute("create table test6 (id int4, data varchar(256))");
            long result = conn.execute("insert into test6 (id, data) values (1, 'fortune')");
            Assertions.assertEquals(1, result);
            try (Statement s = conn.statement("select id, data from test6")) {
                Output<Integer> id = s.linkOutput(1, Integer.class);
                Output<String> data = s.linkOutput(2, String.class);
                Assertions.assertEquals(0, s.execute());
                int count = 0;
                while (s.fetch()) {
                    System.out.println(id.get() + " " + data.get());
                    count++;
                }
                Assertions.assertEquals(1, count);
            }
        }   
    }
        
    @org.junit.jupiter.api.Test
    public void testPrepareSpansCommits() throws Exception {
        try (BaseWeaverConnection conn = BaseWeaverConnection.connectAnonymously("test")) {
            conn.execute("create table test7 (id int4, data varchar(256))");
            long result = conn.execute("insert into test7 (id, data) values (1, 'fortune')");
            Assertions.assertEquals(1, result);
            conn.begin();
            try (Statement s = conn.statement("select id, data from test7")) {
                Output<Integer> id = s.linkOutput(1, Integer.class);
                Output<String> data = s.linkOutput(2, String.class);
                Assertions.assertEquals(0, s.execute());
                int count = 0;
                while (s.fetch()) {
                    System.out.println(id.get() + " " + data.get());
                    count++;
                }
                Assertions.assertEquals(1, count);
                conn.commit();
                conn.begin();
                s.execute();
                count = 0;
                while (s.fetch()) {
                    System.out.println(id.get() + " " + data.get());
                    count++;
                }
                Assertions.assertEquals(1, count);
                conn.commit();
            }
        }   
    }
    
    @org.junit.jupiter.api.Test
    public void testCheckCommits() throws Exception {
        try (BaseWeaverConnection conn = BaseWeaverConnection.connectAnonymously("test")) {
            System.out.println(conn.transaction());
            long transaction = conn.begin();
            long check = conn.transaction();
            System.out.println(check + " " + transaction);
            Assertions.assertEquals(check, transaction);
            conn.commit();
            long check2 = conn.transaction();
            Assertions.assertEquals(0L, check2);
        }   
    }
    
    @org.junit.jupiter.api.Test
    public void testNestedTransactionsNotAllowed() throws Exception {
        try (BaseWeaverConnection conn = BaseWeaverConnection.connectAnonymously("test")) {
            conn.begin();
            conn.begin();
        } catch (ExecutionException ee) {
            ee.printStackTrace();
            // expected
        } 
    }
    
    @org.junit.jupiter.api.Test
    public void testNestedProcuduresNotAllowed() throws Exception {
        try (BaseWeaverConnection conn = BaseWeaverConnection.connectAnonymously("test")) {
            conn.begin();
            conn.start();
            conn.start();
        } catch (ExecutionException ee) {
            ee.printStackTrace();
            // expected
        } 
    }
    
    @org.junit.jupiter.api.Test
    public void testPrepareFailsWithBadQuery() throws Exception {
        try (BaseWeaverConnection conn = BaseWeaverConnection.connectAnonymously("test")) {
            conn.begin();
            try {
                conn.execute("select * from faketable");
            } catch (ExecutionException ee) {
                ee.printStackTrace();
                // expected
            }
            try {
                conn.prepare();
            } catch (ExecutionException ee) {
                ee.printStackTrace();
                // expected
            }
            conn.commit(); // fails
        } catch (ExecutionException ee) {
            ee.printStackTrace();
            // expected
        } 
        
    }
    
    @org.junit.jupiter.api.Test
    public void testResultSetStreams() throws Exception {
        try (BaseWeaverConnection conn = BaseWeaverConnection.connectAnonymously("test")) {
            conn.execute("create table test9 (id int4, value varchar(256))");
            conn.execute("insert into test9 (id, value) values (1, 'test1')");
            conn.execute("insert into test9 (id, value) values (2, 'test2')");
            conn.execute("insert into test9 (id, value) values (3, 'test3')");
            conn.execute("insert into test9 (id, value) values (4, 'test4')");
            conn.execute("insert into test9 (id, value) values (5, 'test5')");
            try (TransactionSequence ts = new TransactionSequence(conn)) {
                try (TransactionSequence.Procedure p = ts.start()) {
                    try (Stream<Row> set = ResultSet.stream(p.statement("select * from test9"))) {
                        set.flatMap(Row::stream).filter(Column::isValid).forEach(System.out::println);
                    }
                }
            }
        }
    }
    
    @org.junit.jupiter.api.Test
    public void testDisableAutoCommit() throws Exception {
        try (BaseWeaverConnection conn = BaseWeaverConnection.connectAnonymously("test")) {
            conn.setAutoCommit(false);
            conn.execute("create table test10 (id int4, value varchar(256))");
        } catch (ExecutionException ee) {
            // expected 
            ee.printStackTrace();
        }
    }
    
    @org.junit.jupiter.api.Test
    public void testIndexCreation() throws Exception {
        try (BaseWeaverConnection conn = BaseWeaverConnection.connectAnonymously("test")) {
            try (TransactionSequence ts = new TransactionSequence(conn)) {
                try (TransactionSequence.Procedure p = ts.start()) {
                    p.execute("create table test11 (id int4, value varchar(256))");
                    p.execute("insert into test11 (id, value) values (1, 'test1')");
                    p.execute("insert into test11 (id, value) values (2, 'test2')");
                    p.execute("insert into test11 (id, value) values (3, 'test3')");
                    p.execute("insert into test11 (id, value) values (4, 'test4')");
                    p.execute("insert into test11 (id, value) values (5, 'test5')");

                    try (Stream<Row> set = ResultSet.stream(p.statement("select * from test11"))) {
                        set.flatMap(Row::stream).filter(Column::isValid).forEach(System.out::println);
                    }
                }
                try (TransactionSequence.Procedure p = ts.start()) {
                    p.execute("create index test11_id_idx on test11(id)");
                    try (Statement s = p.statement("insert into test11 (id, value) values ($id, $val)")) {
                        Input<Integer> id = s.linkInput("id", Integer.class);
                        Input<String> val = s.linkInput("val", String.class);
                        IntStream.range(0, 10000).forEach(i->{
                            try {
                                id.set(i);
                                val.set("value" + i);
                                s.execute();
                            } catch (ExecutionException ee) {
                                
                            }
                        });
                    }
                }
//                try (Stream<Row> explain = ResultSet.stream(ts.statement("select * from (explain select value from test11 where id = 4)"))) {
//                    explain.flatMap(r->r.stream()).forEach(System.out::println);
//                }
            }
        } catch (ExecutionException ee) {
            // expected 
            ee.printStackTrace();
        }
    }
    
    @org.junit.jupiter.api.Test
    public void testStreamExec() throws Exception {
        try (BaseWeaverConnection conn = BaseWeaverConnection.connectAnonymously("test")) {
            try (TransactionSequence ts = new TransactionSequence(conn)) {
                try (TransactionSequence.Procedure p = ts.start()) {
                    p.execute("create table test12 (id int4, value varchar(256))");
                    p.execute("insert into test12 (id, value) values (1, 'test1')");
                    p.execute("insert into test12 (id, value) values (2, 'test2')");
                    p.execute("insert into test12 (id, value) values (3, 'test3')");
                    p.execute("insert into test12 (id, value) values (4, 'test4')");
                    p.execute("insert into test12 (id, value) values (5, 'test5')");

                    try (Stream<Row> set = ResultSet.stream(p.statement("select * from test12"))) {
                        set.flatMap(Row::stream).filter(Column::isValid).forEach(System.out::println);
                    }
                }
            }
            conn.setStandardOutput(System.out);
            conn.streamExec("explain select * from test12");

            Random gen = new Random();
            try (TransactionSequence p = new TransactionSequence(conn)) {
                try (Statement s = p.statement("insert into test12 (id, value) values ($id, $val)")) {
                    Input<Integer> id = s.linkInput("id", Integer.class);
                    Input<String> val = s.linkInput("val", String.class);
                    for (int x=0;x<1000000;x++) {
                        id.set(x);
                        val.set("value" + gen.nextInt(1000000));
                        s.execute();
                    }
                }
            }
            System.out.println("load finished");
            conn.begin();
            System.out.println("begin create index");

            conn.execute("create index test12_id_idx on test12(id)");
            System.out.println("finish create index");
            conn.commit();
            System.out.println("index finished");   
            conn.streamExec("explain select * from test12 where id = 4");
            try (Statement s = conn.statement("select id, value from test12 where id = $id")) {
                int search = gen.nextInt(1000000);
                System.out.println("searching " + search);
                s.linkInput("id", Integer.class).set(search);
                Output<Integer> id = s.linkOutput(1, Integer.class);
                Output<String> value = s.linkOutput(2, String.class);
                s.execute();
                // execute
                while (s.fetch()) {
                    System.out.println(id.get() + " " + value.get());
                }
            }
            conn.execute("drop index test12_id_idx");
            conn.streamExec("explain select * from test12 where id = 4");
            try (Statement s = conn.statement("select id, value from test12 where id = $id")) {
                s.linkInput("id", Integer.class).set(gen.nextInt(1000000));
                try (Stream<Row> explain = ResultSet.stream(s)) {
                    explain.flatMap(r->r.stream()).forEach(System.out::println);
                }
            }
            System.out.println("done");
        }
    }
    
    @org.junit.jupiter.api.Test
    public void testCreateIndex() throws Exception {
        try (BaseWeaverConnection conn = BaseWeaverConnection.connectAnonymously("test")) {
            conn.execute("create table test13 (id int4, value varchar(256))");
            conn.execute("create index test13_id_idx on test13(id)");
        }
    }
    
    @org.junit.jupiter.api.Test
    public void testAbortCreate() throws Exception {
        try (BaseWeaverConnection conn = BaseWeaverConnection.connectAnonymously("test")) {
            conn.begin();
            conn.execute("create table test14 (id int4, value varchar(256))");
            conn.abort();
            conn.begin();
            conn.execute("create table test14 (id int4, value varchar(256))");
            conn.commit();
            conn.execute("create table test15 (id int4, value varchar(256))");
            conn.begin();
            conn.execute("create table test16 (id int4, value varchar(256))");
            try {
                conn.execute("create table test17 (id int4, value varchar(256))");
            } catch (ExecutionException ee) {
                ee.printStackTrace();
            }
            conn.abort();
        }
    }
    
    private static class Generator {
        private final long totalSize;
        private final MessageDigest sig;
        private long current = 0;
        private final Random random;

        public Generator(long totalSize) {
            this.totalSize = totalSize;
            MessageDigest sign = null;
            try {
                sign = MessageDigest.getInstance("SHA-256");
            } catch (NoSuchAlgorithmException no) {
            }
            this.sig = sign;
            random = new Random();
        }
        
        public byte[] read() {
            if (totalSize > 0 && current >= totalSize) {
                return null;
            } else {
                byte[] gen = new byte[1024];
                random.nextBytes(gen);
                sig.update(gen);
                current += gen.length;
                return gen;
            }
        }
        
        public byte[] getSignature() {
            return sig.digest();
        }
    }
    
}
