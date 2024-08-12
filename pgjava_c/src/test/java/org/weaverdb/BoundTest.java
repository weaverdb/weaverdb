/*-------------------------------------------------------------------------
 *
 *
 * Copyright (c) 2024, Myron Scott  <myron@weaverdb.org>
 *
 * All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 *
 *-------------------------------------------------------------------------
 */


package org.weaverdb;

import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.AfterAll;
import org.junit.jupiter.api.Assertions;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.Test;

/**
 *
 * @author myronscott
 */
public class BoundTest {
    
    public BoundTest() {
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

    @Test
    public void testPrimativeCast() throws Exception {
        BoundInput<Integer> bi = new BoundInput<>(null, "test", int.class);
        bi.set(1);
        BoundOutput<Integer> bo = new BoundOutput(null, 1, int.class);
        java.lang.reflect.Field f = BoundOutput.class.getDeclaredField("value");
        f.setAccessible(true);
        f.set(bo, Integer.valueOf(1));
        Integer v = bo.get();
        Assertions.assertEquals(v, Integer.valueOf(1));
    }
}
