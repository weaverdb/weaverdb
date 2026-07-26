package org.weaverdb.direct.example;

import org.weaverdb.*;
import org.weaverdb.direct.DirectWeaverInitializer;

import java.util.Properties;

/**
 * End-to-end example showing how to use Java stored procedures
 * (LANGUAGE 'java') with the modern FFM client path.
 *
 * This demonstrates the intended usage after the FFM upcall invoker work.
 *
 * How to run (example):
 *   1. Build the project (native + Java).
 *   2. Run this class with Java 25+ and --enable-native-access=ALL-UNNAMED.
 *   3. It will create/use ./testdb and register/call a simple Java function.
 *
 * Current status notes:
 * - Initialization via DirectWeaverInitializer automatically registers the
 *   FFM-based Java function invoker (no JNI required for the connection layer).
 * - Function registration via FunctionInstaller still works.
 * - Execution of the Java function will go through the new upcall path
 *   (instead of the classic JNI path) when the invoker is registered.
 *
 * Some wiring in the C backend (argument/result conversion for complex cases,
 * full error propagation) is still being completed.
 */
public class JavaFunctionFFMExample {

    /**
     * Entry point for the more complete FFM Java function demonstration.
     * See JavaFunctionDemoMethods for the actual functions being registered.
     */
    public static void main(String[] args) throws Exception {
        // 1. Initialize using the FFM path (this also registers the Java invoker)
        Properties props = new Properties();
        props.setProperty("datadir", System.getProperty("user.dir") + "/testdb");
        props.setProperty("allow_anonymous", "true");
        props.setProperty("stdlog", "TRUE");
        props.setProperty("disable_crc", "TRUE");

        System.out.println("Initializing WeaverDB via FFM client (DirectWeaverInitializer)...");
        DirectWeaverInitializer.initialize(props);

        Runtime.getRuntime().addShutdownHook(new Thread(() -> {
            try {
                DirectWeaverInitializer.shutdown(java.time.Duration.ofSeconds(10));
            } catch (Exception e) {
                e.printStackTrace();
            }
        }));

        // 2. Connect using the FFM-backed DBReference
        try (DBReference c = DBReferenceManager.connect("template1")) {

            // 3. Register several Java functions using the existing installer.
            //    These come from the companion demo class for variety.
            FunctionInstaller installer = new FunctionInstaller(c);

            var lookup = java.lang.invoke.MethodHandles.lookup();
            var demoClass = JavaFunctionDemoMethods.class;

            System.out.println("Registering Java functions from JavaFunctionDemoMethods...");

            installer.installFunction("greet", lookup.findStatic(
                    demoClass, "greet",
                    java.lang.invoke.MethodType.methodType(String.class, String.class)));

            installer.installFunction("enrich_person", lookup.findStatic(
                    demoClass, "enrichPerson",
                    java.lang.invoke.MethodType.methodType(
                            JavaFunctionDemoMethods.PersonInfo.class,
                            JavaFunctionDemoMethods.PersonInfo.class)));

            // Also register an instance method example
            var instanceDemo = new JavaFunctionDemoMethods("Value:");
            installer.installFunction("format_with_prefix", lookup.findVirtual(
                    demoClass, "formatWithPrefix",
                    java.lang.invoke.MethodType.methodType(String.class, int.class))
                    .bindTo(instanceDemo));

            // 4. Demonstrate calling them from SQL
            System.out.println("\n--- Calling greet (returns String) ---");
            try (Statement s = c.statement("SELECT greet($name) AS greeting")) {
                Input<String> name = s.linkInput("name", String.class);
                Output<String> greeting = s.linkOutput(1, String.class);

                name.set("Alice");
                s.execute();
                if (s.fetch()) {
                    System.out.println("greet('Alice') = " + greeting.get());
                }
            }

            System.out.println("\n--- Calling enrich_person (JAVA_OBJECT round-trip) ---");
            try (Statement s = c.statement("SELECT enrich_person($p) AS info")) {
                // For complex objects we can pass them directly when using the Java API
                Input<JavaFunctionDemoMethods.PersonInfo> person =
                        s.linkInput("p", JavaFunctionDemoMethods.PersonInfo.class);
                Output<JavaFunctionDemoMethods.PersonInfo> info =
                        s.linkOutput(1, JavaFunctionDemoMethods.PersonInfo.class);

                person.set(new JavaFunctionDemoMethods.PersonInfo("bob", 30, false));
                s.execute();
                if (s.fetch()) {
                    System.out.println("enrich_person(...) = " + info.get());
                }
            }

            System.out.println("\n--- Calling instance method via format_with_prefix ---");
            try (Statement s = c.statement("SELECT format_with_prefix($val) AS formatted")) {
                Input<Integer> val = s.linkInput("val", Integer.class);
                Output<String> formatted = s.linkOutput(1, String.class);

                val.set(42);
                s.execute();
                if (s.fetch()) {
                    System.out.println("format_with_prefix(42) = " + formatted.get());
                }
            }

            System.out.println("\nAll Java functions executed successfully through the FFM upcall path!");
        }
    }

    // Note: Real Java methods are now in the companion class JavaFunctionDemoMethods.
}
