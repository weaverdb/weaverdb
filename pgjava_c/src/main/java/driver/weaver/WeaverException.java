/*
 * Click nbfs://nbhost/SystemFileSystem/Templates/Licenses/license-default.txt to change this license
 * Click nbfs://nbhost/SystemFileSystem/Templates/Classes/Exception.java to edit this template
 */
package driver.weaver;

/**
 *
 * @author myronscott
 */
public class WeaverException extends Exception {

    /**
     * Creates a new instance of <code>WeaverException</code> without detail
     * message.
     */
    public WeaverException() {
    }

    /**
     * Constructs an instance of <code>WeaverException</code> with the
     * specified detail message.
     *
     * @param msg the detail message.
     */
    public WeaverException(String msg) {
        super(msg);
    }

    public WeaverException(String message, Throwable cause) {
        super(message, cause);
    }

    public WeaverException(Throwable cause) {
        super(cause);
    }
}
