package org.weaverdb.direct.example;

import java.io.Serial;
import java.io.Serializable;

/**
 * Demo methods registered as SQL functions via the FFM Java function path
 * (LANGUAGE 'java').
 */
public class JavaFunctionDemoMethods implements Serializable {

    @Serial
    private static final long serialVersionUID = 1L;

    public static String greet(String name) {
        if (name == null || name.isBlank()) {
            return "Hello, stranger!";
        }
        return "Hello, " + name + "!";
    }

    public static String echo(String value) {
        return value;
    }

    public static int addInts(int a, int b) {
        return a + b;
    }

    public static long addLongs(long a, long b) {
        return a + b;
    }

    public static double addDoubles(double a, double b) {
        return a + b;
    }

    public static boolean flag(boolean value) {
        return !value;
    }

    public static int failLoudly(String why) {
        throw new IllegalStateException(why != null ? why : "failed");
    }

    public static String formatWithPrefix(String prefix, int value) {
        return (prefix != null ? prefix : "Result:") + " " + value;
    }

    /**
     * Takes a simple value object and returns a transformed one.
     * Exercises JAVA_OBJECT serialization in both directions.
     */
    public static PersonInfo enrichPerson(PersonInfo input) {
        if (input == null) {
            return new PersonInfo("Unknown", 0, false);
        }
        return new PersonInfo(
                input.name().toUpperCase(),
                input.age() + 1,
                true
        );
    }

    private final String prefix;

    public JavaFunctionDemoMethods(String prefix) {
        this.prefix = prefix != null ? prefix : "Result:";
    }

    public String formatWithPrefix(int value) {
        return prefix + " " + value;
    }

    public static final class PersonInfo implements Serializable {
        @Serial
        private static final long serialVersionUID = 1L;

        private final String name;
        private final int age;
        private final boolean verified;

        public PersonInfo(String name, int age, boolean verified) {
            this.name = name;
            this.age = age;
            this.verified = verified;
        }

        public String name() { return name; }
        public int age() { return age; }
        public boolean verified() { return verified; }

        @Override
        public String toString() {
            return "PersonInfo{name='" + name + "', age=" + age + ", verified=" + verified + "}";
        }
    }
}
