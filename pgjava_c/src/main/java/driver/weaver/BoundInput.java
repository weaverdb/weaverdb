/*
 * BindVar.java
 *
 * Created on February 27, 2003, 8:35 AM
 */
package driver.weaver;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.channels.ReadableByteChannel;

/**
 *
 * @author  mscott
 */
public class BoundInput<T> extends Bound<T> {

    private final BaseWeaverConnection owner;
    private final long  link;
    private final String name;
    private Object value;

    protected BoundInput(BaseWeaverConnection fc, long id,String name, Class<T> type) throws WeaverException {
        owner = fc;
        link = id;
        setTypeClass(type);
        this.name = name;
        bind();
    }  
   
    private void bind() throws WeaverException {
        Class<T> type = getTypeClass();
        if (type.equals(String.class)) {
            setType(Types.String);
        } else if (type.equals(Double.class)) {
            setType(Types.Double);
        } else if (type.equals(Integer.class)) {
            setType(Types.Integer);
        } else if (type.equals(byte[].class)) {
            setType(Types.Binary);
        } else if (type.equals(java.io.ByteArrayOutputStream.class)) {
            setType(Types.BLOB);
        } else if (type.equals(Character.class)) {
            setType(Types.Character);
        } else if (type.equals(java.util.Date.class)) {
            setType(Types.Date);
        } else if (type.equals(Long.class)) {
            setType(Types.Long);
        } else if (type.equals(Boolean.class)) {
            setType(Types.Boolean);
        } else if (java.nio.channels.ReadableByteChannel.class.isAssignableFrom(type)) {
            setType(Types.Direct);
        } else if (java.io.InputStream.class.isAssignableFrom(type)) {
            setType(Types.Stream);
        } else {
            setType(Types.Java);
        }
    }

    protected BaseWeaverConnection getOwner() {
        return owner;
    }

    public void set(T value) throws WeaverException {        
        if ( value == null ) {
            this.value = null;
            return;
        }
        
        switch (getType()) {
            case BLOB:
                if (value instanceof InputStream) {
                    InputStream is = (InputStream) value;
                    ByteArrayOutputStream out = new ByteArrayOutputStream();
                    byte[] buff = new byte[1024];
                    try {
                        int read = is.read(buff);
                        while (read >= 0) {
                            out.write(buff, 0, read);
                            read = is.read(buff);
                        }
                    } catch (IOException ioe) {
                    }
                    this.value = out.toByteArray();
                } else if (value instanceof ByteArrayOutputStream is) {
                    this.value = is.toByteArray();
                } else if (value instanceof byte[]) {
                    this.value = value;
                } else {
                    String name = ( value != null ) ? value.getClass().getName() : "NULL";
                    throw new WeaverException("invalid type conversion for BLOB from " + name);
                }
                break;
            case Direct:
            case Stream:
                this.value = value;
                break;
            case Character:
                if (value instanceof Character) {
                    this.value = (Character) value;
                } else {
                    String name = ( value != null ) ? value.getClass().getName() : "NULL";
                    throw new WeaverException("invalid type conversion for Character from " + name);
                }
                break;
            case Binary:
                if (value instanceof InputStream) {
                    InputStream is = (InputStream) value;
                    ByteArrayOutputStream out = new ByteArrayOutputStream();
                    byte[] buff = new byte[1024];
                    try {
                        int read = is.read(buff);
                        while (read >= 0) {
                            out.write(buff, 0, read);
                            read = is.read(buff);
                        }
                    } catch (IOException ioe) {
                    }
                    this.value = out.toByteArray();
                } else if (value instanceof ByteArrayOutputStream) {
                    ByteArrayOutputStream is = (ByteArrayOutputStream) value;
                    this.value = is.toByteArray();
                } else if (value instanceof byte[]) {
                    this.value = value;
                } else {
                    String name = ( value != null ) ? value.getClass().getName() : "NULL";
                    throw new WeaverException("invalid type conversion for Binary from " + name);
                }
                break;
            case String:
                this.value = value;
                break;
            case Boolean:
                if (value instanceof Boolean) {
                    this.value = value;
                } else if (value instanceof Integer) {
                    Integer sb = (Integer) value;
                    this.value = (sb.intValue() != 0);
                } else {
                    String name = ( value != null ) ? value.getClass().getName() : "NULL";
                    throw new WeaverException("invalid type conversion for Boolean from " + name);
                }
                break;
            case Integer:
                if (value instanceof Boolean) {
                    Boolean sb = (Boolean) value;
                    this.value = (sb) ? 1 : 0;
                } else if (value instanceof Integer) {
                    this.value = value;
                } else {
                    String name = ( value != null ) ? value.getClass().getName() : "NULL";
                    throw new WeaverException("invalid type conversion for Integer from " + name);
                }
                break;
            case Date:
                if (value instanceof java.util.Date) {
                    this.value = value;
                } else {
                    String name = ( value != null ) ? value.getClass().getName() : "NULL";
                    throw new WeaverException("invalid type conversion for Date from " + name);
                }
                break;
            case Long:
                if (value instanceof java.lang.Long) {
                    this.value = value;
                } else {
                    String name = ( value != null ) ? value.getClass().getName() : "NULL";
                    throw new WeaverException("invalid type conversion for Long from " + name);
                }
                break;
            case Java:
                byte[] binary = JavaConverter.java_in(value);
                this.value = binary;
                break;
            case Double:
                if (value instanceof Double) {
                    this.value = value;
                } else {
                    String name = ( value != null ) ? value.getClass().getName() : "NULL";
                    throw new WeaverException("invalid type conversion for Date from " + name);
                }
                break;
            default:
            {
                String name = ( value != null ) ? value.getClass().getName() : "NULL";
                throw new WeaverException("invalid type conversion for " + getType().toString() + " from " + name);
            }
        }
    }

    public void setObject(Object obj) throws WeaverException {
        try {
            set(getTypeClass().cast(obj));
        } catch (ClassCastException cast) {
            throw new WeaverException(cast);
        }
    }

    protected int pipeIn(byte[] data) throws IOException {
        InputStream os = (InputStream)value;
        return os.read(data);
    }

    protected int pipeIn(java.nio.ByteBuffer data) throws IOException {
        if (value instanceof ReadableByteChannel) {
            return ((ReadableByteChannel) value).read(data);
        } else {
            if ( data.hasArray() ) {
                 return ((InputStream) value).read(data.array(),data.arrayOffset() + data.position(),data.remaining());
            } else {
                byte[] open = new byte[data.limit()];
                int count = ((InputStream) value).read(open);
                if ( count > 0 ) data.put(open,0,count);
                return count;
            }
        }
    }
}
