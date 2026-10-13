package org.weaverdb.direct;

import org.junit.jupiter.api.Assertions;
import org.junit.jupiter.api.Test;
import org.weaverdb.battle.BattleScenarios;

/**
 * Process-isolated FFM cases. Each child boots libweaver itself, so this
 * class does not initialize the shared engine used by the in-process suite.
 */
public class FfmBattleIsolatedTest {

    @Test
    public void restartSurvivesShutdown() throws Exception {
        BattleScenarios.ChildResult result = BattleScenarios.launchChild("ffm", "restart", 90);
        Assertions.assertEquals(0, result.code, result.output);
    }

    @Test
    public void crossConnectionDeadlockDoesNotHang() throws Exception {
        BattleScenarios.ChildResult result = BattleScenarios.launchChild("ffm", "deadlock", 40);
        Assertions.assertNotEquals(4, result.code, "deadlock hung the engine\n" + result.output);
        Assertions.assertTrue(result.code == 0 || result.code == 2, result.output);
    }

    @Test
    public void sharedConnectionDoesNotCrash() throws Exception {
        BattleScenarios.ChildResult result = BattleScenarios.launchChild("ffm", "shared", 40);
        System.out.println(result.output);
        Assertions.assertEquals(0, result.code,
                "foreign thread did not get a clean rejection\n" + result.output);
        Assertions.assertTrue(result.output.contains("owned by a different thread"), result.output);
        Assertions.assertTrue(result.output.contains("shared rows=1"), result.output);
    }

    @Test
    public void cancelStopsRunningStatement() throws Exception {
        BattleScenarios.ChildResult result = BattleScenarios.launchChild("ffm", "cancel", 30);
        System.out.println(result.output);
        Assertions.assertEquals(0, result.code, result.output);
        Assertions.assertTrue(result.output.contains("cancel sleep started"), result.output);
        Assertions.assertTrue(result.output.contains("cancel alive=false"), result.output);
        Assertions.assertTrue(result.output.toLowerCase().contains("cancel"), result.output);
        Assertions.assertTrue(result.output.contains("cancel followup=1"), result.output);
    }

    @Test
    public void multipageUpdateUnderLockStaysConsistent() throws Exception {
        BattleScenarios.ChildResult result = BattleScenarios.launchChild("ffm", "multipage", 90);
        Assertions.assertEquals(0, result.code, result.output);
        Assertions.assertTrue(result.output.contains("multipage ok"), result.output);
        Assertions.assertTrue(result.output.contains("recovered=true"), result.output);
    }

    @Test
    public void vacuumKeepsHeapAndIndexInOneWriteGroup() throws Exception {
        BattleScenarios.ChildResult result = BattleScenarios.launchChild("ffm", "vacuum-group", 90);
        Assertions.assertEquals(0, result.code, result.output);
        Assertions.assertTrue(result.output.contains("vacuum-group ok"), result.output);
        Assertions.assertTrue(result.output.contains("recovered=true"), result.output);
    }

    @Test
    public void hashIndexDoesNotSpin() throws Exception {
        BattleScenarios.ChildResult result = BattleScenarios.launchChild("ffm", "hash-index", 20);
        System.out.println(result.output);
        Assertions.assertNotEquals(4, result.code, "hash index build did not return\n" + result.output);
        Assertions.assertEquals(0, result.code, result.output);
    }

    @Test
    public void helperDoesNotCrash() throws Exception {
        BattleScenarios.ChildResult result = BattleScenarios.launchChild("ffm", "helper", 60);
        System.out.println(result.output);
        Assertions.assertEquals(0, result.code,
                "helper() crashed the process\n" + result.output);
        Assertions.assertTrue(result.output.contains("helper select=1"), result.output);
    }

    @Test
    public void constraintsDoNotCrash() throws Exception {
        BattleScenarios.ChildResult result = BattleScenarios.launchChild("ffm", "constraints", 60);
        System.out.println(result.output);
        Assertions.assertEquals(0, result.code,
                "constraints crashed or failed\n" + result.output);
    }

    @Test
    public void autocommitErrorLeavesConnectionUsable() throws Exception {
        BattleScenarios.ChildResult result = BattleScenarios.launchChild("ffm", "autocommit-error", 60);
        System.out.println(result.output);
        Assertions.assertEquals(0, result.code, result.output);
    }

    @Test
    public void characterRoundTrips() throws Exception {
        BattleScenarios.ChildResult result = BattleScenarios.launchChild("ffm", "char-bind", 60);
        System.out.println(result.output);
        Assertions.assertEquals(0, result.code, result.output);
    }

    @Test
    public void nullIntegerRoundTrips() throws Exception {
        BattleScenarios.ChildResult result = BattleScenarios.launchChild("ffm", "null-int", 60);
        System.out.println(result.output);
        Assertions.assertEquals(0, result.code, result.output);
    }

    @Test
    public void hashDropAfterReinitDoesNotReadPastLog() throws Exception {
        BattleScenarios.ChildResult result = BattleScenarios.launchChild("ffm", "prefetch-drop", 90);
        System.out.println(result.output);
        Assertions.assertNotEquals(4, result.code, "shutdown hung after drop index\n" + result.output);
        Assertions.assertEquals(0, result.code, result.output);
        Assertions.assertTrue(result.output.contains("prefetch-drop ok"), result.output);
        Assertions.assertTrue(result.output.contains("prefetch-drop rows=3"), result.output);
    }

    @Test
    public void concurrentHeapInsertDoesNotTripFreespaceAssert() throws Exception {
        BattleScenarios.ChildResult result = BattleScenarios.launchChild("ffm", "freespace-extend", 90);
        System.out.println(result.output);
        Assertions.assertNotEquals(4, result.code, "concurrent insert hung\n" + result.output);
        Assertions.assertEquals(0, result.code, result.output);
        Assertions.assertTrue(result.output.contains("freespace-extend ok"), result.output);
        Assertions.assertTrue(result.output.contains("freespace-extend rows=240"), result.output);
    }

    @Test
    public void concurrentAutocommitDoesNotCrashInTimezone() throws Exception {
        BattleScenarios.ChildResult result = BattleScenarios.launchChild("ffm", "concurrent-autocommit", 90);
        System.out.println(result.output);
        Assertions.assertNotEquals(4, result.code, "concurrent autocommit hung\n" + result.output);
        Assertions.assertEquals(0, result.code, result.output);
        Assertions.assertTrue(result.output.contains("concurrent-autocommit ok"), result.output);
        Assertions.assertTrue(result.output.contains("concurrent-autocommit rows=160"), result.output);
    }

    @Test
    public void abortWithoutBeginDoesNotCrash() throws Exception {
        BattleScenarios.ChildResult result = BattleScenarios.launchChild("ffm", "abort-idle", 60);
        System.out.println(result.output);
        Assertions.assertTrue(result.code == 0 || result.code == 2,
                "abort() with no begin() crashed the process\n" + result.output);
    }
}
