ki# WeaverDB

WeaverDB is an embeddable database engine based on a very old version (~v7) of PostgreSQL.  

## Description

The original PostgreSQL code (~v7) was originally ported around 2008 and is made embeddable by moving the project from a process per connection model to a thread per connection in a single process.  The main binding provided here is Java via JNI.  An interface for C is also provided.  

While the bulk of the PostgreSQL code remains, there are many notable changes.  Besides the move from process per connection, WeaverDB also changes the way data is written to files.  WeaverDB uses a single writer thread to write changes to the underlying files.  New concurrency control structures have been added to buffer pages and elsewhere mainly with the use of pthread_mutex and pthread_cond.  Vacuum occurs here using an in process thread scheduler called poolsweep.  Ability to add C extensions has been removed and replaced with a basic form of Java extensions.  Networking code is mostly removed or non-functional.  

This project does not have all of the improvements, development, and reliablitity testing of the last 15+ years of PostgreSQL and should not be considered stable and reliable at this time.  The codebase has only been recently revived and is being worked on in spare time.

## Getting Started

### Building
    
    % mkdir build; cmake -S . -B build; cd build; make
    % ./gradlew build

### Running

The main interfaces for loading and using WeaverDB are [WeaverInitializer](https://github.com/weaverdb/weaverdb/blob/main/pgjava_c/src/main/java/org/weaverdb/WeaverInitializer.java) and [Connection](https://github.com/weaverdb/weaverdb/blob/main/pgjava_c/src/main/java/org/weaverdb/Connection.java).  Once the database directory has been created with initdb, the Java native library can be loaded via the initializer

    Properties prop = new Properties();
    prop.setProperty("datadir", System.getProperty("user.dir") + "/build/testdb");
    WeaverInitializer.initialize(prop);

### Dependencies

Currently built with cmake, gradle, clang and java on MacOS for MacOS or Android.  Requires Java 17 or greater.  

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

