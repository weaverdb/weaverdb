/*
 * Driver.java
 *
 * Created on November 16, 2003, 11:39 AM
 */

package driver.weaver.jdbc;

import driver.weaver.*;
import java.sql.*;
import java.util.*;
/**
 *
 * @author  mscott
 */
public class WeaverDriver extends WeaverInitializer implements Driver {
    /** Creates a new instance of Driver */
    boolean initialized  = false;
    
    public WeaverDriver() {
    }
    
    public void initialize(Properties props) throws java.lang.UnsatisfiedLinkError  {
        super.initialize(props);
        initialized = true;
    }
    
    public boolean acceptsURL(String url) {
        if ( url.startsWith("jdbc:weaver") ) return true;
        else return false;
    }
    
    public Connection connect(String url, Properties info) throws SQLException {
        if ( url.startsWith("jdbc:weaver") ) {
            boolean normalized = false;
            int start = 19;
            int stop = url.indexOf(':',start);
            String host = url.substring(start,stop);
            start = stop + 1;
            stop = url.indexOf(':',start);
            String port = (stop > 0 ) ? url.substring(start,stop) : url.substring(start);
            
            info.setProperty("host", host);
            info.setProperty("port", port);
            Weaver conn = new Weaver(info);
            return conn;
        }
        return null;
    }
    
    public int getMajorVersion() {
        return 2;
    }
    
    public int getMinorVersion() {
        return 1;
    }
    
    public DriverPropertyInfo[] getPropertyInfo(String url, Properties info) {
        return new DriverPropertyInfo[0];
    }
    
    public boolean jdbcCompliant() {
        return false;
    }
    
    public static void main(String[] args) {
        try {
            DriverManager.registerDriver(new WeaverDriver());
            Connection conn = DriverManager.getConnection("jdbc:weaver:localhost:4444","mscott", "axonenv");
            Statement stmt = conn.createStatement();
            stmt.execute("select name from profiles");
            while ( stmt.getMoreResults() ) {
                ResultSet set = stmt.getResultSet();
                ResultSetMetaData meta = set.getMetaData();
                while ( set.next() ) {
                    for ( int x=0;x<meta.getColumnCount();x++) {
                        Object val =set.getObject(x+1);
                    }
                }
            }
        } catch ( SQLException sql ) {
            sql.printStackTrace();
            
        }
        
    }
}
