/*
 * WeaverHeader.java
 *
 * Created on November 17, 2003, 6:50 AM
 */

package driver.weaver.jdbc;

/**
 *
 * @author  mscott
 */
public class WeaverHeader {
    Class<?> return_type;
    String converter;
    String path;
    int sql_type = java.sql.Types.NULL;
    String type_name = null;
    boolean currency = false;
    /**
     * Creates a new instance of WeaverHeader
     */
    public WeaverHeader(Object type,String convert,String p) {
        return_type = (Class)type;
        if ( convert != null ) converter = convert.replace('.','/');
        path = p;
    }
    
    public Class getType() {
        return return_type;
    }
    
    public String getConverter() {
        return converter;
    }
    
    public String getPath() {
        return path;
    }
    
    public String getName() {
        if ( converter != null && converter.length() > 0 ) {
            return converter + "(" + path + ")";
        } else {
            return path;
        }
    }
    
    public String getLabel() {
        return path;
        
    }
    
    public int getSQLType() {
        if ( sql_type != java.sql.Types.NULL ) return sql_type;
        
        if ( return_type.equals(Integer.class) ) {
            sql_type = java.sql.Types.INTEGER;
        } else if ( return_type.equals(String.class) ) {
            sql_type = java.sql.Types.VARCHAR;
        } else if ( return_type.equals(Double.class) ) {
            sql_type = java.sql.Types.DOUBLE;
        } else if ( return_type.equals(Boolean.class) ) {
            sql_type = java.sql.Types.BOOLEAN;  
        } else if ( return_type.equals(Character.class) ) {
            sql_type = java.sql.Types.CHAR;
        } else if ( return_type.equals(Float.class) ) {
            sql_type = java.sql.Types.FLOAT;
        } else if ( return_type.equals(java.util.Date.class) ) {
            sql_type = java.sql.Types.DATE;
        } else if ( return_type.equals(Long.class) ) {
            sql_type= java.sql.Types.BIGINT;
        } else if ( Number.class.isAssignableFrom(return_type) ) {
            sql_type = java.sql.Types.NUMERIC;
        } else if ( return_type.isArray() ) {
            sql_type = java.sql.Types.ARRAY;
        }
        return sql_type;
    }
    
    public String getSQLName() {
        if ( type_name != null ) return type_name;
        if ( return_type.equals(Integer.class) ) {
            type_name = "integer";
        } else if ( return_type.equals(String.class) ) {
            type_name = "varchar";
        } else if ( return_type.equals(Double.class) ) {
            type_name = "double";
        } else if ( return_type.equals(Boolean.class) ) {
/* this is for staroffice might need to change it later to boolean */
            type_name = "boolean";
        } else if ( return_type.equals(Character.class) ) {
            type_name = "character";
        } else if ( return_type.equals(Float.class) ) {
            type_name = "float";
        } else if ( return_type.equals(java.util.Currency.class) ) {
            type_name = "currency";
        } else if ( return_type.equals(java.util.Date.class) ) {
            type_name = "date";
        } else if ( return_type.equals(java.lang.Long.class ) ) {
            type_name = "long";
        } else if ( return_type.isAssignableFrom(Number.class) ) {
            type_name = "numeric";
        } else if ( return_type.isArray() ) {
            type_name = "array";
        } else {
            type_name = "java_object";
        }
        return type_name;
    }
    
    public int getScale() {
       if ( sql_type == java.sql.Types.NULL ) getSQLType();
       return 9;
    }
    
    public int getPrecision() {
        if ( sql_type == java.sql.Types.NULL ) getSQLType();
        return 9;
    }
    
    public boolean isCurrency() {
        if ( sql_type == java.sql.Types.NULL ) getSQLType();
        return currency;
    }
}
