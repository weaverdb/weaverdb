package org.weaverdb.direct;

import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

/**
 * Minimal test for the FFM Java function invoker (LANGUAGE 'java' via upcalls).
 *
 * This exercises the upcall stub creation and basic registration path on the
 * Java side. Full end-to-end testing (calling from native C code) requires
 * the native library to be rebuilt with current sources.
 */
public class JavaFunctionInvokerTest {

    @Test
    void canCreateUpcallStub() {
        JavaFunctionInvoker invoker = new JavaFunctionInvoker();
        var stub = invoker.createUpcallStub();

        assertNotNull(stub, "Upcall stub must not be null");
        assertTrue(stub.address() != 0, "Upcall stub must have a valid native address");
    }

    @Test
    void defaultInstanceWorks() {
        JavaFunctionInvoker invoker = JavaFunctionInvoker.getDefault();
        assertNotNull(invoker);

        var stub = invoker.createUpcallStub();
        assertNotNull(stub);
    }
}
