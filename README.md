# WeaverDB

WeaverDB is an embeddable database engine based on a very old version (~v7) of PostgreSQL.  

## Description

First forked around 2008 and is made embeddable by moving the project from a process per connection model to a thread per connection in a single process.  The main binding provided here is Java via JNI.  An interface for C is also provided.  

While the bulk of the PostgreSQL code remains, there are many notable changes.  Besides the move from process per connection, WeaverDB also changes the way data is written to files.  WeaverDB uses a single writer thread to write changes to the underlying files.  New concurrency control structures have been added to buffer pages and elsewhere mainly with the use of pthread_mutex and pthread_cond.  Vacuum occurs here using an in process thread scheduler called poolsweep.  Ability to add C extensions has been removed and replaced with a basic form of Java extensions.  Networking code is mostly removed or non-functional.  

This project does not have all of the improvements, development, and reliablitity testing of the last 15+ years of PostgreSQL and should not be considered stable and reliable at this time.  The codebase has only been recently revived and is being worked on in spare time.

## Getting Started

### Building
    
    % mkdir build; cmake -S . -B build; cmake --build build
    % ./gradlew build

Native libraries are written to `build/mtpg/lib` (`libweaver` / `libweaver_jni`). Gradle tests load them from that path.

### Running

The main interfaces for loading and using WeaverDB are [DirectWeaverInitializer](https://github.com/weaverdb/weaverdb/blob/main/connect25/src/main/java/org/weaverdb/direct/DirectWeaverInitializer.java) (recommended FFM path) or the legacy [WeaverInitializer](https://github.com/weaverdb/weaverdb/blob/main/pgjava_c/src/main/java/org/weaverdb/WeaverInitializer.java), along with [DBReference](https://github.com/weaverdb/weaverdb/blob/main/pgjava_c/src/main/java/org/weaverdb/DBReference.java).

**Recommended path (2026+):** Use `DirectWeaverInitializer` + the FFM client. This enables Java stored procedures via pure FFM upcalls (no JNI required for the client).

Legacy example (still works):

    Properties prop = new Properties();
    prop.setProperty("datadir", System.getProperty("user.dir") + "/build/testdb");
    WeaverInitializer.initialize(prop);

Preferred (FFM) example:

    Properties prop = new Properties();
    prop.setProperty("datadir", System.getProperty("user.dir") + "/build/testdb");
    DirectWeaverInitializer.initialize(prop);

### Dependencies

Requires CMake 4.2.1+, a C99 compiler (Clang or GCC), bison, flex, Gradle, and Java 17 or greater (Java 25 for the FFM `connect25` module).

### Platform support

| Platform | Status |
|----------|--------|
| macOS (Darwin) | Primary development target |
| Linux | Supported for native + JNI/FFM builds (x86_64 and aarch64) |
| Android | Cross-build via `abuild.sh` / NDK (`x86`, `x86_64`, `arm64-v8a`, `armeabi-v7a`) |
| Windows | Not supported — POSIX-only runtime and LP64 `long`/pointer assumptions |

Spinlocks use `pthread_mutex_t` (`SPIN_IS_MUTEX`), so CPU-specific test-and-set assembly is not required for current builds. On-disk page layouts are endian-sensitive; databases are not portable across endianness. Android builds use a 4KB block size; other platforms default to 8KB.

### Installing

No binaries are currently being published.


## Authors

Myron Scott <myron@weaverdb.org>

## License

This project is licensed under the BSD License - see the LICENSE file for details

## Acknowledgments

Inspiration
* [PostgreSQL](https://www.postgresql.org)


See [mtpgsql/README](mtpgsql/README) for historical README

