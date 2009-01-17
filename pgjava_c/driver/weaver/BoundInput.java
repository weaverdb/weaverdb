/*
 * BindVar.java
 *
 * Created on February 27, 2003, 8:35 AM
 */
package driver.weaver;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.sql.SQLException;
import java.io.InputStream;
import java.nio.channels.ReadableByteChannel;

/**
 *
 * @author  mscott
 */
public class BoundInput<T> extends Bound {

    private BaseWeaverConnection owner;
    private Class<T> type;
    private String name;
    private Types settype;
    private Object stream_holder;

    protected BoundInput(BaseWeaverConnection fc, String name, Class<T> type) throws SQLException {
        owner = fc;
        this.type = type;
        this.name = name;
        bind();
    }

    private void bind() throws SQLException {
        settype = Types.Null;
        if (type.equals(String.class)) {
            settype = Types.String;
        } else if (type.equals(Double.class)) {
            settype = Types.Double;
        } else if (type.equals(Integer.class)) {
            settype = Types.Integer;
        } else if (type.equals(byte[].class)) {
            settype = Types.Binary;
        } else if (type.equals(java.io.ByteArrayOutputStream.class)) {
            settype = Types.BLOB;
        } else if (type.equals(Character.class)) {
            settype = Types.Character;
        } else if (type.equals(java.util.Date.class)) {
            settype = Types.Date;
        } else if (type.equals(Long.class)) {
            settype = Types.Long;
        } else if (type.equals(Boolean.class)) {
            settype = Types.Boolean;
        } else if (java.nio.channels.ReadableByteChannel.class.isAssignableFrom(type)) {
            settype = Types.Direct;
        } else if (java.io.InputStream.class.isAssignableFrom(type)) {
            settype = Types.Stream;
        } else {
            settype = Types.Java;
        }
        owner.bind(name, settype.getId());
    }

    protected BaseWeaverConnection getOwner() {
        return owner;
    }

    public void set(T value) throws SQLException {
        switch (settype) {
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
                    owner.setBind(name, out.toByteArray());
                } else if (value instanceof ByteArrayOutputStream) {
                    ByteArrayOutputStream is = (ByteArrayOutputStream) value;
                    owner.setBind(name, is.toByteArray());
                } else if (value instanceof byte[]) {
                    owner.setBind(name, value);
                } else {
                    throw new SQLException("invalid type conversion for BLOB from " + value.getClass().getName());
                }
                break;
            case Direct:
            case Stream:
                this.stream_holder = value;
                if ( value != null ) {
                    owner.setBind(name, this);
                } else {
                    owner.setBind(name,null);
                }
                break;
            case Character:
                if (value instanceof Character) {
                    owner.setBind(name, (Character) value);
                } else {
                    throw new SQLException("invalid type conversion for Character from " + value.getClass().getName());
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
                    owner.setBind(name, out.toByteArray());
                } else if (value instanceof ByteArrayOutputStream) {
                    ByteArrayOutputStream is = (ByteArrayOutputStream) value;
                    owner.setBind(name, is.toByteArray());
                } else if (value instanceof byte[]) {
                    owner.setBind(name, (byte[]) value);
                } else {
                    throw new SQLException("invalid type conversion for Binary from " + value.getClass().getName());
                }
                break;
            case String:
                owner.setBind(name, value);
                break;
            case Boolean:
                if (value instanceof Boolean) {
                    owner.setBind(name, value);
                } else if (value instanceof Integer) {
                    Integer sb = (Integer) value;
                    owner.setBind(name, (sb.intValue() != 0));
                } else {
                    throw new SQLException("invalid type conversion for Boolean from " + value.getClass().getName());
                }
                break;
            case Integer:
                if (value instanceof Boolean) {
                    Boolean sb = (Boolean) value;
                    owner.setBind(name, (sb) ? 1 : 0);
                } else if (value instanceof Integer) {
                    owner.setBind(name, value);
                } else {
                    throw new SQLException("invalid type conversion for Integer from " + value.getClass().getName());
                }
                break;
            case Date:
                if (value instanceof java.util.Date) {
                    owner.setBind(name, value);
                } else {
                    throw new SQLException("invalid type conversion for Date from " + value.getClass().getName());
                }
                break;
            case Long:
                if (value instanceof java.lang.Long) {
                    owner.setBind(name, value);
                } else {
                    throw new SQLException("invalid type conversion for Long from " + value.getClass().getName());
                }
                break;
            case Java:
                byte[] binary = JavaConverter.java_in(value);
                owner.setBind(name, binary);
                break;
            case Double:
                if (value instanceof Double) {
                    owner.setBind(name, value);
                } else {
                    throw new SQLException("invalid type conversion for Date from " + value.getClass().getName());
                }
                break;
            default:
                throw new SQLException("invalid type conversion for " + settype.toString() + " from " + value.getClass().getName());
        }
    }

    public void setObject(Object obj) throws SQLException {
        try {
            set(type.cast(obj));
        } catch (ClassCastException cast) {
            throw new SQLException(cast);
        }
    }

    protected int pipeIn(byte[] data) throws IOException {
        InputStream os = (InputStream) stream_holder;
        return os.read(data);
    }

    protected int pipeIn(java.nio.ByteBuffer data) throws IOException {
        if (stream_holder instanceof ReadableByteChannel) {
            return ((ReadableByteChannel) stream_holder).read(data);
        } else {
            byte[] open = new byte[data.limit()];
            data.get(open);
            return ((InputStream) stream_holder).read(open);
        }
    }
}
