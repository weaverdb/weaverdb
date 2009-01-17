/*
 * ByteWellInputStream.java
 *
 * Created on September 14, 2004, 8:24 PM
 */

package driver.weaver;

import java.io.*;
/**
 *
 * @author  mscott
 */
public class ByteWellOutputStream extends ByteArrayOutputStream {
    
    /** Creates a new instance of ByteWellInputStream */
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
