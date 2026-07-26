/*-------------------------------------------------------------------------
 *
 * Ensures connect21 (Java 21 JNI factory) runs pgvector regression tests.
 *
 *-------------------------------------------------------------------------
 */

package org.weaverdb;

import java.util.Iterator;
import java.util.ServiceLoader;
import org.junit.jupiter.api.Assertions;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.extension.ExtendWith;

@ExtendWith(InstallNative.class)
public class PgvectorConnect21Test {

    @Test
    public void serviceLoaderSelectsJava21Factory() {
        ServiceLoader<DBReferenceFactory> loader = ServiceLoader.load(DBReferenceFactory.class);
        Iterator<DBReferenceFactory> it = loader.iterator();
        DBReferenceFactory winner = null;
        Runtime.Version running = Runtime.version();
        while (it.hasNext()) {
            DBReferenceFactory candidate = it.next();
            if (Runtime.Version.parse(candidate.builtFor()).compareTo(running) <= 0) {
                if (winner == null || winner.builtFor().compareTo(candidate.builtFor()) < 0) {
                    winner = candidate;
                }
            }
        }
        Assertions.assertNotNull(winner);
        Assertions.assertEquals("21", winner.builtFor());
    }

    @Test
    public void postIndexInsertViaJava21Connection() throws Exception {
        try (DBReference conn = DBReferenceManager.connect("template1")) {
            exec(conn, "create table pv_c21 (id int, emb vector)");
            exec(conn, "insert into pv_c21 values (1, '[1,0,0]')");
            exec(conn,
                    "create index pv_c21_ivf on pv_c21 using ivfflat (emb vector_l2_ops) with (lists = 2)");
            exec(conn, "insert into pv_c21 values (2, '[0,1,0]')");
            try (Statement s = conn.statement(
                    "select id from pv_c21 order by emb <-> '[0,1,0]' limit 1")) {
                Output<Integer> out = s.linkOutput(1, Integer.class);
                s.execute();
                Assertions.assertTrue(s.fetch());
                Assertions.assertEquals(2, out.get().intValue());
            }
        }
    }

    private static void exec(DBReference conn, String sql) throws Exception {
        try (Statement s = conn.statement(sql)) {
            s.execute();
        }
    }
}
