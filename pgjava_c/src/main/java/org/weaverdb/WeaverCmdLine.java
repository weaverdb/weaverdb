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

public class WeaverCmdLine {
    public static native int cmd(String args[]);
    
    public static void main(String args[]) {
        cmd(args);
    }
}
