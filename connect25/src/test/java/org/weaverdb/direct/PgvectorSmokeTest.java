/*-------------------------------------------------------------------------
 *
 * Smoke test for pgvector via Weaver (inserts; indexes covered by shell tests).
 *
 *-------------------------------------------------------------------------
 */

package org.weaverdb.direct;

import org.junit.jupiter.api.Assertions;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.Test;
import org.weaverdb.DBReference;
import org.weaverdb.Output;
import org.weaverdb.Statement;

public class PgvectorSmokeTest {

    @BeforeAll
    public static void setup() throws Throwable {
        PgvectorWeaverTestSupport.ensureInitialized();
    }

    @Test
    public void vectorInsertIndexAndOrderBy() throws Exception {
        try (DBReference conn = DBReference.connect("template1")) {
            exec(conn, "create table pv_junit (id int, emb vector)");
            exec(conn, "insert into pv_junit values (1, '[1,0,0]')");
            exec(conn, "insert into pv_junit values (2, '[0,1,0]')");
            exec(conn, "insert into pv_junit values (3, '[0,0,1]')");
            exec(conn,
                    "create index pv_junit_ivf on pv_junit using ivfflat (emb vector_l2_ops) with (lists = 2)");
            exec(conn,
                    "create index pv_junit_hnsw on pv_junit using hnsw (emb vector_l2_ops) with (m = 8, ef_construction = 32)");
            try (Statement s = conn.statement(
                    "select id from pv_junit order by emb <-> '[1,0,0]' limit 2")) {
                Output<Integer> out = s.linkOutput(1, Integer.class);
                int rows = 0;
                int first = -1;
                while (s.fetch()) {
                    rows++;
                    if (first < 0) {
                        first = out.get().intValue();
                    }
                }
                Assertions.assertEquals(2, rows);
                Assertions.assertEquals(1, first);
            }
        }
    }

    /**
     * typsend must produce a real bytea (dim/unused + float payload), not the
     * former no-op stub that returned a null pointer.
     */
    @Test
    public void vectorSendProducesBinaryPayload() throws Exception {
        try (DBReference conn = DBReference.connect("template1")) {
            try (Statement s = conn.statement(
                    "select length(vector_send('[1,2,3]'::vector))")) {
                Output<Integer> out = s.linkOutput(1, Integer.class);
                Assertions.assertTrue(s.fetch());
                // int16 dim + int16 unused + 3 * float4
                Assertions.assertEquals(16, out.get().intValue());
            }
            try (Statement s = conn.statement(
                    "select length(halfvec_send('[1,2]'::halfvec))")) {
                Output<Integer> out = s.linkOutput(1, Integer.class);
                Assertions.assertTrue(s.fetch());
                // int16 dim + int16 unused + 2 * float16
                Assertions.assertEquals(8, out.get().intValue());
            }
        }
    }

    private static void exec(DBReference conn, String sql) throws Exception {
        try (Statement s = conn.statement(sql)) {
            s.execute();
        }
    }
}
