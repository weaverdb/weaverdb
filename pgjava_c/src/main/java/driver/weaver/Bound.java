/*
 * BindVar.java
 *
 * Created on February 27, 2003, 8:35 AM
 */

package driver.weaver;

/**
 *
 * @author  mscott
 */
class Bound<T> {

    private final Types settype;
    private final Class<T> type; 
    private boolean orphaned;

    Bound(Class<T> type) {
        this.type = type;
        this.settype = bind(this.type);
    }
    
    private static Types bind(Class<?> type) {
        if (type.equals(String.class)) {
            return Types.String;
        } else if (type.equals(Double.class)) {
            return Types.Double;
        } else if (type.equals(Integer.class)) {
            return Types.Integer;
        } else if (type.equals(byte[].class)) {
            return Types.Binary;
        } else if (type.equals(java.io.ByteArrayInputStream.class)) {
            return Types.BLOB;
        } else if (type.equals(java.io.ByteArrayOutputStream.class)) {
            return Types.BLOB;
        } else if (type.equals(Character.class)) {
            return Types.Character;
        } else if (type.equals(java.util.Date.class)) {
            return Types.Date;
        } else if (type.equals(Long.class)) {
            return Types.Long;
        } else if (type.equals(Boolean.class)) {
            return Types.Boolean;
        } else if (java.nio.channels.WritableByteChannel.class.isAssignableFrom(type)) {
            return Types.Direct;
        } else if (java.nio.channels.ReadableByteChannel.class.isAssignableFrom(type)) {
            return Types.Direct;
        } else if (java.io.OutputStream.class.isAssignableFrom(type)) {
            return Types.Stream;
        } else if (java.io.InputStream.class.isAssignableFrom(type)) {
            return Types.Stream;
        } else {
            return Types.Java;
        }
    }
    
    protected enum Types {
        String(2,"Ljava/lang/String;"),
        Double(3,"D"),
        Integer(1,"I"),
        Binary(6,"[B"),
        BLOB(7,"[B"),
        Character(4,"C"),
        Boolean(5,"Z"),
        Date(12,"[B"),
        Long(13,"[B"),
        Function(20,"[B"),
        Slot(30,"[B"),
        Java(40,"[B"),
        Text(41,"[B"),
        Stream(42,"<assigned>"),
        Direct(43,"<direct>"),
        Null(0,"");
                
        private final int id;
        private final String signature;
        
        Types(int id, String sig) {
            this.id = id;
            this.signature = sig;
        }
        
        int getId() {
            return id;
        }
        
        String getSignature() {
            return signature;
        }
    }

    Class<T> getTypeClass() {
        return type;
    }

    boolean isSameType(Class t) {
        return t.equals(type);
    }

    Types getType() {
        return settype;
    }

    int getTypeId() {
        return settype.getId();
    }

    void deactivate() {
        orphaned = false;
    }
    
    boolean isOrphaned() {
        return orphaned;
    }
}
