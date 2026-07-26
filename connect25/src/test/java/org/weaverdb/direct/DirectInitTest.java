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

import java.util.Properties;
import org.weaverdb.DBReference;
import org.weaverdb.ExecutionException;
import org.weaverdb.FetchSet;
import org.weaverdb.Output;
import org.weaverdb.Statement;

public class DirectInitTest {

    @org.junit.jupiter.api.BeforeAll
    public static void setup() throws Throwable {
        if (DirectWeaverInitializer.isBackendLoaded()) {
            return;
        }
        try {
                java.nio.file.Path connect25 = java.nio.file.Path.of(System.getProperty("user.dir")).toAbsolutePath();
                java.nio.file.Path repoRoot = connect25.getParent();
                java.nio.file.Path mtpgSrc = repoRoot.resolve("build_test/mtpg");

                ProcessBuilder b = new ProcessBuilder("rm", "-rf", connect25.resolve("build/mtpg").toString());
                b.inheritIO().start().waitFor();

                b = new ProcessBuilder("cp", "-rf", mtpgSrc.toString(),
                        connect25.resolve("build/mtpg").toString());
                b.inheritIO().start().waitFor();

                String dbDir = connect25.resolve("build/testdb").toString();
                b = new ProcessBuilder("rm", "-rf", dbDir);
                b.inheritIO().start().waitFor();
                b = new ProcessBuilder(connect25.resolve("build/mtpg/bin/initdb").toString(), "-D", dbDir);
                b.inheritIO().start().waitFor();

                Properties prop = new Properties();
                prop.setProperty("datadir", dbDir);

                prop.setProperty("start_delay", "10");
                prop.setProperty("stdlog", "TRUE");
                prop.setProperty("disable_crc", "TRUE");
                
                DirectWeaverInitializer.initialize(prop);

                try (DBReference conn = DBReference.connect("template1");
                        Statement s = conn.statement("create database test")) {
                    s.execute();
                } catch (ExecutionException e) {
                    /* database may already exist on re-run */
                }

        } finally {

        }
    }
    
    @org.junit.jupiter.api.Test
    public void testStreamExec() throws Exception {
        try (DBReference conn = DBReference.connect("template1")) {
            conn.setStandardOutput(System.out);
            conn.stream("select 1 as one");
        }
    }

    @org.junit.jupiter.api.Test
    public void testBind() throws Exception {
        try (DBReference conn = DBReference.connect("template1")) {
            try (Statement s = conn.statement("select xmin,xmax,oid,* from pg_type where oid = 16")) {
                FetchSet.stream(s).flatMap(FetchSet.Row::stream).forEach(
                        i-> {
                            System.out.println(i);
                        }

                );
            }
        }
}

    @org.junit.jupiter.api.Test
    public void testBadBind() throws Exception {
        try (DBReference conn = DBReference.connect("test")) {
            try (Statement s = conn.statement("select * from pg_database;")) {
                Output<Integer> b = s.linkOutput(1, Integer.class);
                Output<Integer> c = s.linkOutput(2, Integer.class);
                Output<Integer> d = s.linkOutput(3, Integer.class);
                Output<Integer> e = s.linkOutput(4, Integer.class);
                Output<Integer> f = s.linkOutput(5, Integer.class);
                System.out.println(s.execute());
                s.fetch();
                System.out.println(b.getName() + "=" + b.get());
                System.out.println(c.getName() + "=" + c.get());
                System.out.println(d.getName() + "=" + d.get());
                System.out.println(e.getName() + "=" + e.get());
                System.out.println(f.getName() + "=" + f.get());
            } catch (ExecutionException we) {
                we.printStackTrace();
                // expected
            }
        }
    }
    /*
     * Leave the embedded backend up for later test classes (pgvector suite).
     * JVM exit cleans up; forceShutdown here breaks re-init in the same process.
     */
}
