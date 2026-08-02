/*-------------------------------------------------------------------------
 *
 * Parallel HNSW + IVFFlat index builds (pthread / DOL-style worker Envs).
 *
 * Forces workers=2 and inserts enough heap pages to clear
 * HNSW_MIN_BLOCKS_PER_WORKER / IVFFLAT_MIN_BLOCKS_PER_WORKER (8 blocks each
 * → ≥16 pages for 2 workers). Asserts ANN correctness after each parallel
 * build. The companion shell smoke greps the "parallel …" NOTICE lines to
 * prove the pthread path ran (serial fallback would stay silent).
 *
 *   ./gradlew :connect25:test --tests 'org.weaverdb.direct.PgvectorParallelBuildTest'
 *
 *-------------------------------------------------------------------------
 */

package org.weaverdb.direct;

import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.TimeUnit;
import org.junit.jupiter.api.Assertions;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.MethodOrderer;
import org.junit.jupiter.api.Order;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.TestMethodOrder;
import org.junit.jupiter.api.Timeout;
import org.weaverdb.DBReference;
import org.weaverdb.Output;
import org.weaverdb.Statement;

@TestMethodOrder(MethodOrderer.OrderAnnotation.class)
@Timeout(value = 20, unit = TimeUnit.MINUTES)
public class PgvectorParallelBuildTest {

    private static final String HNSW_TABLE = "pv_par_hnsw_w25";
    private static final String IVF_TABLE = "pv_par_ivf_w25";

    /**
     * Rows sized so a 32-d vector heap spans ≥16 pages (2 workers × 8 min
     * blocks). Override with -Dweaver.vector.parallel.count=N.
     */
    private static final int ROW_COUNT =
            Integer.getInteger("weaver.vector.parallel.count", 4_000);

    private static final int DIM = 32;
    private static final int WORKERS = 2;
    private static final int QUERY_ID = 1;
    private static final int MIN_PAGES = WORKERS * 8;

    @BeforeAll
    public static void setup() throws Throwable {
        Assertions.assertTrue(ROW_COUNT >= 1_000, "need enough rows for ≥16 heap pages");
        PgvectorWeaverTestSupport.ensureInitialized();
        try (DBReference conn = DBReference.connect("template1")) {
            exec(conn, "create table " + HNSW_TABLE + " (id int, emb vector)");
            exec(conn, "create table " + IVF_TABLE + " (id int, emb vector)");
        }
    }

    @Test
    @Order(1)
    public void insertRowsForBothTables() throws Exception {
        insertSpikes(HNSW_TABLE);
        insertSpikes(IVF_TABLE);
        try (DBReference conn = DBReference.connect("template1")) {
            exec(conn, "vacuum " + HNSW_TABLE);
            exec(conn, "vacuum " + IVF_TABLE);
        }
        /*
         * Page-count gate (≥16 for workers=2) is asserted in
         * pgvector_parallel_build_smoke.sh via relpages. Heap size here is
         * sized the same way (4000 × 32-d); smoke proved ~76 pages.
         */
        System.out.println("tables loaded + vacuumed; expecting ≥" + MIN_PAGES
                + " heap pages for parallel workers=" + WORKERS);
    }

    @Test
    @Order(2)
    public void parallelHnswBuildAndQuery() throws Exception {
        long t0 = System.nanoTime();
        try (DBReference conn = DBReference.connect("template1")) {
            exec(conn, "set hnsw.build_workers = " + WORKERS);
            exec(conn,
                    "create index " + HNSW_TABLE + "_idx on " + HNSW_TABLE
                            + " using hnsw (emb vector_l2_ops)"
                            + " with (m = 8, ef_construction = 32)");
            exec(conn, "set hnsw.build_workers = 1");
        }
        System.out.println("hnsw parallel create index in "
                + ((System.nanoTime() - t0) / 1_000_000L) + " ms");

        try (DBReference conn = DBReference.connect("template1")) {
            exec(conn, "set hnsw.ef_search = 100");
            exec(conn, "set enable_seqscan = off");
        }
        List<Integer> nearest = orderBy(HNSW_TABLE, vectorLiteral(QUERY_ID), 5);
        Assertions.assertFalse(nearest.isEmpty());
        Assertions.assertEquals(QUERY_ID, nearest.get(0).intValue(),
                "parallel HNSW ANN nearest, got " + nearest);
        System.out.println("hnsw ANN top=" + nearest);
    }

    @Test
    @Order(3)
    public void parallelIvfflatBuildAndQuery() throws Exception {
        int lists = Math.max(4, (int) Math.round(Math.sqrt(ROW_COUNT)));
        int probes = Math.max(4, Math.min(lists, lists / 4));

        long t0 = System.nanoTime();
        try (DBReference conn = DBReference.connect("template1")) {
            exec(conn, "set ivfflat.assign_workers = " + WORKERS);
            exec(conn,
                    "create index " + IVF_TABLE + "_idx on " + IVF_TABLE
                            + " using ivfflat (emb vector_l2_ops)"
                            + " with (lists = " + lists + ")");
            exec(conn, "set ivfflat.assign_workers = 1");
        }
        System.out.println("ivfflat parallel create index in "
                + ((System.nanoTime() - t0) / 1_000_000L) + " ms (lists=" + lists + ")");

        try (DBReference conn = DBReference.connect("template1")) {
            exec(conn, "set ivfflat.probes = " + probes);
            exec(conn, "set enable_seqscan = off");
        }
        List<Integer> nearest = orderBy(IVF_TABLE, vectorLiteral(QUERY_ID), 5);
        Assertions.assertFalse(nearest.isEmpty());
        Assertions.assertEquals(QUERY_ID, nearest.get(0).intValue(),
                "parallel IVFFlat ANN nearest, got " + nearest);
        System.out.println("ivf ANN top=" + nearest);
    }

    private static void insertSpikes(String table) throws Exception {
        long t0 = System.nanoTime();
        try (DBReference conn = DBReference.connect("template1")) {
            for (int i = 0; i < ROW_COUNT; i++) {
                exec(conn, "insert into " + table + " values (" + i + ", '"
                        + vectorLiteral(i) + "')");
            }
        }
        Assertions.assertEquals(ROW_COUNT, queryInt("select count(*) from " + table));
        System.out.println("inserted " + ROW_COUNT + " into " + table
                + " in " + ((System.nanoTime() - t0) / 1_000_000L) + " ms");
    }

    /** Unit spike on axis id%DIM; last coord = id so nearest is unambiguous. */
    private static String vectorLiteral(int id) {
        StringBuilder sb = new StringBuilder(DIM * 8);
        sb.append('[');
        int axis = id % DIM;
        for (int d = 0; d < DIM; d++) {
            if (d > 0) {
                sb.append(',');
            }
            if (d == DIM - 1) {
                sb.append(id);
            } else if (d == axis) {
                sb.append('1');
            } else {
                sb.append('0');
            }
        }
        sb.append(']');
        return sb.toString();
    }

    private static List<Integer> orderBy(String table, String queryVec, int limit)
            throws Exception {
        List<Integer> rows = new ArrayList<>();
        try (DBReference conn = DBReference.connect("template1");
                Statement s = conn.statement(
                        "select id from " + table + " order by emb <-> '"
                                + queryVec + "' limit " + limit)) {
            Output<Integer> out = s.linkOutput(1, Integer.class);
            s.execute();
            while (s.fetch()) {
                Integer v = out.get();
                if (v != null) {
                    rows.add(v);
                }
            }
        }
        return rows;
    }

    private static int queryInt(String sql) throws Exception {
        try (DBReference conn = DBReference.connect("template1");
                Statement s = conn.statement(sql)) {
            Output<Integer> out = s.linkOutput(1, Integer.class);
            s.execute();
            Assertions.assertTrue(s.fetch());
            return out.get().intValue();
        }
    }

    private static void exec(DBReference conn, String sql) throws Exception {
        try (Statement s = conn.statement(sql)) {
            s.execute();
        }
    }
}
