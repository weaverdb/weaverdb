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
 * Any exception thrown by native functions will result in an ExecutionException.
 * 
 * @author myronscott
 */

public class ExecutionException extends Exception {

    /**
     * Creates a new instance of <code>WeaverException</code> without detail
     * message.
     */
    public ExecutionException() {
    }

    /**
     * Constructs an instance of <code>WeaverException</code> with the
     * specified detail message.
     *
     * @param msg the detail message.
     */
    public ExecutionException(String msg) {
        super(msg);
    }

    public ExecutionException(String message, Throwable cause) {
        super(message, cause);
    }

    public ExecutionException(Throwable cause) {
        super(cause);
    }
}
