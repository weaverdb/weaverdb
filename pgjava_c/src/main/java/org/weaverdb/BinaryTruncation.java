/*-------------------------------------------------------------------------
 *
 *
 * Copyright (c) 2000-2024, Myron Scott  <myron@weaverdb.org>
 *
 * All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 *
 *-------------------------------------------------------------------------
 */



package org.weaverdb;
/**
 * Thrown when not enough space has been allocated to collect a binary value.
 * @author myronscott
 */

public class BinaryTruncation extends Exception {

    public int maxcount = -1;
    
    public BinaryTruncation(String s) {
        super(s);
    }
    
    public BinaryTruncation(int max) {
        super(Integer.toString(max));
        maxcount = max;
    }
    
    public BinaryTruncation(Throwable  s) {
        super(s.getMessage());
        this.initCause(s);
    }
    
    public int getMaxFieldLength() {
        return maxcount;
    }
}