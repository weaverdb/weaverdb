/*-------------------------------------------------------------------------
 *
 * pgvector smoke test via JNI Weaver (Java 17 factory / BaseWeaverConnection).
 *
 *-------------------------------------------------------------------------
 */

package org.weaverdb;

import org.junit.jupiter.api.Assertions;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.extension.ExtendWith;

@ExtendWith(InstallNative.class)
public class PgvectorSmokeTest {

    @BeforeAll
    public static void setup() {
        // cluster + Weaver JVM init handled by InstallNative
    }

    @Test
    public void vectorInsertIndexAndOrderBy() throws Exception {
        try (DBReference conn = DBReferenceManager.connect("template1")) {
            exec(conn, "create table pv_jni_smoke (id int, emb vector)");
            exec(conn, "insert into pv_jni_smoke values (1, '[1,0,0]')");
            exec(conn, "insert into pv_jni_smoke values (2, '[0,1,0]')");
            exec(conn, "insert into pv_jni_smoke values (3, '[0,0,1]')");
            exec(conn,
                    "create index pv_jni_smoke_ivf on pv_jni_smoke using ivfflat (emb vector_l2_ops) with (lists = 2)");
            exec(conn,
                    "create index pv_jni_smoke_hnsw on pv_jni_smoke using hnsw (emb vector_l2_ops) with (m = 8, ef_construction = 32)");
            try (Statement s = conn.statement(
                    "select id from pv_jni_smoke order by emb <-> '[1,0,0]' limit 2")) {
                Output<Integer> out = s.linkOutput(1, Integer.class);
                int rows = 0;
                int first = -1;
                s.execute();
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
     * typsend must produce a real bytea (dim/unused + float4 payload), not the
     * former no-op stub that returned a null pointer.
     */
    @Test
    public void vectorSendProducesBinaryPayload() throws Exception {
        try (DBReference conn = DBReferenceManager.connect("template1")) {
            try (Statement s = conn.statement(
                    "select octet_length(vector_send('[1,2,3]'::vector))")) {
                Output<Integer> out = s.linkOutput(1, Integer.class);
                s.execute();
                Assertions.assertTrue(s.fetch());
                // int16 dim + int16 unused + 3 * float4
                Assertions.assertEquals(16, out.get().intValue());
            }
            try (Statement s = conn.statement(
                    "select octet_length(halfvec_send('[1,2]'::halfvec))")) {
                Output<Integer> out = s.linkOutput(1, Integer.class);
                s.execute();
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
