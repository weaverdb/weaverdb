package org.weaverdb.battle;

import org.junit.jupiter.api.Assertions;
import org.junit.jupiter.api.Test;

/**
 * Process-isolated JNI cases: engine restart, lock waits, and a second
 * thread that uses a connection it does not own. A crash here does not kill the in-process suite.
 */
public class JniBattleIsolatedTest {

    @Test
    public void restartSurvivesShutdown() throws Exception {
        BattleScenarios.ChildResult result = BattleScenarios.launchChild("jni", "restart", 90);
        Assertions.assertEquals(0, result.code, result.output);
    }

    @Test
    public void crossConnectionDeadlockDoesNotHang() throws Exception {
        BattleScenarios.ChildResult result = BattleScenarios.launchChild("jni", "deadlock", 40);
        Assertions.assertNotEquals(4, result.code, "deadlock hung the engine\n" + result.output);
        Assertions.assertTrue(result.code == 0 || result.code == 2, result.output);
    }

    @Test
    public void sharedConnectionDoesNotCrash() throws Exception {
        BattleScenarios.ChildResult result = BattleScenarios.launchChild("jni", "shared", 40);
        System.out.println(result.output);
        Assertions.assertEquals(0, result.code,
                "foreign thread did not get a clean rejection\n" + result.output);
        Assertions.assertTrue(result.output.contains("owned by a different thread"), result.output);
        Assertions.assertTrue(result.output.contains("shared rows=1"), result.output);
    }

    @Test
    public void cancelStopsRunningStatement() throws Exception {
        BattleScenarios.ChildResult result = BattleScenarios.launchChild("jni", "cancel", 30);
        System.out.println(result.output);
        Assertions.assertEquals(0, result.code, result.output);
        Assertions.assertTrue(result.output.contains("cancel sleep started"), result.output);
        Assertions.assertTrue(result.output.contains("cancel alive=false"), result.output);
        Assertions.assertTrue(result.output.toLowerCase().contains("cancel"), result.output);
        Assertions.assertTrue(result.output.contains("cancel followup=1"), result.output);
    }

    @Test
    public void multipageUpdateUnderLockStaysConsistent() throws Exception {
        BattleScenarios.ChildResult result = BattleScenarios.launchChild("jni", "multipage", 90);
        Assertions.assertEquals(0, result.code, result.output);
        Assertions.assertTrue(result.output.contains("multipage ok"), result.output);
        Assertions.assertTrue(result.output.contains("recovered=true"), result.output);
    }

    @Test
    public void vacuumKeepsHeapAndIndexInOneWriteGroup() throws Exception {
        BattleScenarios.ChildResult result = BattleScenarios.launchChild("jni", "vacuum-group", 90);
        Assertions.assertEquals(0, result.code, result.output);
        Assertions.assertTrue(result.output.contains("vacuum-group ok"), result.output);
        Assertions.assertTrue(result.output.contains("recovered=true"), result.output);
    }

    @Test
    public void hashIndexDoesNotSpin() throws Exception {
        BattleScenarios.ChildResult result = BattleScenarios.launchChild("jni", "hash-index", 20);
        System.out.println(result.output);
        Assertions.assertNotEquals(4, result.code, "hash index build did not return\n" + result.output);
        Assertions.assertEquals(0, result.code, result.output);
    }

    @Test
    public void helperDoesNotCrash() throws Exception {
        BattleScenarios.ChildResult result = BattleScenarios.launchChild("jni", "helper", 60);
        System.out.println(result.output);
        Assertions.assertEquals(0, result.code,
                "helper() crashed the process\n" + result.output);
        Assertions.assertTrue(result.output.contains("helper select=1"), result.output);
    }

    @Test
    public void constraintsDoNotCrash() throws Exception {
        BattleScenarios.ChildResult result = BattleScenarios.launchChild("jni", "constraints", 60);
        System.out.println(result.output);
        Assertions.assertEquals(0, result.code,
                "constraints crashed or failed\n" + result.output);
    }

    @Test
    public void autocommitErrorLeavesConnectionUsable() throws Exception {
        BattleScenarios.ChildResult result = BattleScenarios.launchChild("jni", "autocommit-error", 60);
        System.out.println(result.output);
        Assertions.assertEquals(0, result.code, result.output);
    }

    @Test
    public void characterRoundTrips() throws Exception {
        BattleScenarios.ChildResult result = BattleScenarios.launchChild("jni", "char-bind", 60);
        System.out.println(result.output);
        Assertions.assertEquals(0, result.code, result.output);
    }

    @Test
    public void nullIntegerRoundTrips() throws Exception {
        BattleScenarios.ChildResult result = BattleScenarios.launchChild("jni", "null-int", 60);
        System.out.println(result.output);
        Assertions.assertEquals(0, result.code, result.output);
    }

    @Test
    public void hashDropAfterReinitDoesNotReadPastLog() throws Exception {
        BattleScenarios.ChildResult result = BattleScenarios.launchChild("jni", "prefetch-drop", 90);
        System.out.println(result.output);
        Assertions.assertNotEquals(4, result.code, "shutdown hung after drop index\n" + result.output);
        Assertions.assertEquals(0, result.code, result.output);
        Assertions.assertTrue(result.output.contains("prefetch-drop ok"), result.output);
        Assertions.assertTrue(result.output.contains("prefetch-drop rows=3"), result.output);
    }

    @Test
    public void concurrentHeapInsertDoesNotTripFreespaceAssert() throws Exception {
        BattleScenarios.ChildResult result = BattleScenarios.launchChild("jni", "freespace-extend", 90);
        System.out.println(result.output);
        Assertions.assertNotEquals(4, result.code, "concurrent insert hung\n" + result.output);
        Assertions.assertEquals(0, result.code, result.output);
        Assertions.assertTrue(result.output.contains("freespace-extend ok"), result.output);
        Assertions.assertTrue(result.output.contains("freespace-extend rows=240"), result.output);
    }

    @Test
    public void concurrentAutocommitDoesNotCrashInTimezone() throws Exception {
        BattleScenarios.ChildResult result = BattleScenarios.launchChild("jni", "concurrent-autocommit", 90);
        System.out.println(result.output);
        Assertions.assertNotEquals(4, result.code, "concurrent autocommit hung\n" + result.output);
        Assertions.assertEquals(0, result.code, result.output);
        Assertions.assertTrue(result.output.contains("concurrent-autocommit ok"), result.output);
        Assertions.assertTrue(result.output.contains("concurrent-autocommit rows=160"), result.output);
    }

    @Test
    public void beginReturnsTransactionId() throws Exception {
        BattleScenarios.ChildResult result = BattleScenarios.launchChild("jni", "begin-id", 60);
        System.out.println(result.output);
        Assertions.assertEquals(0, result.code, result.output);
        Assertions.assertTrue(result.output.contains("begin rows=1"), result.output);
        Assertions.assertTrue(result.output.contains("begin ok"), result.output);
        Assertions.assertFalse(result.output.contains("code 0"), result.output);
    }

    @Test
    public void abortWithoutBeginDoesNotCrash() throws Exception {
        BattleScenarios.ChildResult result = BattleScenarios.launchChild("jni", "abort-idle", 60);
        System.out.println(result.output);
        Assertions.assertTrue(result.code == 0 || result.code == 2,
                "abort() with no begin() crashed the process\n" + result.output);
    }
}
