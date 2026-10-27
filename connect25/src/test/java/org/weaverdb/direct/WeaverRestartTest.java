/*-------------------------------------------------------------------------
 *
 *
 * Copyright (c) 2000-2024, Myron Scott  <myron@weaverdb.org>
 *
 * All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 *
 *-------------------------------------------------------------------------
 */

package org.weaverdb.direct;

import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.time.Duration;
import java.util.Properties;
import java.util.concurrent.TimeUnit;
import org.junit.jupiter.api.Assertions;
import org.junit.jupiter.api.Test;
import org.weaverdb.DBReference;
import org.weaverdb.Statement;

/**
 * Shutdown and initialize again in one process. The engine keeps process-global
 * caches, mutexes, and the datadir lock; a second boot has to find them reset.
 * The work runs in a child JVM so the rest of the suite can keep a backend up.
 */
public class WeaverRestartTest {

    @Test
    public void shutdownThenInitializeKeepsRowsAndLock() throws Exception {
        Path connect25 = Path.of(System.getProperty("user.dir")).toAbsolutePath();
        Path initdb = connect25.getParent().resolve("build_test/mtpg/bin/initdb");
        Assertions.assertTrue(Files.isExecutable(initdb), "missing initdb at " + initdb);

        Path data = connect25.resolve("build/restart_test_db");
        deleteTree(data);
        ProcessBuilder init = new ProcessBuilder(initdb.toString(), "-D", data.toString());
        init.redirectErrorStream(true);
        Process initProc = init.start();
        String initOut = readAll(initProc.getInputStream());
        int initRc = initProc.waitFor();
        Assertions.assertEquals(0, initRc, "initdb failed\n" + initOut);

        String lib = System.getProperty("java.library.path");
        Assertions.assertNotNull(lib, "java.library.path");
        ProcessBuilder child = new ProcessBuilder(
                Path.of(System.getProperty("java.home"), "bin", "java").toString(),
                "--enable-native-access=ALL-UNNAMED",
                "-Djava.library.path=" + lib,
                "-cp", System.getProperty("java.class.path"),
                "org.weaverdb.direct.WeaverRestartTest",
                data.toString());
        child.redirectErrorStream(true);
        Process proc = child.start();
        /*
         * Drain stdout on another thread. Reading it on this thread blocks
         * until the child exits, so a hung shutdown would never hit the timeout.
         */
        StringBuilder output = new StringBuilder();
        Thread reader = new Thread(() -> {
            try {
                output.append(readAll(proc.getInputStream()));
            } catch (Exception e) {
                output.append(e);
            }
        });
        reader.setDaemon(true);
        reader.start();
        boolean finished = proc.waitFor(120, TimeUnit.SECONDS);
        if (!finished) {
            proc.destroyForcibly();
            reader.join(5_000);
            Assertions.fail("restart child timed out\n" + output);
        }
        reader.join(5_000);
        Assertions.assertEquals(0, proc.exitValue(), output.toString());
        Assertions.assertTrue(output.toString().contains("restart ok"), output.toString());
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 1) {
            System.err.println("usage: WeaverRestartTest <datadir>");
            System.exit(2);
        }
        try {
            runRestart(args[0]);
        } catch (Throwable t) {
            t.printStackTrace(System.out);
            System.exit(1);
        }
    }

    private static void runRestart(String dataDir) throws Exception {
        Properties props = new Properties();
        props.setProperty("datadir", dataDir);
        props.setProperty("stdlog", "TRUE");
        props.setProperty("disable_crc", "TRUE");
        props.setProperty("synctimeout", "1000");
        Path lock = Path.of(dataDir, "LOCK");

        /*
         * Six boots allocate more private shared-memory slots than the
         * 16-entry table if shutdown does not free them.
         */
        for (int cycle = 1; cycle <= 6; cycle++) {
            DirectWeaverInitializer.initialize(props);
            Assertions.assertTrue(DirectWeaverInitializer.isBackendLoaded(),
                    "backend not loaded on cycle " + cycle);
            Assertions.assertTrue(Files.exists(lock),
                    "datadir lock missing after initialize, cycle " + cycle);

            try (DBReference conn = DBReference.connect("template1")) {
                if (cycle == 1) {
                    conn.execute("create table restart_probe (id int4)");
                }
                int found = count(conn);
                Assertions.assertEquals(cycle - 1, found,
                        "rows before insert on cycle " + cycle);
                conn.execute("insert into restart_probe (id) values (" + cycle + ")");
                Assertions.assertEquals(cycle, count(conn),
                        "rows after insert on cycle " + cycle);
            }

            DirectWeaverInitializer.shutdown(Duration.ofSeconds(30));
            Assertions.assertFalse(DirectWeaverInitializer.isBackendLoaded(),
                    "backend still loaded after shutdown, cycle " + cycle);
            Assertions.assertFalse(Files.exists(lock),
                    "datadir lock still held after shutdown, cycle " + cycle);
        }
        System.out.println("restart ok");
    }

    private static int count(DBReference conn) throws Exception {
        try (Statement statement = conn.statement("select id from restart_probe")) {
            statement.linkOutput(1, Integer.class);
            statement.execute();
            int n = 0;
            while (statement.fetch()) {
                n++;
            }
            return n;
        }
    }

    private static String readAll(InputStream in) throws Exception {
        return new String(in.readAllBytes(), StandardCharsets.UTF_8);
    }

    private static void deleteTree(Path root) throws Exception {
        if (!Files.exists(root)) {
            return;
        }
        try (var walk = Files.walk(root)) {
            walk.sorted((a, b) -> b.compareTo(a)).forEach(path -> {
                try {
                    Files.deleteIfExists(path);
                } catch (Exception e) {
                    throw new RuntimeException(e);
                }
            });
        }
    }
}
