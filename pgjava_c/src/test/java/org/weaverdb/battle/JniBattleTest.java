package org.weaverdb.battle;

import java.util.List;
import java.util.stream.Stream;
import org.junit.jupiter.api.Assertions;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.DynamicTest;
import org.junit.jupiter.api.TestFactory;
import org.junit.jupiter.api.extension.ExtendWith;
import org.weaverdb.DBReference;
import org.weaverdb.InstallNative;

/**
 * Same scenarios as the FFM suite, through the JNI connection factory.
 */
@ExtendWith(InstallNative.class)
public class JniBattleTest {

    @BeforeAll
    public static void openDatabase() throws Exception {
        BattleScenarios.ensureDatabase();
        try (DBReference conn = BattleScenarios.open()) {
            Assertions.assertTrue(conn.getClass().getName().contains("BaseWeaverConnection"),
                    conn.getClass().getName());
        }
    }

    @TestFactory
    Stream<DynamicTest> core() {
        return tests(BattleScenarios.core());
    }

    @TestFactory
    Stream<DynamicTest> stress() {
        return tests(BattleScenarios.stress());
    }

    private static Stream<DynamicTest> tests(List<BattleScenarios.Scenario> scenarios) {
        return scenarios.stream().map(scenario -> DynamicTest.dynamicTest(scenario.name(), scenario::run));
    }
}
