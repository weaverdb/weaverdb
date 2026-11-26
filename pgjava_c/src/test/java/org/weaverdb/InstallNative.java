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

package org.weaverdb;

import java.nio.file.Path;
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
                rollbackQuiet();
                return;
            }
            Path repo = NativeBuild.repoRoot();
            NativeBuild.assertCurrent(repo);
            Path home = NativeBuild.nativeHome();
            Path db = NativeBuild.dataDir();

            ProcessBuilder b = new ProcessBuilder("rm", "-rf", db.toString());
            b.inheritIO().start().waitFor();

            NativeBuild.initdb(home.resolve("bin/initdb"), db, 60);

            Properties prop = new Properties();
            prop.setProperty("datadir", db.toString());
            prop.setProperty("start_delay", "10");
            prop.setProperty("stdlog", "TRUE");
            prop.setProperty("disable_crc", "TRUE");
        prop.setProperty("index_corruption", "IGNORE");
        prop.setProperty("heap_corruption", "IGNORE");
            /* Test-only. Production DBWriter sync wait stays 5000ms. */
            prop.setProperty("synctimeout", "1000");
            WeaverInitializer.initialize(prop);

            try (DBReference conn = DBReferenceManager.connect("template1");
                    Statement s = conn.statement("create database test")) {
                s.execute();
            } catch (ExecutionException e) {
                /* may already exist */
            }

            owner = true;
            context.getRoot().getStore(GLOBAL).put("RunOnce", this);
            rollbackQuiet();
        } catch (Exception e) {
            throw new RuntimeException("InstallNative setup failed", e);
        } finally {
            LOCK.unlock();
        }
    }

    private static void rollbackQuiet() {
        try (DBReference conn = DBReferenceManager.connect("template1");
                Statement s = conn.statement("rollback")) {
            s.execute();
        } catch (Exception ignored) {
        }
    }

    @Override
    public void close() {
        /* Do not wrapup here: Gradle JVM teardown after all tests is enough. */
    }
}
