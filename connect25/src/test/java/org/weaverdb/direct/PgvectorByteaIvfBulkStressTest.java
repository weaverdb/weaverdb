/*-------------------------------------------------------------------------
 *
 * Long-running bytea embedding stress via IVFFlat: bulk insert, functional
 * index, ANN query, delete half, then VACUUM (Java 25 / FFM connect25).
 *
 * Scale defaults (override with -Dweaver.vector.ivf.count / .dim / .lists /
 * .probes):
 *   100_000 rows × 64-dim float32 packed into bytea via DenseVector.
 *   lists defaults to rows/1000; probes defaults to max(10, lists/10).
 * IVFFlat kmeans needs SortMem (weaver.sortmem, default 131072 kB).
 *
 * Run alone (can take tens of minutes at default scale):
 *   ./gradlew :connect25:test --tests 'org.weaverdb.direct.PgvectorByteaIvfBulkStressTest'
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
import org.weaverdb.Input;
import org.weaverdb.Output;
import org.weaverdb.Statement;
import org.weaverdb.vector.pg.DenseVector;

@TestMethodOrder(MethodOrderer.OrderAnnotation.class)
@Timeout(value = 120, unit = TimeUnit.MINUTES)
public class PgvectorByteaIvfBulkStressTest {

    private static final String TABLE = "pv_ba_ivf_bulk_w25";

    /** Row count; override with -Dweaver.vector.ivf.count=N. */
    private static final int ROW_COUNT =
            Integer.getInteger("weaver.vector.ivf.count", 100_000);

    /** Embedding dimensions; override with -Dweaver.vector.ivf.dim=D. */
    private static final int DIM =
            Integer.getInteger("weaver.vector.ivf.dim", 64);

    /**
     * IVFFlat lists; override with -Dweaver.vector.ivf.lists=L.
     * Default follows the pgvector heuristic lists ≈ rows/1000 for this scale.
     */
    private static final int LISTS =
            Integer.getInteger("weaver.vector.ivf.lists",
                    Math.max(1, ROW_COUNT / 1000));

    /**
     * IVFFlat probes at query time; override with -Dweaver.vector.ivf.probes=P.
     * Default probes enough lists for reliable nearest-neighbor on spiked vectors.
     */
    private static final int PROBES =
            Integer.getInteger("weaver.vector.ivf.probes",
                    Math.max(10, LISTS / 10));

    /** Stable query target that survives deleting even ids. */
    private static final int QUERY_ID = 1;

    @BeforeAll
    public static void setup() throws Throwable {
        Assertions.assertTrue(ROW_COUNT >= 1_000, "ivf bulk count must be at least 1000");
        Assertions.assertTrue(ROW_COUNT % 2 == 0, "ivf bulk count must be even");
        Assertions.assertTrue(DIM >= 8, "dim must be at least 8");
        Assertions.assertTrue(LISTS >= 1 && LISTS <= ROW_COUNT,
                "lists must be in [1, ROW_COUNT]");
        Assertions.assertTrue(PROBES >= 1 && PROBES <= LISTS,
                "probes must be in [1, lists]");
        Assertions.assertTrue(QUERY_ID % 2 == 1, "QUERY_ID must be odd to survive delete");
        PgvectorWeaverTestSupport.ensureInitialized();
        try (DBReference conn = DBReference.connect("template1")) {
            exec(conn, "create table " + TABLE + " (id int, emb bytea)");
        }
        System.out.println("PgvectorByteaIvfBulkStressTest: rows=" + ROW_COUNT
                + " dim=" + DIM + " lists=" + LISTS + " probes=" + PROBES
                + " queryId=" + QUERY_ID);
    }

    @Test
    @Order(1)
    public void insertLargeNumberOfByteaVectors() throws Exception {
        long t0 = System.nanoTime();
        int progressEvery = Math.max(10_000, ROW_COUNT / 20);
        try (DBReference conn = DBReference.connect("template1");
                Statement s = conn.statement(
                        "insert into " + TABLE + " values ($id, $emb)")) {
            Input<Integer> idIn = s.linkInput("id", Integer.class);
            Input<byte[]> embIn = s.linkInput("emb", byte[].class);
            for (int i = 0; i < ROW_COUNT; i++) {
                idIn.set(i);
                embIn.set(DenseVector.encode(vectorFor(i)));
                s.execute();
                if (i > 0 && i % progressEvery == 0) {
                    System.out.println("inserted " + i + "/" + ROW_COUNT
                            + " (" + elapsedMs(t0) + " ms)");
                }
            }
        }
        int count = queryInt("select count(*) from " + TABLE);
        Assertions.assertEquals(ROW_COUNT, count);
        System.out.println("insert finished in " + elapsedMs(t0) + " ms, count=" + count);
    }

    @Test
    @Order(2)
    public void createIvfflatFunctionalIndexOnBytea() throws Exception {
        long t0 = System.nanoTime();
        try (DBReference conn = DBReference.connect("template1")) {
            exec(conn,
                    "create index " + TABLE + "_ivf on " + TABLE
                            + " using ivfflat (bytea_to_vector(emb) vector_l2_ops)"
                            + " with (lists = " + LISTS + ")");
        }
        System.out.println("ivfflat index created in " + elapsedMs(t0)
                + " ms (lists=" + LISTS + ")");
    }

    @Test
    @Order(3)
    public void queryNearestNeighborViaIvfflatIndex() throws Exception {
        long t0 = System.nanoTime();
        List<Integer> nearest = orderByIndexed(
                DenseVector.encode(vectorFor(QUERY_ID)), 5);
        Assertions.assertFalse(nearest.isEmpty(), "ANN query returned no rows");
        Assertions.assertEquals(QUERY_ID, nearest.get(0).intValue(),
                "expected id " + QUERY_ID + " as nearest neighbor, got " + nearest);
        System.out.println("query finished in " + elapsedMs(t0) + " ms, top=" + nearest);
    }

    @Test
    @Order(4)
    public void deleteHalfTheRows() throws Exception {
        long t0 = System.nanoTime();
        long deleted;
        try (DBReference conn = DBReference.connect("template1")) {
            deleted = conn.execute(
                    "delete from " + TABLE + " where id % 2 = 0");
        }
        Assertions.assertEquals(ROW_COUNT / 2, deleted);
        int remaining = queryInt("select count(*) from " + TABLE);
        Assertions.assertEquals(ROW_COUNT / 2, remaining);
        Assertions.assertEquals(0, queryInt(
                "select count(*) from " + TABLE + " where id % 2 = 0"));
        System.out.println("delete half finished in " + elapsedMs(t0)
                + " ms, deleted=" + deleted + " remaining=" + remaining);
    }

    @Test
    @Order(5)
    public void vacuumAfterHalfDelete() throws Exception {
        long t0 = System.nanoTime();
        try (DBReference conn = DBReference.connect("template1")) {
            exec(conn, "vacuum " + TABLE);
        }
        System.out.println("vacuum finished in " + elapsedMs(t0) + " ms");

        Assertions.assertEquals(ROW_COUNT / 2,
                queryInt("select count(*) from " + TABLE));
        Assertions.assertEquals(0, queryInt(
                "select count(*) from " + TABLE + " where id % 2 = 0"));

        List<Integer> nearest = orderByIndexed(
                DenseVector.encode(vectorFor(QUERY_ID)), 5);
        Assertions.assertFalse(nearest.isEmpty());
        Assertions.assertEquals(QUERY_ID, nearest.get(0).intValue());
        Assertions.assertTrue(nearest.stream().noneMatch(id -> id % 2 == 0),
                "deleted even ids must not appear after vacuum: " + nearest);
        System.out.println("post-vacuum query top=" + nearest);
    }

    /**
     * Deterministic unit-ish vectors: unit spike on axis {@code id % DIM},
     * plus a tiny id-dependent offset so neighbors are unique.
     */
    private static float[] vectorFor(int id) {
        float[] v = new float[DIM];
        int axis = id % DIM;
        v[axis] = 1.0f + (id * 1.0e-4f);
        return v;
    }

    private static long elapsedMs(long startNanos) {
        return (System.nanoTime() - startNanos) / 1_000_000L;
    }

    private static List<Integer> orderByIndexed(byte[] query, int limit) throws Exception {
        List<Integer> rows = new ArrayList<>();
        try (DBReference conn = DBReference.connect("template1")) {
            exec(conn, "set ivfflat.probes = " + PROBES);
            exec(conn, "set enable_seqscan = off");
            try (Statement s = conn.statement(
                    "select id from " + TABLE
                            + " order by bytea_to_vector(emb) <-> bytea_to_vector($q) limit "
                            + limit)) {
                s.linkInput("q", byte[].class).set(query);
                Output<Integer> out = s.linkOutput(1, Integer.class);
                s.execute();
                while (s.fetch()) {
                    Integer v = out.get();
                    if (v != null) {
                        rows.add(v);
                    }
                }
            } finally {
                // enable_seqscan is per-connection (Env-local CostInfo); restore on
                // this connection so later statements here use the default planner.
                exec(conn, "set enable_seqscan = on");
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
