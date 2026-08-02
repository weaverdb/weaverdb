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
import java.util.List;
import org.junit.jupiter.api.Assertions;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.extension.ExtendWith;

/**
 * Lazy vacuum index/heap crash-consistency coverage.
 *
 * Verifies that after delete + vacuum, indexes stay consistent with the heap:
 * remaining keys are findable, deleted keys can be re-inserted (uniqueness),
 * and a second vacuum is restartable (incomplete-cycle safe).
 *
 * Crash-injection scenarios (WEAVER_VACUUM_CRASH_POINT / vacuum_crash_point)
 * are exercised by mtpgsql/scripts/vacuum_crash_ordering_smoke.sh.
 */
@ExtendWith({InstallNative.class})
public class VacuumIndexCrashConsistencyTest {

    @Test
    public void deleteVacuumKeepsIndexConsistent() throws Exception {
        try (DBReference conn = DBReferenceManager.connect("test")) {
            conn.execute("create table vac_crash_t (id int4, val varchar(64))");
            conn.execute("create unique index vac_crash_t_id_idx on vac_crash_t (id)");
            for (int i = 1; i <= 20; i++) {
                conn.execute("insert into vac_crash_t (id, val) values (" + i + ", 'v" + i + "')");
            }
            conn.execute("delete from vac_crash_t where id % 2 = 0");
            conn.execute("vacuum vac_crash_t");

            List<Integer> ids = new ArrayList<>();
            try (Statement s = conn.statement("select id from vac_crash_t order by id")) {
                Output<Integer> id = s.linkOutput(1, Integer.class);
                s.execute();
                while (s.fetch()) {
                    ids.add(id.get());
                }
            }
            Assertions.assertFalse(ids.isEmpty(), "expected odd ids to remain");
            for (Integer id : ids) {
                Assertions.assertEquals(1, id % 2, "only odd ids should remain after delete");
            }

            // Deleted even keys must be re-insertable (no stale unique index TID).
            conn.execute("insert into vac_crash_t (id, val) values (2, 're2')");
            conn.execute("insert into vac_crash_t (id, val) values (4, 're4')");

            // Second vacuum must be restartable after a completed cycle.
            conn.execute("vacuum vac_crash_t");

            try (Statement s = conn.statement("select id from vac_crash_t where id = 2")) {
                Output<Integer> id = s.linkOutput(1, Integer.class);
                s.execute();
                Assertions.assertTrue(s.fetch(), "reinserted id=2 must be visible via index/heap");
                Assertions.assertEquals(2, id.get().intValue());
                Assertions.assertFalse(s.fetch(), "exactly one row for id=2");
            }
        }
    }
}
