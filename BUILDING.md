# Building WeaverDB

One command from the repository root builds the native engine and runs the unit and integration tests:

```
./gradlew check
```

`./gradlew build` assembles the Java modules and runs `check`. A native abort fails only its own test task. Gradle keeps going (`continueOnFailure`) so the other suites still report, then the build exits non-zero.

## Prerequisites

- CMake **4.2.1 or newer** on `PATH`. Older CMake fails in `buildNative` with the version it found.
- A C99 compiler (Clang or GCC), bison, and flex.
- JDK **17**, **21**, and **25** installed. Gradle toolchains select them. `verifyJdks` names the one that is missing. JDK 17 compiles `pgjava_c`. JDK 21 runs `connect21`. JDK 25 runs `connect25` and the JNI tests. The FFM client needs 25.
- Linux or macOS. Windows is not a native target.

## What `check` runs

| Task | What |
|---|---|
| `test` | Fast tests with no database: codec, bound values, type transfer, the FFM upcall stub. |
| `integrationTest` | Engine tests: JNI and FFM battle (in-process and isolated), `WeaverRestartTest`, fresh initdb, pgvector, the Java 21 suite. |
| `:pgjava_c:jniTest` | `JNITest` in its own JVM. |
| `:pgjava_c:crashSmokeTest` | Index/heap crash smoke (shell script and Java child JVMs), one class per JVM. |

Native code is configured when `build_test/CMakeCache.txt` is missing or was written by CMake older than 4.2.1, then built incrementally. Outputs live in `build_test/mtpg` (`bin/`, `lib/`, `BUILD_COMMIT`). Every engine test reads that tree. `BUILD_COMMIT` must equal `git rev-parse HEAD`.

`./gradlew clean` deletes module `build/` directories and `build_test/`.

Which test was running when a worker dies is appended to `build/reports/test-progress.log` as `STARTED` / `SUCCESS` / `FAILURE` lines. Shell and crash-harness output is also in the JUnit failure message and in each module's `build/reports/<task>-harness.log`.

## Opt-in soak

Not part of `check`. Several hours of index/heap crash rounds:

```
./gradlew :pgjava_c:soakTest
```

`:pgjava_c:indexHeapCrashLong` is the same task. Duration and clients: `-Dweaver.crash.duration.sec=3600` and `-Dweaver.crash.clients=4`.

## Generated sources

CMake still writes `fmgr.h`, `fmgrtab.c`, and the bison/flex outputs (`gram.c`, `scan.c`, `parse.h`, `bootparse.c`, `bootscanner.c`, `bootstrap_tokens.h`) into the source tree. Those paths are gitignored. The generators and the backend include layout both point at the source tree, so moving them into `build_test` is a separate change from the Gradle entry point.

## Platforms

macOS is the primary development target. Linux is supported. Android is a separate NDK configure via `abuild.sh`, not `./gradlew check`.
