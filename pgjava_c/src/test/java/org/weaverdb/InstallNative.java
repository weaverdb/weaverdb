/*
 * Copyright (c) 2024 Myron Scott <myron@weaverdb.org> All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 */

package org.weaverdb;

import java.io.Writer;
import java.util.Properties;
import java.util.concurrent.locks.Lock;
import java.util.concurrent.locks.ReentrantLock;
import org.junit.jupiter.api.extension.BeforeAllCallback;
import org.junit.jupiter.api.extension.ExtendWith;
import org.junit.jupiter.api.extension.ExtensionContext;
import static org.junit.jupiter.api.extension.ExtensionContext.Namespace.GLOBAL;

@ExtendWith({InstallNative.class})
public class InstallNative implements BeforeAllCallback, ExtensionContext.Store.CloseableResource {

    private static final Lock LOCK = new ReentrantLock();

    private boolean owner = false;
    @Override
    public void beforeAll(ExtensionContext context) {
        LOCK.lock();
        try {
            if (context.getRoot().getStore(GLOBAL).get("RunOnce") != null) {
                return;
            }
                ProcessBuilder b = new ProcessBuilder("pwd");
                b.inheritIO();
                Process p = b.start();
                p.waitFor();
                
                b = new ProcessBuilder("rm", "-rf", "build/mtpg");
                b.inheritIO();
                p = b.start();
                p.waitFor();
                
                b = new ProcessBuilder("cp", "-rf", "../cbuild/mtpg", "build/");
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
                prop.setProperty("allow_anonymous", "true");
                prop.setProperty("start_delay", "10");
                prop.setProperty("debuglevel", "DEBUG");
                prop.setProperty("stdlog", "TRUE");

                prop.setProperty("disable_crc", "TRUE");
                WeaverInitializer.initialize(prop);
                owner = true;
                context.getRoot().getStore(GLOBAL).put("RunOnce", this);
        } catch (Exception io) {
        
        } finally {
            LOCK.unlock();
        }
    }

    @Override
    public void close() {
        if (owner) {
            WeaverInitializer.close(true);
        }
    }
}
