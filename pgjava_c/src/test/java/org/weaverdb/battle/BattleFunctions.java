package org.weaverdb.battle;

/**
 * Methods installed as LANGUAGE 'java' functions by the battle suite.
 */
public final class BattleFunctions {

    private BattleFunctions() {
    }

    public static int add(int a, int b) {
        return a + b;
    }

    public static String ident(String value) {
        return value;
    }

    public static int sleepMs(int millis) throws InterruptedException {
        Thread.sleep(Math.max(0, millis));
        return millis;
    }
}
