package driver.weaver;

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
                b = new ProcessBuilder("tar", "xvf", "../mtpgsql/src/mtpg.tar.bz2", "-C", "build");
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
                b = new ProcessBuilder("build/mtpg/bin/postgres", "-D", System.getProperty("user.dir") + "/build/testdb", "template1");
                b.redirectOutput(ProcessBuilder.Redirect.INHERIT);
                b.redirectError(ProcessBuilder.Redirect.INHERIT);
                p = b.start();
                try (Writer w = p.outputWriter()) {
                    w.append("create database test;\n").flush();
                }
                p.waitFor();
//        b = new ProcessBuilder("cp","libweaver.dylib", System.getProperty("user.dir") + "/build/libs/");
//        b.inheritIO();
//        p = b.start();
//        p.waitFor();
                Properties prop = new Properties();
                prop.setProperty("datadir", System.getProperty("user.dir") + "/build/testdb");
                prop.setProperty("allow_anonymous", "true");
                prop.setProperty("start_delay", "10");
                prop.setProperty("debuglevel", "DEBUG");
                prop.setProperty("stdlog", "TRUE");
                prop.setProperty("logfile", System.getProperty("user.dir") + "/build/weaver_debug.txt");
                
//        prop.setProperty("index_corruption", "IGNORE");
//        prop.setProperty("heap_corruption", "IGNORE");
                prop.setProperty("disable_crc", "TRUE");
//        prop.setProperty("usegc", "FALSE");
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
