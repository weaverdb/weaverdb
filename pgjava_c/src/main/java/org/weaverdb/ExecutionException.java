
package org.weaverdb;


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
