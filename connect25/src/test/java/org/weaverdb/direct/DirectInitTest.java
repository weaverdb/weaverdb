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

import java.io.Writer;
import java.util.Properties;
import org.junit.jupiter.api.AfterAll;
import org.weaverdb.DBReference;
import org.weaverdb.FetchSet;
import org.weaverdb.Statement;

public class DirectInitTest {

    @org.junit.jupiter.api.Test
    public void testInit() throws Throwable{
        try {
                ProcessBuilder b = new ProcessBuilder("pwd");
                b.inheritIO();
                Process p = b.start();
                p.waitFor();
                
                b = new ProcessBuilder("rm", "-rf", "build/mtpg");
                b.inheritIO();
                p = b.start();
                p.waitFor();
                
                b = new ProcessBuilder("cp", "-rf", "../build/mtpg", "build/");
                b.inheritIO();
                p = b.start();
                p.waitFor();

                b = new ProcessBuilder("rm", "-rf", System.getProperty("user.dir") + "/build/testdb");
                b.inheritIO();
                p = b.start();
                p.waitFor();
                b = new ProcessBuilder("build/mtpg/bin/initdb","-D", System.getProperty("user.dir") + "/build/testdb");
                b.inheritIO();
                p = b.start();
                p.waitFor();
                b = new ProcessBuilder("build/mtpg/bin/postgres", "-D", System.getProperty("user.dir") + "/build/testdb", "-o", "/dev/null", "template1");
                b.redirectOutput(ProcessBuilder.Redirect.INHERIT);
                b.redirectError(ProcessBuilder.Redirect.INHERIT);
                p = b.start();
                try (Writer w = p.outputWriter()) {
                    w.append("create database test;\n").flush();
                }
                p.waitFor();

                Properties prop = new Properties();
                prop.setProperty("datadir", System.getProperty("user.dir") + "/build/testdb");

                prop.setProperty("start_delay", "10");
                prop.setProperty("stdlog", "TRUE");
                prop.setProperty("disable_crc", "TRUE");
                
                DirectWeaverInitializer.initialize(prop); 
                
                try (DBReference conn = DBReference.connect("template1")) {
                    try (Statement s = conn.statement("select xmin,xmax,oid,* from pg_type where oid = 16")) {
                        FetchSet.stream(s).flatMap(FetchSet.Row::stream).forEach(
                                i-> {
                                    System.out.println(i);
                                }
                                     
                        );
                    }
                }

        } finally {

        }
    }

    @AfterAll
    public static void close() {
            DirectWeaverInitializer.forceShutdown();
    }
}
