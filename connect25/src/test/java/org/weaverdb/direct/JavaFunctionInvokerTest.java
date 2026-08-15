package org.weaverdb.direct;

import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

/**
 * Stub-level checks for the FFM Java function invoker.
 * SQL end-to-end coverage lives in {@link JavaStoredProcedureTest}.
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
