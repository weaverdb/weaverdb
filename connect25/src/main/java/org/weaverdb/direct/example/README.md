# Java Functions over FFM (Examples)

This directory contains end-to-end usage examples for registering and calling Java methods as SQL functions using the modern FFM client path.

## Files

- `JavaFunctionFFMExample.java` — Main runnable example showing registration and calls.
- `JavaFunctionDemoMethods.java` — Companion class containing several interesting demo methods (String return, JAVA_OBJECT round-trip, instance method).

## How These Work

1. `DirectWeaverInitializer.initialize(...)` starts the database using the FFM path.
2. As part of initialization, it automatically registers the FFM upcall-based Java function invoker.
3. `FunctionInstaller` is used to create SQL functions that map to Java `MethodHandle`s.
4. When the functions are called from SQL, execution flows through the new upcall mechanism instead of the classic JNI path.

## Current Status

These examples are intended to become fully working as the remaining C-side integration work (argument/result conversion, error handling, etc.) is completed.

See the class Javadocs for the latest notes on what is currently functional.

## Running

Run `JavaFunctionFFMExample` with Java 25+ and `--enable-native-access=ALL-UNNAMED`.

You will need a built `libweaver` (and the Java modules) on the classpath/modulepath.
