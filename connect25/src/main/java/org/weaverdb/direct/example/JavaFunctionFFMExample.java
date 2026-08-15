package org.weaverdb.direct.example;

import org.weaverdb.*;
import org.weaverdb.direct.DirectWeaverInitializer;

import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;
import java.util.Properties;

/**
 * End-to-end example showing how to use Java stored procedures
 * (LANGUAGE 'java') with the FFM client path.
 *
 * Run with Java 25+ and --enable-native-access=ALL-UNNAMED after building
 * libweaver and the Java modules.
 */
public class JavaFunctionFFMExample {

    public static void main(String[] args) throws Exception {
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

        try (DBReference c = DBReferenceManager.connect("template1")) {
            FunctionInstaller installer = new FunctionInstaller(c);
            var lookup = MethodHandles.lookup();
            var demoClass = JavaFunctionDemoMethods.class;

            System.out.println("Registering Java functions from JavaFunctionDemoMethods...");

            installer.installFunction("greet", lookup.findStatic(
                    demoClass, "greet",
                    MethodType.methodType(String.class, String.class)));

            installer.installFunction("enrich_person", lookup.findStatic(
                    demoClass, "enrichPerson",
                    MethodType.methodType(
                            JavaFunctionDemoMethods.PersonInfo.class,
                            JavaFunctionDemoMethods.PersonInfo.class)));

            installer.installFunction("format_with_prefix", lookup.findStatic(
                    demoClass, "formatWithPrefix",
                    MethodType.methodType(String.class, String.class, int.class)));

            installer.installFunction("instance_format", lookup.findVirtual(
                    demoClass, "formatWithPrefix",
                    MethodType.methodType(String.class, int.class)));

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

            System.out.println("\n--- Calling static format_with_prefix ---");
            try (Statement s = c.statement("SELECT format_with_prefix($prefix, $val) AS formatted")) {
                Input<String> prefix = s.linkInput("prefix", String.class);
                Input<Integer> val = s.linkInput("val", Integer.class);
                Output<String> formatted = s.linkOutput(1, String.class);

                prefix.set("Value:");
                val.set(42);
                s.execute();
                if (s.fetch()) {
                    System.out.println("format_with_prefix('Value:', 42) = " + formatted.get());
                }
            }

            System.out.println("\n--- Calling instance method via java receiver ---");
            try (Statement s = c.statement("SELECT instance_format($recv, $val) AS formatted")) {
                Input<JavaFunctionDemoMethods> recv =
                        s.linkInput("recv", JavaFunctionDemoMethods.class);
                Input<Integer> val = s.linkInput("val", Integer.class);
                Output<String> formatted = s.linkOutput(1, String.class);

                recv.set(new JavaFunctionDemoMethods("Value:"));
                val.set(42);
                s.execute();
                if (s.fetch()) {
                    System.out.println("instance_format(Value:, 42) = " + formatted.get());
                }
            }

            System.out.println("\nAll Java functions executed successfully through the FFM upcall path!");
        }
    }
}
