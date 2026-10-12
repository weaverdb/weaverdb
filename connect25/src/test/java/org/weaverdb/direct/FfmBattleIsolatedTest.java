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
        Assertions.assertTrue(result.code == 0 || result.code == 2,
                "shared connection crashed or hung\n" + result.output);
    }

    @Test
    public void cancelStopsRunningStatement() throws Exception {
        BattleScenarios.ChildResult result = BattleScenarios.launchChild("ffm", "cancel", 30);
        Assertions.assertEquals(0, result.code, result.output);
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
        Assertions.assertTrue(result.code == 0 || result.code == 2,
                "helper() crashed the process\n" + result.output);
    }

    @Test
    public void constraintsDoNotCrash() throws Exception {
        BattleScenarios.ChildResult result = BattleScenarios.launchChild("ffm", "constraints", 60);
        System.out.println(result.output);
        Assertions.assertTrue(result.code == 0 || result.code == 3,
                "constraints crashed the process\n" + result.output);
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
    public void abortWithoutBeginDoesNotCrash() throws Exception {
        BattleScenarios.ChildResult result = BattleScenarios.launchChild("ffm", "abort-idle", 60);
        System.out.println(result.output);
        Assertions.assertTrue(result.code == 0 || result.code == 2,
                "abort() with no begin() crashed the process\n" + result.output);
    }
}
