/*
 * Copyright (c) 2024 Myron Scott <myron@weaverdb.org> All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
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
public class ByteWellOutputStream extends ByteArrayOutputStream {
    
    public ByteWellOutputStream() {
        super(32568);
    }

    public byte[] getByteWell() {
        byte[] pass = new byte[count];
        System.arraycopy(buf,0,pass,0,count);
        count = 0;
        return pass;
    }
}
