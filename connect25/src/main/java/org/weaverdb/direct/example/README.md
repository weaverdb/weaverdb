# Java Functions over FFM (Examples)

End-to-end usage for registering and calling Java methods as SQL functions
(`LANGUAGE 'java'`) on the FFM client path.

## Files

- `JavaFunctionFFMExample.java` — Runnable example: register and call functions.
- `JavaFunctionDemoMethods.java` — Demo methods (varchar, primitives, JAVA_OBJECT, instance method, errors).

## How These Work

1. `DirectWeaverInitializer.initialize(...)` starts the database and registers the FFM upcall invoker.
2. `FunctionInstaller` creates SQL functions that map to Java `MethodHandle`s.
3. SQL execution calls into `JavaFunctionInvoker` via the native upcall. Arguments and results use `JavaCallProtocol` (int4/int8/float8/bool/varchar/java/null). Java exceptions become SQL errors with the exception text.

## Running

Run `JavaFunctionFFMExample` with Java 25+ and `--enable-native-access=ALL-UNNAMED`.

You will need a built `libweaver` (and the Java modules) on the classpath/modulepath.
