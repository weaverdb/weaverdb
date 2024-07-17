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

/*
 * ByteWellInputStream.java
 *
 * Created on September 14, 2004, 8:24 PM
 */

package org.weaverdb;

import java.io.*;
/**
 *
 * @author  mscott
 */
class ByteWellInputStream extends ByteArrayInputStream {
    
    /** Creates a new instance of ByteWellInputStream */
    ByteWellInputStream() {
        super(new byte[0]);
    }
    
    ByteWellInputStream(int size) {
        super(new byte[size]);
    }
    
    public void setByteWell(byte[] data) {
        buf = data;
        count = data.length;
        pos = 0; 
        mark = 0;
    }
}
