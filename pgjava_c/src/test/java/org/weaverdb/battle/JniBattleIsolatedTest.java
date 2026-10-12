package org.weaverdb.battle;

import org.junit.jupiter.api.Assertions;
import org.junit.jupiter.api.Test;

/**
 * Process-isolated JNI cases: engine restart, lock waits, and sharing one
 * connection across threads. A crash here does not kill the in-process suite.
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
        Assertions.assertTrue(result.code == 0 || result.code == 2,
                "shared connection crashed or hung\n" + result.output);
    }

    @Test
    public void cancelStopsRunningStatement() throws Exception {
        BattleScenarios.ChildResult result = BattleScenarios.launchChild("jni", "cancel", 30);
        Assertions.assertEquals(0, result.code, result.output);
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
        Assertions.assertTrue(result.code == 0 || result.code == 2,
                "helper() crashed the process\n" + result.output);
    }

    @Test
    public void constraintsDoNotCrash() throws Exception {
        BattleScenarios.ChildResult result = BattleScenarios.launchChild("jni", "constraints", 60);
        System.out.println(result.output);
        Assertions.assertTrue(result.code == 0 || result.code == 3,
                "constraints crashed the process\n" + result.output);
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
    public void abortWithoutBeginDoesNotCrash() throws Exception {
        BattleScenarios.ChildResult result = BattleScenarios.launchChild("jni", "abort-idle", 60);
        System.out.println(result.output);
        Assertions.assertTrue(result.code == 0 || result.code == 2,
                "abort() with no begin() crashed the process\n" + result.output);
    }
}
