/*
 * Copyright (c) 2024 Myron Scott <myron@weaverdb.org> All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 */




package org.weaverdb;


public class BinaryTruncation extends Exception {

    public int maxcount = -1;
    
    public BinaryTruncation(String s) {
        super(s);
        maxcount = Integer.parseInt(s);
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