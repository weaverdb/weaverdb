/*-------------------------------------------------------------------------
 *
 * End-to-end LANGUAGE 'java' coverage on the FFM upcall path.
 *
 *-------------------------------------------------------------------------
 */

package org.weaverdb.direct;

import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;
import org.junit.jupiter.api.Assertions;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.MethodOrderer;
import org.junit.jupiter.api.Order;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.TestMethodOrder;
import org.weaverdb.DBReference;
import org.weaverdb.ExecutionException;
import org.weaverdb.FunctionInstaller;
import org.weaverdb.Input;
import org.weaverdb.Output;
import org.weaverdb.Statement;
import org.weaverdb.direct.example.JavaFunctionDemoMethods;

@TestMethodOrder(MethodOrderer.OrderAnnotation.class)
public class JavaStoredProcedureTest {

    @BeforeAll
    public static void setup() throws Throwable {
        PgvectorWeaverTestSupport.ensureInitialized();
        Assertions.assertNotNull(DirectWeaverInitializer.getJavaFunctionInvoker(),
                "FFM Java function invoker must be registered");

        try (DBReference conn = DBReference.connect("template1")) {
            FunctionInstaller installer = new FunctionInstaller(conn);
            var lookup = MethodHandles.lookup();
            Class<?> demo = JavaFunctionDemoMethods.class;

            installer.installFunction("ffm_greet", lookup.findStatic(
                    demo, "greet", MethodType.methodType(String.class, String.class)));
            installer.installFunction("ffm_echo", lookup.findStatic(
                    demo, "echo", MethodType.methodType(String.class, String.class)));
            installer.installFunction("ffm_add_ints", lookup.findStatic(
                    demo, "addInts", MethodType.methodType(int.class, int.class, int.class)));
            installer.installFunction("ffm_add_longs", lookup.findStatic(
                    demo, "addLongs", MethodType.methodType(long.class, long.class, long.class)));
            installer.installFunction("ffm_add_doubles", lookup.findStatic(
                    demo, "addDoubles", MethodType.methodType(double.class, double.class, double.class)));
            installer.installFunction("ffm_flag", lookup.findStatic(
                    demo, "flag", MethodType.methodType(boolean.class, boolean.class)));
            installer.installFunction("ffm_fail", lookup.findStatic(
                    demo, "failLoudly", MethodType.methodType(int.class, String.class)));
            installer.installFunction("ffm_format", lookup.findStatic(
                    demo, "formatWithPrefix",
                    MethodType.methodType(String.class, String.class, int.class)));
            installer.installFunction("ffm_enrich", lookup.findStatic(
                    demo, "enrichPerson",
                    MethodType.methodType(JavaFunctionDemoMethods.PersonInfo.class,
                            JavaFunctionDemoMethods.PersonInfo.class)));
            installer.installFunction("ffm_instance_format", lookup.findVirtual(
                    demo, "formatWithPrefix", MethodType.methodType(String.class, int.class)));
            installer.installFunction("ffm_hex", MethodHandles.publicLookup().findStatic(
                    Integer.class, "toHexString", MethodType.methodType(String.class, int.class)));
            installer.installFunction("ffm_tostring", MethodHandles.publicLookup().findVirtual(
                    Object.class, "toString", MethodType.methodType(String.class)));
        }
    }

    @Test
    @Order(1)
    public void greetVarcharRoundTrip() throws Exception {
        Assertions.assertEquals("Hello, Alice!", scalarString("select ffm_greet($n)", "n", "Alice"));
        Assertions.assertEquals("Hello, stranger!", scalarString("select ffm_greet($n)", "n", "  "));
    }

    @Test
    @Order(2)
    public void echoNullVarchar() throws Exception {
        try (DBReference conn = DBReference.connect("template1");
                Statement s = conn.statement("select ffm_echo($n)")) {
            s.linkInput("n", String.class).set(null);
            Output<String> out = s.linkOutput(1, String.class);
            s.execute();
            Assertions.assertTrue(s.fetch());
            Assertions.assertNull(out.get());
        }
    }

    @Test
    @Order(3)
    public void primitiveRoundTrips() throws Exception {
        Assertions.assertEquals(7, scalarInt("select ffm_add_ints(3, 4)"));
        Assertions.assertEquals(9L, scalarLong("select ffm_add_longs(4, 5)"));
        Assertions.assertEquals(6.5, scalarDouble("select ffm_add_doubles(2.25, 4.25)"), 1e-9);
        Assertions.assertEquals(Boolean.FALSE, scalarBool("select ffm_flag(true)"));
        Assertions.assertEquals("5", scalarString("select ffm_hex(5)"));
    }

    @Test
    @Order(4)
    public void nestedVarcharFunctions() throws Exception {
        Assertions.assertEquals("Hello, Bob!",
                scalarString("select ffm_greet(ffm_echo($n))", "n", "Bob"));
    }

    @Test
    @Order(5)
    public void javaObjectRoundTrip() throws Exception {
        try (DBReference conn = DBReference.connect("template1");
                Statement s = conn.statement("select ffm_enrich($p)")) {
            Input<JavaFunctionDemoMethods.PersonInfo> in =
                    s.linkInput("p", JavaFunctionDemoMethods.PersonInfo.class);
            Output<JavaFunctionDemoMethods.PersonInfo> out =
                    s.linkOutput(1, JavaFunctionDemoMethods.PersonInfo.class);
            in.set(new JavaFunctionDemoMethods.PersonInfo("bob", 30, false));
            s.execute();
            Assertions.assertTrue(s.fetch());
            JavaFunctionDemoMethods.PersonInfo got = out.get();
            Assertions.assertNotNull(got);
            Assertions.assertEquals("BOB", got.name());
            Assertions.assertEquals(31, got.age());
            Assertions.assertTrue(got.verified());
        }
    }

    @Test
    @Order(6)
    public void instanceMethodWithJavaReceiver() throws Exception {
        try (DBReference conn = DBReference.connect("template1");
                Statement s = conn.statement("select ffm_instance_format($recv, $val)")) {
            s.linkInput("recv", JavaFunctionDemoMethods.class)
                    .set(new JavaFunctionDemoMethods("Value:"));
            s.linkInput("val", Integer.class).set(42);
            Output<String> out = s.linkOutput(1, String.class);
            s.execute();
            Assertions.assertTrue(s.fetch());
            Assertions.assertEquals("Value: 42", out.get());
        }
    }

    @Test
    @Order(7)
    public void instanceToStringOnStoredJavaValue() throws Exception {
        try (DBReference conn = DBReference.connect("template1")) {
            exec(conn, "create table ffm_java_t (item java)");
            try (Statement s = conn.statement("insert into ffm_java_t (item) values ($item)")) {
                s.linkInput("item", java.io.Serializable.class).set("stored-value");
                s.execute();
            }
            try (Statement s = conn.statement("select ffm_tostring(item) from ffm_java_t")) {
                Output<String> out = s.linkOutput(1, String.class);
                s.execute();
                Assertions.assertTrue(s.fetch());
                Assertions.assertEquals("stored-value", out.get());
            }
        }
    }

    @Test
    @Order(8)
    public void staticFormatHelper() throws Exception {
        try (DBReference conn = DBReference.connect("template1");
                Statement s = conn.statement("select ffm_format($p, $v)")) {
            s.linkInput("p", String.class).set("Value:");
            s.linkInput("v", Integer.class).set(7);
            Output<String> out = s.linkOutput(1, String.class);
            s.execute();
            Assertions.assertTrue(s.fetch());
            Assertions.assertEquals("Value: 7", out.get());
        }
    }

    @Test
    @Order(9)
    public void javaExceptionBecomesSqlError() {
        ExecutionException ex = Assertions.assertThrows(ExecutionException.class, () -> {
            try (DBReference conn = DBReference.connect("template1");
                    Statement s = conn.statement("select ffm_fail($why)")) {
                s.linkInput("why", String.class).set("boom-from-java");
                s.execute();
                s.fetch();
            }
        });
        String msg = ex.getMessage() == null ? "" : ex.getMessage();
        Assertions.assertTrue(msg.contains("Java function error"), "missing error prefix: " + msg);
        Assertions.assertTrue(msg.contains("IllegalStateException"), "missing exception type: " + msg);
        Assertions.assertTrue(msg.contains("boom-from-java"), "missing Java message: " + msg);
    }

    private static String scalarString(String sql) throws Exception {
        try (DBReference conn = DBReference.connect("template1");
                Statement s = conn.statement(sql)) {
            Output<String> out = s.linkOutput(1, String.class);
            s.execute();
            Assertions.assertTrue(s.fetch());
            return out.get();
        }
    }

    private static String scalarString(String sql, String bind, String value) throws Exception {
        try (DBReference conn = DBReference.connect("template1");
                Statement s = conn.statement(sql)) {
            s.linkInput(bind, String.class).set(value);
            Output<String> out = s.linkOutput(1, String.class);
            s.execute();
            Assertions.assertTrue(s.fetch());
            return out.get();
        }
    }

    private static int scalarInt(String sql) throws Exception {
        try (DBReference conn = DBReference.connect("template1");
                Statement s = conn.statement(sql)) {
            Output<Integer> out = s.linkOutput(1, Integer.class);
            s.execute();
            Assertions.assertTrue(s.fetch());
            return out.get();
        }
    }

    private static long scalarLong(String sql) throws Exception {
        try (DBReference conn = DBReference.connect("template1");
                Statement s = conn.statement(sql)) {
            Output<Long> out = s.linkOutput(1, Long.class);
            s.execute();
            Assertions.assertTrue(s.fetch());
            return out.get();
        }
    }

    private static double scalarDouble(String sql) throws Exception {
        try (DBReference conn = DBReference.connect("template1");
                Statement s = conn.statement(sql)) {
            Output<Double> out = s.linkOutput(1, Double.class);
            s.execute();
            Assertions.assertTrue(s.fetch());
            return out.get();
        }
    }

    private static Boolean scalarBool(String sql) throws Exception {
        try (DBReference conn = DBReference.connect("template1");
                Statement s = conn.statement(sql)) {
            Output<Boolean> out = s.linkOutput(1, Boolean.class);
            s.execute();
            Assertions.assertTrue(s.fetch());
            return out.get();
        }
    }

    private static void exec(DBReference conn, String sql) throws Exception {
        try (Statement s = conn.statement(sql)) {
            s.execute();
        }
    }
}
