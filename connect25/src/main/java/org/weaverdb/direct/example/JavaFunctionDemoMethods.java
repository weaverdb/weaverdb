package org.weaverdb.direct.example;

/**
 * A small set of demo methods intended to be registered as SQL functions
 * via the FFM Java function path (LANGUAGE 'java').
 *
 * These are more interesting than the trivial addOne example and exercise
 * different parts of the current implementation:
 *   - Returning String (TEXT/VARCHAR)
 *   - Simple Java object (JAVA_OBJECT) round-trip
 *   - Instance method
 */
public class JavaFunctionDemoMethods {

    // ---------------------------------------------------------------------
    // Static methods
    // ---------------------------------------------------------------------

    /** Returns a greeting string. Good for testing TEXT/VARCHAR return. */
    public static String greet(String name) {
        if (name == null || name.isBlank()) {
            return "Hello, stranger!";
        }
        return "Hello, " + name + "!";
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

    // ---------------------------------------------------------------------
    // Instance method example
    // ---------------------------------------------------------------------

    private final String prefix;

    public JavaFunctionDemoMethods(String prefix) {
        this.prefix = prefix != null ? prefix : "Result:";
    }

    /** Instance method that prefixes a number. */
    public String formatWithPrefix(int value) {
        return prefix + " " + value;
    }

    // ---------------------------------------------------------------------
    // Simple value class for JAVA_OBJECT demo
    // ---------------------------------------------------------------------

    /** A tiny immutable value class for demonstrating object round-tripping. */
    public static final class PersonInfo {
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
