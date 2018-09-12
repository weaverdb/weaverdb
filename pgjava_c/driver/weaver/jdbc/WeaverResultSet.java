/*
 * WeaverResultSet.java
 *
 * Created on November 16, 2003, 1:23 PM
 */

package driver.weaver.jdbc;

import java.io.InputStream;
import java.io.Reader;
import java.sql.*;
import java.util.*;
/**
 *
 * @author  mscott
 */
public class WeaverResultSet implements ResultSet {
    HashMap map;
    List rows;
    int position;
    Weaver connection;
    WeaverResultSetMetaData meta_data;
    WeaverStatement statement;
    boolean was_null;
    /**
     * Creates a new instance of WeaverResultSet
     */
    public WeaverResultSet(List items,WeaverStatement st,WeaverResultSetMetaData md) throws SQLException {
        meta_data = md;
        statement = st;
        map = new HashMap();
        int count = md.getColumnCount();
        for(int x=1;x<=count;x++) {
            map.put(md.getColumnName(x), new Integer(x));
        }
        if ( st != null ) connection = (Weaver)st.getConnection();
         
        rows = items;
        position = -1;
    }
    
  /*  this flattens results into a 2d array  */  
    private List normalizeResults(List results) {
        ArrayList newlist = new ArrayList();
        Iterator it = results.iterator();
        while ( it.hasNext() ) {
            normalizeRow((Object[])it.next(),newlist);
        }
        return newlist;
    }
    
    private void normalizeRow(Object[] row,List pool) {
        int amax = 0;
        for ( int y=0;y<row.length;y++ ) {
            if ( row[y] != null && row[y].getClass().isArray() ) {
                int item = java.lang.reflect.Array.getLength(row[y]);
                if ( item > amax ) amax = item;
            }
        }
        if ( amax > 0 ) {
            for (int x=0;x<amax;x++) {
                Object[] send = new Object[row.length];
                for ( int y=0;y<row.length;y++ ) {
                    if ( row[y] != null && row[y].getClass().isArray() ) {
                        try {
                            send[y] = java.lang.reflect.Array.get(row[y], x);
                        } catch ( ArrayIndexOutOfBoundsException exp ) {
                            send[y] = null;
                        }
                    } else {
                        if ( x == 0 ) send[y] = row[y];
                        else send[y] = null;
                    }
                }
                normalizeRow(send, pool);
            }
        } else {
            pool.add(row);
        }
    }
    
    public int size() {
        return rows.size();
    }
    
    public WeaverResultSet(Object[] base,WeaverHeader header) throws SQLException {
        rows = new ArrayList(base.length);
        map = new HashMap();
        for(int x=1;x<=base.length;x++) {
            rows.add(base[x]);
            map.put(Integer.toString(x), new Integer(x));
        }
        ArrayList headers = new ArrayList();
        headers.add(header);
        meta_data = new WeaverResultSetMetaData(headers, null);
    }
    
    public boolean absolute(int row) throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public void afterLast() throws SQLException {
        position = rows.size();
    }
    
    public void beforeFirst() throws SQLException {
        position = -1;
    }
    
    public void cancelRowUpdates() throws SQLException {
    }
    
    public void clearWarnings() throws SQLException {
    }
    
    public void close() throws SQLException {
    }
    
    public void deleteRow() throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public int findColumn(String columnName) throws SQLException {
        Integer i = (Integer)map.get(columnName);
        return i.intValue();
    }
    
    public boolean first() throws SQLException {
        return false;
    }
    
    public Array getArray(int i) throws SQLException {
        Object col = this.getObject(i);
        if ( col.getClass().isArray() ) {
            WeaverHeader header = meta_data.getHeader(i);
            header = new WeaverHeader(header.getType().getComponentType(), header.getConverter(),header.getName());
            return new WeaverArray((Object[])col,header);
        } else if ( col == null ) {
            WeaverHeader header = meta_data.getHeader(i);
            header = new WeaverHeader(header.getType().getComponentType(), header.getConverter(),header.getName());
            return new WeaverArray(new Object[0],header);
        }
        throw new SQLException("object not array");
    }
    
    public Array getArray(String colName) throws SQLException {
        return this.getArray(((Integer)map.get(colName)).intValue());
    }
    
    public java.io.InputStream getAsciiStream(String columnName) throws SQLException {
        return this.getAsciiStream(((Integer)map.get(columnName)).intValue());
    }
    
    public java.io.InputStream getAsciiStream(int columnIndex) throws SQLException {
        Object target = this.getObject(columnIndex);
        try {
            return new java.io.ByteArrayInputStream(target.toString().getBytes("ASCII"));
        } catch ( java.io.UnsupportedEncodingException us ) {
            throw new SQLException(us.getMessage());
        }
    }
    
    public java.math.BigDecimal getBigDecimal(String columnName) throws SQLException {
        return this.getBigDecimal(((Integer)map.get(columnName)).intValue());
    }
    
    public java.math.BigDecimal getBigDecimal(int columnIndex) throws SQLException {
        Object target = this.getObject(columnIndex);
        if ( target instanceof Double ) {
            return new java.math.BigDecimal(((Double)target).doubleValue());
        } else if ( target == null ) {
            return null;
        }
        String classname = null;
        if ( target != null ) {
            classname = target.getClass().getName();
        }
        throw new SQLException("wrong type expected big decimal got " + classname + "column:" + columnIndex);
    }
     
    public java.math.BigDecimal getBigDecimal(int columnIndex, int scale) throws SQLException {
        java.math.BigDecimal bd = this.getBigDecimal(columnIndex);
        bd.setScale(scale);
        return bd;
    }
   
    public java.math.BigDecimal getBigDecimal(String columnName, int scale) throws SQLException {
        java.math.BigDecimal bd = this.getBigDecimal(columnName);
        bd.setScale(scale);
        return bd;
    }
    
    public java.io.InputStream getBinaryStream(int columnIndex) throws SQLException {
        Object target = this.getObject(columnIndex);
        if ( target instanceof byte[] ) {
            return new java.io.ByteArrayInputStream((byte[])target);
        }
        String classname = null;
        if ( target != null ) {
            classname = target.getClass().getName();
        }
        throw new SQLException("wrong type expected binary stream got " + classname + "column:" + columnIndex);

    }
    
    public java.io.InputStream getBinaryStream(String columnName) throws SQLException {
        return this.getBinaryStream(((Integer)map.get(columnName)).intValue());
    }
    
    public Blob getBlob(int columnIndex) throws SQLException {
        Object target = this.getObject(columnIndex);
        if ( target instanceof Blob ) {
            return (Blob)target;
        }
        String classname = null;
        if ( target != null ) {
            classname = target.getClass().getName();
        }
        throw new SQLException("wrong type expected blob got " + classname + "column:" + columnIndex);

    }
    
    public Blob getBlob(String colName) throws SQLException {
        return this.getBlob(((Integer)map.get(colName)).intValue());
    }
    
    public boolean getBoolean(int columnIndex) throws SQLException {
        Object target = this.getObject(columnIndex);
        if ( target instanceof Boolean ) {
            return ((Boolean)target).booleanValue();
        } else if ( target instanceof Byte ) {
            return ( ((Byte)target).byteValue() > 0 ) ? true : false;
        } else if ( target == null ) {
            return false;
        }
        String classname = null;
        if ( target != null ) {
            classname = target.getClass().getName();
        }
        throw new SQLException("wrong type expected boolean got " + classname + "column:" + columnIndex);

    }
    
    public boolean getBoolean(String columnName) throws SQLException {
        return this.getBoolean(((Integer)map.get(columnName)).intValue());
    }
    
    public byte getByte(int columnIndex) throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public byte getByte(String columnName) throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public byte[] getBytes(int columnIndex) throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public byte[] getBytes(String columnName) throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public java.io.Reader getCharacterStream(int columnIndex) throws SQLException {
        Object target = this.getObject(columnIndex);
        if ( target instanceof String ) {
            return new java.io.StringReader(target.toString());
        }
        String classname = null;
        if ( target != null ) {
            classname = target.getClass().getName();
        }
        throw new SQLException("wrong type expected char stream got " + classname + "column:" + columnIndex);

    }
    
    public java.io.Reader getCharacterStream(String columnName) throws SQLException {
        return this.getCharacterStream(this.findColumn(columnName));
    }
    
    public Clob getClob(int i) throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public Clob getClob(String colName) throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public int getConcurrency() throws SQLException {
        return this.CONCUR_READ_ONLY;
    }
    
    public String getCursorName() throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public java.sql.Date getDate(int columnIndex) throws SQLException {
        Object target = this.getObject(columnIndex);
        if ( target instanceof java.util.Date ) {
            return new java.sql.Date(((java.util.Date)target).getTime());
        }
        return null;
    }
    
    public java.sql.Date getDate(String columnName) throws SQLException {
        return this.getDate(findColumn(columnName));
    }
    
    public java.sql.Date getDate(int columnIndex, java.util.Calendar cal) throws SQLException {
        Object target = this.getObject(columnIndex);
        if ( target instanceof java.util.Date ) {
            cal.setTime(((java.util.Date)target));
            return new java.sql.Date(cal.getTime().getTime());
        }
        return null;
    }
    
    public java.sql.Date getDate(String columnName, java.util.Calendar cal) throws SQLException {
        return this.getDate(findColumn(columnName),cal);
        
    }
    
    public double getDouble(int columnIndex) throws SQLException {
        Object target = this.getObject(columnIndex);
        if ( target instanceof Double ) {
            return ((Double)target).doubleValue();
        } else if ( target == null ) {
            return 0.0;
        }
        throw new SQLException("type not double");
    }
    
    public double getDouble(String columnName) throws SQLException {
        return this.getDouble(findColumn(columnName));
    }
    
    public int getFetchDirection() throws SQLException {
        return this.FETCH_FORWARD;
    }
    
    public int getFetchSize() throws SQLException {
        return statement.getFetchSize();
    }
    
    public float getFloat(int columnIndex) throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public float getFloat(String columnName) throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public int getInt(String columnName) throws SQLException {
        return this.getInt(findColumn(columnName));
    }
    
    public int getInt(int columnIndex) throws SQLException {
        Object target = this.getObject(columnIndex);
        if ( target instanceof Integer ) {
            return ((Integer)target).intValue();
        } else if ( target instanceof Boolean ) {
            return (((Boolean)target).booleanValue()) ? 1 : 0;
        } else if ( target == null ) {
            return 0;
        }
        String classname = null;
        if ( target != null ) {
            classname = target.getClass().getName();
        }
        throw new SQLException("wrong type expected int got " + classname + "column:" + columnIndex);

    }
    
    public long getLong(int columnIndex) throws SQLException {
        Object target = this.getObject(columnIndex);
        if ( target instanceof Long ) {
            return ((Long)target).longValue();
        }
        String classname = null;
        if ( target != null ) {
            classname = target.getClass().getName();
        }
        throw new SQLException("wrong type expected long got " + classname + "column:" + columnIndex);

    }
    
    public long getLong(String columnName) throws SQLException {
        return this.getLong(findColumn(columnName));
    }
    
    public ResultSetMetaData getMetaData() throws SQLException {
        return meta_data;
    }
    
    public Object getObject(int columnIndex) throws SQLException {
        Object[] target = (Object[])rows.get(position);
        was_null = (target[columnIndex-1] == null);
        return target[columnIndex-1];
    }
    
    public Object getObject(String columnName) throws SQLException {
        return this.getObject(((Integer)map.get(columnName)).intValue());
    }
    
    public Object getObject(int i, java.util.Map map) throws SQLException {
        return this.getObject(i);
    }
    
    public Object getObject(String colName, java.util.Map map) throws SQLException {
        return this.getObject(colName);
    }
    
    public Ref getRef(int i) throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public Ref getRef(String colName) throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public int getRow() throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public short getShort(String columnName) throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public short getShort(int columnIndex) throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public Statement getStatement() throws SQLException {
        return statement;
    }
    
    public String getString(String columnName) throws SQLException {
        return this.getString(findColumn(columnName));
    }
    
    public String getString(int columnIndex) throws SQLException {
        Object target = this.getObject(columnIndex);
        if ( target != null ) return target.toString();
        return null;
    }
    
    public java.sql.Time getTime(String columnName) throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public java.sql.Time getTime(int columnIndex) throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public java.sql.Time getTime(String columnName, java.util.Calendar cal) throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public java.sql.Time getTime(int columnIndex, java.util.Calendar cal) throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public java.sql.Timestamp getTimestamp(String columnName) throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public java.sql.Timestamp getTimestamp(int columnIndex) throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public java.sql.Timestamp getTimestamp(String columnName, java.util.Calendar cal) throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public java.sql.Timestamp getTimestamp(int columnIndex, java.util.Calendar cal) throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public int getType() throws SQLException {
        return ResultSet.TYPE_FORWARD_ONLY;
    }
    
    public java.net.URL getURL(int columnIndex) throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public java.net.URL getURL(String columnName) throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public java.io.InputStream getUnicodeStream(String columnName) throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public java.io.InputStream getUnicodeStream(int columnIndex) throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public SQLWarning getWarnings() throws SQLException {
        return null;
    }
    
    public void insertRow() throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public boolean isAfterLast() throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public boolean isBeforeFirst() throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public boolean isFirst() throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public boolean isLast() throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public boolean last() throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public void moveToCurrentRow() throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public void moveToInsertRow() throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public boolean next() throws SQLException {
        position++;
        if ( position >= rows.size() ) {
            position = rows.size();
            return false;
        }
        return true;
    }
    
    public boolean previous() throws SQLException {
        position--;
        if ( position < 0 ) {
            position = -1;
            return false;
        }
        return true;
    }
    
    public void refreshRow() throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public boolean relative(int rows) throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public boolean rowDeleted() throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public boolean rowInserted() throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public boolean rowUpdated() throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public void setFetchDirection(int direction) throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public void setFetchSize(int rows) throws SQLException {
        statement.setFetchSize(rows);
    }
    
    public void updateArray(String columnName, java.sql.Array x) throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public void updateArray(int columnIndex, java.sql.Array x) throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public void updateAsciiStream(String columnName, java.io.InputStream x, int length) throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public void updateAsciiStream(int columnIndex, java.io.InputStream x, int length) throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public void updateBigDecimal(String columnName, java.math.BigDecimal x) throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public void updateBigDecimal(int columnIndex, java.math.BigDecimal x) throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public void updateBinaryStream(int columnIndex, java.io.InputStream x, int length) throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public void updateBinaryStream(String columnName, java.io.InputStream x, int length) throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public void updateBlob(int columnIndex, java.sql.Blob x) throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public void updateBlob(String columnName, java.sql.Blob x) throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public void updateBoolean(int columnIndex, boolean x) throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public void updateBoolean(String columnName, boolean x) throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public void updateByte(int columnIndex, byte x) throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public void updateByte(String columnName, byte x) throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public void updateBytes(int columnIndex, byte[] x) throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public void updateBytes(String columnName, byte[] x) throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public void updateCharacterStream(int columnIndex, java.io.Reader x, int length) throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public void updateCharacterStream(String columnName, java.io.Reader reader, int length) throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public void updateClob(String columnName, java.sql.Clob x) throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public void updateClob(int columnIndex, java.sql.Clob x) throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public void updateDate(int columnIndex, java.sql.Date x) throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public void updateDate(String columnName, java.sql.Date x) throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public void updateDouble(int columnIndex, double x) throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public void updateDouble(String columnName, double x) throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public void updateFloat(String columnName, float x) throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public void updateFloat(int columnIndex, float x) throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public void updateInt(String columnName, int x) throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public void updateInt(int columnIndex, int x) throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public void updateLong(int columnIndex, long x) throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public void updateLong(String columnName, long x) throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public void updateNull(String columnName) throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public void updateNull(int columnIndex) throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public void updateObject(String columnName, Object x) throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public void updateObject(int columnIndex, Object x) throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public void updateObject(int columnIndex, Object x, int scale) throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public void updateObject(String columnName, Object x, int scale) throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public void updateRef(int columnIndex, java.sql.Ref x) throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public void updateRef(String columnName, java.sql.Ref x) throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public void updateRow() throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public void updateShort(int columnIndex, short x) throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public void updateShort(String columnName, short x) throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public void updateString(int columnIndex, String x) throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public void updateString(String columnName, String x) throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public void updateTime(String columnName, java.sql.Time x) throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public void updateTime(int columnIndex, java.sql.Time x) throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public void updateTimestamp(String columnName, java.sql.Timestamp x) throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public void updateTimestamp(int columnIndex, java.sql.Timestamp x) throws SQLException {
        Thread.dumpStack();
        throw new SQLException("not supported");
    }
    
    public boolean wasNull() throws SQLException {
        return was_null;
    }

    public RowId getRowId(int columnIndex) throws SQLException {
             throw new SQLException("not implemented");
    }

    public RowId getRowId(String columnLabel) throws SQLException {
             throw new SQLException("not implemented");
    }

    public void updateRowId(int columnIndex, RowId x) throws SQLException {
             throw new SQLException("not implemented");
    }

    public void updateRowId(String columnLabel, RowId x) throws SQLException {
             throw new SQLException("not implemented");
    }

    public int getHoldability() throws SQLException {
             throw new SQLException("not implemented");
    }

    public boolean isClosed() throws SQLException {
             throw new SQLException("not implemented");
    }

    public void updateNString(int columnIndex, String nString) throws SQLException {
             throw new SQLException("not implemented");
    }

    public void updateNString(String columnLabel, String nString) throws SQLException {
             throw new SQLException("not implemented");
    }

    public void updateNClob(int columnIndex, NClob nClob) throws SQLException {
             throw new SQLException("not implemented");
    }

    public void updateNClob(String columnLabel, NClob nClob) throws SQLException {
             throw new SQLException("not implemented");
    }

    public NClob getNClob(int columnIndex) throws SQLException {
             throw new SQLException("not implemented");
    }

    public NClob getNClob(String columnLabel) throws SQLException {
             throw new SQLException("not implemented");
    }

    public SQLXML getSQLXML(int columnIndex) throws SQLException {
             throw new SQLException("not implemented");
    }

    public SQLXML getSQLXML(String columnLabel) throws SQLException {
             throw new SQLException("not implemented");
    }

    public void updateSQLXML(int columnIndex, SQLXML xmlObject) throws SQLException {
             throw new SQLException("not implemented");
    }

    public void updateSQLXML(String columnLabel, SQLXML xmlObject) throws SQLException {
             throw new SQLException("not implemented");
    }

    public String getNString(int columnIndex) throws SQLException {
             throw new SQLException("not implemented");
    }

    public String getNString(String columnLabel) throws SQLException {
         throw new SQLException("not implemented");
    }

    public Reader getNCharacterStream(int columnIndex) throws SQLException {
         throw new SQLException("not implemented");
    }

    public Reader getNCharacterStream(String columnLabel) throws SQLException {
         throw new SQLException("not implemented");
    }

    public void updateNCharacterStream(int columnIndex, Reader x, long length) throws SQLException {
         throw new SQLException("not implemented");
    }

    public void updateNCharacterStream(String columnLabel, Reader reader, long length) throws SQLException {
         throw new SQLException("not implemented");
    }

    public void updateAsciiStream(int columnIndex, InputStream x, long length) throws SQLException {
         throw new SQLException("not implemented");
    }

    public void updateBinaryStream(int columnIndex, InputStream x, long length) throws SQLException {
         throw new SQLException("not implemented");
    }

    public void updateCharacterStream(int columnIndex, Reader x, long length) throws SQLException {
         throw new SQLException("not implemented");
    }

    public void updateAsciiStream(String columnLabel, InputStream x, long length) throws SQLException {
         throw new SQLException("not implemented");
    }

    public void updateBinaryStream(String columnLabel, InputStream x, long length) throws SQLException {
         throw new SQLException("not implemented");
    }

    public void updateCharacterStream(String columnLabel, Reader reader, long length) throws SQLException {
         throw new SQLException("not implemented");
    }

    public void updateBlob(int columnIndex, InputStream inputStream, long length) throws SQLException {
         throw new SQLException("not implemented");
    }

    public void updateBlob(String columnLabel, InputStream inputStream, long length) throws SQLException {
         throw new SQLException("not implemented");
    }

    public void updateClob(int columnIndex, Reader reader, long length) throws SQLException {
         throw new SQLException("not implemented");
    }

    public void updateClob(String columnLabel, Reader reader, long length) throws SQLException {
         throw new SQLException("not implemented");
    }

    public void updateNClob(int columnIndex, Reader reader, long length) throws SQLException {
         throw new SQLException("not implemented");
    }

    public void updateNClob(String columnLabel, Reader reader, long length) throws SQLException {
         throw new SQLException("not implemented");
    }

    public <T> T unwrap(Class<T> iface) throws SQLException {
         throw new SQLException("not implemented");
    }

    public boolean isWrapperFor(Class<?> iface) throws SQLException {
         throw new SQLException("not implemented");
    }
}
