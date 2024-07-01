
package org.weaverdb;

public class WeaverCmdLine {
    public static native int cmd(String args[]);
    
    public static void main(String args[]) {
        cmd(args);
    }
}
