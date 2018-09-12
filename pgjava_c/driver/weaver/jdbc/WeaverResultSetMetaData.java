/*
 * WeaverResultSetMetaData.java
 *
 * Created on November 16, 2003, 1:30 PM
 */

package driver.weaver.jdbc;

import java.sql.*;
import java.util.*;
/**
 *
 * @author  mscott
 */
public class WeaverResultSetMetaData implements ResultSetMetaData {
    ArrayList header;
    /**
     * Creates a new instance of WeaverResultSetMetaData
     */
    public WeaverResultSetMetaData(Weaver conn) throws SQLException {
        header = new ArrayList(conn.getResultsHeader());
    }
    
    public WeaverResultSetMetaData(Collection headers,String table) throws SQLException {
        header = new ArrayList(headers);
    }

    public WeaverHeader getHeader(int index) {
        return (WeaverHeader)header.get(index-1);
    }    
    
    public String getCatalogName(int column) throws SQLException {
        return "";
    }
    
    public String getColumnClassName(int column) throws SQLException {
        return ((WeaverHeader)header.get(column-1)).getType().getClass().getName();
    }
    
    public int getColumnCount() throws SQLException {
        return header.size();
    }
    
    public int getColumnDisplaySize(int column) throws SQLException {
        return 10;
    }
    
    public String getColumnLabel(int column) throws SQLException {
        try {
            return ((WeaverHeader)header.get(column-1)).getLabel();
        } catch ( java.lang.IndexOutOfBoundsException out ) {
            throw new SQLException(column + " out of bounds");
        }
    }
    
    public String getColumnName(int column) throws SQLException {
        try {
            return ((WeaverHeader)header.get(column-1)).getName();
        } catch ( java.lang.IndexOutOfBoundsException out ) {
            throw new SQLException(column + " out of bounds");
        }
    }
    
    public int getColumnType(int column) throws SQLException {
        WeaverHeader head = (WeaverHeader)header.get(column-1);
        return head.getSQLType();
    }
    
    public String getColumnTypeName(int column) throws SQLException {
        WeaverHeader head = (WeaverHeader)header.get(column-1);
        return head.getSQLName();
    }
    
    public int getPrecision(int column) throws SQLException {
        WeaverHeader head = (WeaverHeader)header.get(column-1);
        return head.getPrecision();
    }
    
    public int getScale(int column) throws SQLException {
        WeaverHeader head = (WeaverHeader)header.get(column-1);
        return head.getScale();
    }
    
    public String getSchemaName(int column) throws SQLException {
        return "";
    }
    
    public String getTableName(int column) throws SQLException {
        return null;
    }
    
    public boolean isAutoIncrement(int column) throws SQLException {
        return false;
    }
    
    public boolean isCaseSensitive(int column) throws SQLException {
        return true;
    }
    
    public boolean isCurrency(int column) throws SQLException {
        return false;
    }
    
    public boolean isDefinitelyWritable(int column) throws SQLException {
        return false;
    }
    
    public int isNullable(int column) throws SQLException {
        return columnNoNulls;
    }
    
    public boolean isReadOnly(int column) throws SQLException {
        return true;
    }
    
    public boolean isSearchable(int column) throws SQLException {
        return true;
    }
    
    public boolean isSigned(int column) throws SQLException {
        return false;
    }
    
    public boolean isWritable(int column) throws SQLException {
        return false;
    }

    public <T> T unwrap(Class<T> iface) throws SQLException {
         throw new SQLException("not implemented");
    }

    public boolean isWrapperFor(Class<?> iface) throws SQLException {
         throw new SQLException("not implemented");
    }
    
}
