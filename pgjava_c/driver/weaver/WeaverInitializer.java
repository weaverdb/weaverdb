package driver.weaver;

import java.util.Properties;

public class WeaverInitializer {
    
    boolean loaded = false;
    
    public WeaverInitializer() {
        
    }
    
    public synchronized native void init(String database);
    public synchronized native void close();
    
    public void initialize(Properties props) throws java.lang.UnsatisfiedLinkError  {
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
}
