/*-------------------------------------------------------------------------
 *
 * FFM entry for the same two-connection relhasindex scenario.
 *
 *-------------------------------------------------------------------------
 */

package org.weaverdb.direct;

import java.time.Duration;
import java.util.Properties;
import java.util.concurrent.TimeUnit;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.Timeout;
import org.weaverdb.RelhasindexInvalidationHarness;
import org.weaverdb.RelhasindexInvalidationTest;

public class RelhasindexInvalidationFfmTest {

    @Test
    @Timeout(value = 6, unit = TimeUnit.MINUTES)
    public void otherConnectionSeesIndexAfterCreate() throws Exception {
        RelhasindexInvalidationTest.runChild(RelhasindexInvalidationFfmTest.class.getName());
    }

    public static void main(String[] args) {
        if (args.length != 2) {
            System.err.println("usage: RelhasindexInvalidationFfmTest <datadir> <trials>");
            System.exit(2);
        }
        int rc = RelhasindexInvalidationHarness.run(
                java.nio.file.Path.of(args[0]),
                Integer.parseInt(args[1]),
                new RelhasindexInvalidationHarness.Engine() {
                    @Override
                    public void initialize(Properties props) {
                        DirectWeaverInitializer.initialize(props);
                    }

                    @Override
                    public void shutdown() throws Exception {
                        DirectWeaverInitializer.shutdown(Duration.ofSeconds(30));
                    }
                });
        System.exit(rc);
    }
}
