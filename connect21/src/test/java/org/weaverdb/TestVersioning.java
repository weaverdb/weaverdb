
package org.weaverdb;

import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.AfterAll;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.BeforeAll;


public class TestVersioning {
    
    public TestVersioning() {
    }
    
    @BeforeAll
    public static void setUpClass() {
    }
    
    @AfterAll
    public static void tearDownClass() {
    }
    
    @BeforeEach
    public void setUp() {
    }
    
    @AfterEach
    public void tearDown() {
    }

    
    @org.junit.jupiter.api.Test
    public void testAutoMethod() throws Exception {
        ConnectionFactory factory = Connection.loader;
        assertNotNull(factory);
    }
}
