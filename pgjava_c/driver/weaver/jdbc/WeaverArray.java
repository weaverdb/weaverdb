/*
 * WeaverArray.java
 *
 * Created on November 16, 2003, 4:18 PM
 */

package driver.weaver.jdbc;

import java.sql.*;

/**
 *
 * @author  mscott
 */
public class WeaverArray implements java.sql.Array {
    Object[] base;
    WeaverHeader head;
    /**
     * Creates a new instance of WeaverArray
     */
    public WeaverArray(Object[] array, WeaverHeader header) {
        base = array;
        head = header;
    }
    
    public Object getArray() throws SQLException {
        return base;
    }
    
    public Object getArray(java.util.Map map) throws SQLException {
        return base;
    }
    
    public Object getArray(long index, int count) throws SQLException {
        Object[] items = new Object[count];
        System.arraycopy(base, (int)(index-1), items, 0, count);
        return items;
    }
    
    public Object getArray(long index, int count, java.util.Map map) throws SQLException {
        Object[] items = new Object[count];
        System.arraycopy(base, (int)(index-1), items, 0, count);
        return items;
    }
    
    public int getBaseType() throws SQLException {
        return head.getSQLType();
    }
    
    public String getBaseTypeName() throws SQLException {
        return head.getSQLName();
    }
    
    public ResultSet getResultSet() throws SQLException {
        return new WeaverResultSet(base,head);
    }
    
    public ResultSet getResultSet(java.util.Map map) throws SQLException {
        return new WeaverResultSet(base,head);
    }
    
    public ResultSet getResultSet(long index, int count) throws SQLException {
        Object[] items = new Object[count];
        System.arraycopy(base, (int)(index-1), items, 0, count);
        return new WeaverResultSet(items,head);
    }
    
    public ResultSet getResultSet(long index, int count, java.util.Map map) throws SQLException {
        Object[] items = new Object[count];
        System.arraycopy(base, (int)(index-1), items, 0, count);
        return new WeaverResultSet(items,head);
    }

    public void free() throws SQLException {
        base = null;
    }
    
}
