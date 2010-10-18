/*
 * WeaverParameterMetaData.java
 *
 * Created on November 16, 2003, 3:24 PM
 */

package driver.weaver.jdbc;

import java.sql.*;
import java.util.*;
/**
 *
 * @author  mscott
 */
public class WeaverParameterMetaData implements ParameterMetaData {
    Weaver connection;
    ArrayList list;
    /**
     * Creates a new instance of WeaverParameterMetaData
     */
    public WeaverParameterMetaData(Weaver conn) throws SQLException {
        connection = conn;
        list = new ArrayList(connection.getParameters());
    }
    
    public String getParameterClassName(int param) throws SQLException {
        return "java.lang.Object";
    }
    
    public int getParameterCount() throws SQLException {
        return list.size();
    }
    
    public int getParameterMode(int param) throws SQLException {
        return this.parameterModeOut;
    }
    
    public int getParameterType(int param) throws SQLException {
        return java.sql.Types.JAVA_OBJECT;
    }
    
    public String getParameterTypeName(int param) throws SQLException {
        return "java.lang.Object";
    }
    
    public int getPrecision(int param) throws SQLException {
        return 0;
    }
    
    public int getScale(int param) throws SQLException {
        return 0;
    }
    
    public int isNullable(int param) throws SQLException {
        return this.parameterNoNulls;
    }
    
    public boolean isSigned(int param) throws SQLException {
        return false;
    }

    public <T> T unwrap(Class<T> iface) throws SQLException {
             throw new SQLException("not implemented");
    }

    public boolean isWrapperFor(Class<?> iface) throws SQLException {
             throw new SQLException("not implemented");
    }
    
}
