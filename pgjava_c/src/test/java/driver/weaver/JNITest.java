

package driver.weaver;

import driver.weaver.BaseWeaverConnection.Statement;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.io.Writer;
import java.nio.channels.Channels;
import java.util.Properties;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.function.Consumer;
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
            conn.execute("create table typecheck (id int4, value varchar(256))");
            conn.execute("insert into typecheck (id, value) values (1, 'value')");
            try (Statement s = conn.statement("select id, value from typecheck")) {
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
            conn.execute("create table typecheck (id int4, value varchar(256))");
            conn.execute("insert into typecheck (id, value) values (1, 'value')");
            conn.execute("insert into typecheck (id, value) values (2, 'value2')");
            conn.execute("insert into typecheck (id, value) values (3, 'value3')");
            conn.execute("insert into typecheck (id, value) values (4, 'value4')");
            conn.execute("insert into typecheck (id, value) values (5, 'value5')");
            try (Statement s = conn.statement("select xmin, * from typecheck")) {
                Assertions.assertEquals(5,ResultSet.stream(s).peek(os->{
                    try {
                        for (int x=0;x<os.length;x++) {
                            System.out.println(os[x].getName() + "=" + os[x].get());
                        }
                    } catch (ExecutionException ee) {
                        
                    }
                }).count());
            }
        }
    }
}
