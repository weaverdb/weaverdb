package driver.weaver;

import driver.weaver.BaseWeaverConnection.Statement;
import java.io.Writer;
import java.util.Properties;

/*
 * Click nbfs://nbhost/SystemFileSystem/Templates/Licenses/license-default.txt to change this license
 * Click nbfs://nbhost/SystemFileSystem/Templates/UnitTests/JUnit5TestClass.java to edit this template
 */

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
        prop.setProperty("start_delay", "1");
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
            try (Statement s = conn.parse("select * from pg_database;")) {
                BoundOutput<String> b = s.linkOutput(1, String.class);
                BoundOutput<String> c = s.linkOutput(2, String.class);
                BoundOutput<String> d = s.linkOutput(3, String.class);
                BoundOutput<String> e = s.linkOutput(4, String.class);
                BoundOutput<String> f = s.linkOutput(5, String.class);
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
            try (Statement s = conn.parse("select * from pg_database;")) {
                BoundOutput<Integer> b = s.linkOutput(1, Integer.class);
                BoundOutput<Integer> c = s.linkOutput(2, Integer.class);
                BoundOutput<Integer> d = s.linkOutput(3, Integer.class);
                BoundOutput<Integer> e = s.linkOutput(4, Integer.class);
                BoundOutput<Integer> f = s.linkOutput(5, Integer.class);
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
            try (Statement s = conn.parse("select * from pg_class;")) {
                BoundOutput<String> b = s.linkOutput(1, String.class);
                BoundOutput<String> c = s.linkOutput(2, String.class);
                BoundOutput<String> d = s.linkOutput(3, String.class);
                BoundOutput<String> e = s.linkOutput(4, String.class);
                BoundOutput<String> f = s.linkOutput(5, String.class);
                System.out.println(s.execute());
                while (s.fetch()) {
                    if (!b.isNull()) System.out.println(b.getName() + "=" + b.get());
                    if (!c.isNull())System.out.println(c.getName() + "=" + c.get());
                    if (!d.isNull())System.out.println(d.getName() + "=" + d.get());
                    if (!e.isNull())System.out.println(e.getName() + "=" + e.get());
                    if (!f.isNull())System.out.println(f.getName() + "=" + f.get());
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
        Statement s = conn.parse("select * from pg_class;");
        BoundOutput<String> b = s.linkOutput(1, String.class);
        BoundOutput<String> c = s.linkOutput(2, String.class);
        BoundOutput<String> d = s.linkOutput(3, String.class);
        BoundOutput<String> e = s.linkOutput(4, String.class);
        BoundOutput<String> f = s.linkOutput(5, String.class);
        System.out.println(s.execute());
        while (s.fetch()) {
            if (!b.isNull()) System.out.println(b.getName() + "=" + b.get());
            if (!c.isNull())System.out.println(c.getName() + "=" + c.get());
            if (!d.isNull())System.out.println(d.getName() + "=" + d.get());
            if (!e.isNull())System.out.println(e.getName() + "=" + e.get());
            if (!f.isNull())System.out.println(f.getName() + "=" + f.get());
            System.out.println("+++++++");
        }
        conn.close();
        s.close();
        conn.close();
        s.close();
    }
}
