/*
 * WeaverConverter.java
 *
 * Created on August 23, 2004, 3:20 PM
 */

package org.weaverdb;

import java.io.*;
/**
 *
 * @author  mscott
 */
public class WeaverConverter {
    
    /**
     * Creates a new instance of WeaverConverter
     */
    public WeaverConverter() {
    }
    
    public static Object java_out(byte[] input) {
        try {
            ByteArrayInputStream rawout = new ByteArrayInputStream(input);
            ObjectInputStream objout = new ObjectInputStream(rawout);
            return objout.readObject();
        } catch ( IOException ioe ) {
            return null;
        } catch ( ClassNotFoundException cla ) {
            return null;
        } catch (Throwable t) {
            t.printStackTrace();
            return null;
        }
    }   
    
    public static byte[] java_in(Object input) {
        try     {
            ByteArrayOutputStream rawout = new ByteArrayOutputStream();
            ObjectOutputStream objout = new ObjectOutputStream(rawout);
            objout.writeObject(input);
            objout.flush();
            return rawout.toByteArray();
        } catch ( IOException ioe ) {
            return null;
        }  catch (Throwable t) {
            t.printStackTrace();
            return null;
        }
    }
}
