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

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Random;
import java.util.Set;
import java.util.TreeSet;
import org.junit.jupiter.api.Assertions;
import org.junit.jupiter.api.RepeatedTest;
import org.junit.jupiter.api.RepetitionInfo;
import org.junit.jupiter.api.extension.ExtendWith;

/**
 * In-process randomized persistence invariants (no crash injection).
 *
 * A FATAL crash would kill the JVM, so restart coverage lives in
 * {@code mtpgsql/scripts/persistence_crash_fuzzer.sh}. This test fuzzes the
 * path that <em>can</em> run inside InstallNative: random delete / vacuum /
 * unique re-insert on a btree, plus HNSW + IVFFlat so vacuum WriteBuffer
 * mutations stay consistent with the heap.
 *
 * Replay a failure with the seed printed in the assertion message.
 */
@ExtendWith({InstallNative.class})
public class PersistenceInvariantFuzzerTest {

    private static final int N = 12;
    private static final long SEED_BASE = 20260824L;

    @RepeatedTest(5)
    public void randomMutationsThenVacuumStayConsistent(RepetitionInfo rep) throws Exception {
        long seed = SEED_BASE + rep.getCurrentRepetition();
        Random rng = new Random(seed);
        String table = "persist_inv_" + rep.getCurrentRepetition();
        Set<Integer> live = new TreeSet<>();
        List<Integer> deleted = new ArrayList<>();

        try (DBReference conn = DBReferenceManager.connect("template1")) {
            conn.execute("create table " + table + " (id int4, val varchar(64), emb vector)");
            conn.execute("create unique index " + table + "_id_idx on " + table + " (id)");
            for (int i = 1; i <= N; i++) {
                conn.execute("insert into " + table
                        + " (id, val, emb) values (" + i + ", 'v" + i + "', '" + vec(i) + "')");
                live.add(i);
            }
            conn.execute("create index " + table
                    + "_hnsw on " + table + " using hnsw (emb vector_l2_ops) with (m = 8, ef_construction = 32)");
            conn.execute("create index " + table
                    + "_ivf on " + table + " using ivfflat (emb vector_l2_ops) with (lists = 2)");

            int nDelete = 3 + rng.nextInt(4);
            List<Integer> shuffled = new ArrayList<>(live);
            Collections.shuffle(shuffled, rng);
            for (int i = 0; i < nDelete; i++) {
                int id = shuffled.get(i);
                conn.execute("delete from " + table + " where id = " + id);
                live.remove(id);
                deleted.add(id);
            }

            if (rng.nextBoolean() && !live.isEmpty()) {
                int id = live.iterator().next();
                conn.execute("update " + table + " set emb = '[0," + id + ",0]' where id = " + id);
            }

            conn.execute("vacuum " + table);

            Assertions.assertEquals(new ArrayList<>(live), queryIds(conn, table),
                    "heap ids after vacuum; seed=" + seed);

            int reinsert = deleted.get(0);
            conn.execute("insert into " + table
                    + " (id, val, emb) values (" + reinsert + ", 're', '" + vec(reinsert) + "')");
            live.add(reinsert);
            deleted.remove(Integer.valueOf(reinsert));

            Assertions.assertEquals(new ArrayList<>(live), queryIds(conn, table),
                    "heap ids after unique reinsert; seed=" + seed);

            conn.execute("set enable_seqscan = off");
            conn.execute("set ivfflat.probes = 2");
            List<Integer> ann = queryAnn(conn, table);
            for (Integer id : ann) {
                Assertions.assertFalse(deleted.contains(id),
                        "ANN returned deleted id=" + id + " seed=" + seed + " ann=" + ann);
                Assertions.assertTrue(live.contains(id),
                        "ANN returned unknown id=" + id + " seed=" + seed);
            }

            conn.execute("vacuum " + table);
            conn.execute("drop table " + table);
        }
    }

    private static String vec(int id) {
        return "[" + id + ",0,0]";
    }

    private static List<Integer> queryIds(DBReference conn, String table) throws Exception {
        List<Integer> ids = new ArrayList<>();
        try (Statement s = conn.statement("select id from " + table + " order by id")) {
            Output<Integer> id = s.linkOutput(1, Integer.class);
            s.execute();
            while (s.fetch()) {
                ids.add(id.get());
            }
        }
        return ids;
    }

    private static List<Integer> queryAnn(DBReference conn, String table) throws Exception {
        List<Integer> ids = new ArrayList<>();
        try (Statement s = conn.statement(
                "select id from " + table + " order by emb <-> '[1,0,0]' limit 8")) {
            Output<Integer> id = s.linkOutput(1, Integer.class);
            s.execute();
            while (s.fetch()) {
                ids.add(id.get());
            }
        }
        return ids;
    }
}
