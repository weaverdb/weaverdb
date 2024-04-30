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
public class ByteWellInputStream extends ByteArrayInputStream {
    
    /** Creates a new instance of ByteWellInputStream */
    public ByteWellInputStream() {
        super(new byte[0]);
    }
    
    public ByteWellInputStream(int size) {
        super(new byte[size]);
    }
    
    public void setByteWell(byte[] data) {
        buf = data;
        count = data.length;
        pos = 0; 
        mark = 0;
    }
}
