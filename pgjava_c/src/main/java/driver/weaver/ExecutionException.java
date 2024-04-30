/*
 * Click nbfs://nbhost/SystemFileSystem/Templates/Licenses/license-default.txt to change this license
 * Click nbfs://nbhost/SystemFileSystem/Templates/Classes/Exception.java to edit this template
 */
package driver.weaver;

/**
 *
 * @author myronscott
 */
public class ExecutionException extends Exception {

    /**
     * Creates a new instance of <code>ExecutionException</code> without detail
     * message.
     */
    public ExecutionException() {
    }

    /**
     * Constructs an instance of <code>ExecutionException</code> with the
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
