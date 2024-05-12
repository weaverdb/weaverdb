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
    }

    @org.junit.jupiter.api.AfterAll
    public static void tearDownClass() throws Exception {
    }

    @org.junit.jupiter.api.BeforeEach
    public void setUp() throws Exception {
    }

    @org.junit.jupiter.api.AfterEach
    public void tearDown() throws Exception {
    }
    
    @org.junit.jupiter.api.Test
    public void test() throws Exception {
        Properties prop = new Properties();
        prop.setProperty("datadir", System.getProperty("user.dir") + "/build/testdb");
        prop.setProperty("allow_anonymous", "true");
        prop.setProperty("start_delay", "1");
        WeaverInitializer.initialize(prop);
        try (BaseWeaverConnection conn = BaseWeaverConnection.connectAnonymously("test")) {
            try (Statement s = conn.parse("select datname from pg_database;")) {
                BoundOutput<String> b = s.linkOutput(1, String.class);
                s.execute();
                s.fetch();
                System.out.println(b.get());
            }
        }
    }    
}
