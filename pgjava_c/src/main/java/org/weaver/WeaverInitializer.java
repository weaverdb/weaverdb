package org.weaver;

import java.util.Properties;

public class WeaverInitializer {
    
    private static boolean loaded = false;
    
    public WeaverInitializer() {
        
    }
    
    private static synchronized native void init(String database);
    private static synchronized native void close();
    
    public static void initialize(Properties props) throws java.lang.UnsatisfiedLinkError  {
        StringBuilder vars = new StringBuilder();
        
        if ( loaded ) return;
        
        java.util.Enumeration it = props.keys();
        while ( it.hasMoreElements() ) {
            String key = it.nextElement().toString();
            vars.append(key + "=" + props.getProperty(key) + ";");
        }
        
        String library = props.getProperty("library");
        if ( library == null ) library = "weaver";
        try {
            init(vars.toString());
        } catch ( UnsatisfiedLinkError us ) {
            System.loadLibrary(library);
            
            init(vars.toString());
        }
        
        loaded = true;
    }
    
    public static void close(boolean clean) {
        close();
    }
}
