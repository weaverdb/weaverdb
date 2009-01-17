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
import java.io.OutputStream;
import java.nio.channels.WritableByteChannel;
import java.sql.SQLException;
import java.util.Date;

/**
 *
 * @author  mscott
 */
public class BoundOutput<T> extends Bound {

    private BaseWeaverConnection owner;
    private Class<T> type;
    private int index;
    private Types settype;
    private Object value;
    private boolean isnull = true;

    protected BoundOutput(BaseWeaverConnection fc, int index, Class<T> type) throws SQLException {
        owner = fc;
        this.type = type;
        this.index = index;
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
        } else if (type.equals(java.io.ByteArrayInputStream.class)) {
            settype = Types.BLOB;
        } else if (type.equals(Character.class)) {
            settype = Types.Character;
        } else if (type.equals(Date.class)) {
            settype = Types.Date;
        } else if (type.equals(Long.class)) {
            settype = Types.Long;
        } else if (type.equals(Boolean.class)) {
            settype = Types.Boolean;
        } else if (java.nio.channels.WritableByteChannel.class.isAssignableFrom(type)) {
            settype = Types.Direct;
        } else if (java.io.OutputStream.class.isAssignableFrom(type)) {
            settype = Types.Stream;
        } else {
            settype = Types.Java;
        }
        owner.output(index, this, settype.getId());
    }

    protected BaseWeaverConnection getOwner() {
        return owner;
    }

    public void setStream(OutputStream value) {
        this.value = value;
    }

    public void setChannel(java.nio.channels.WritableByteChannel value) {
        this.value = value;
    }

    public T get() throws SQLException {
        if (isnull) {
            return null;
        }
        try {
            switch (settype) {
                case Stream:
                    break;
                case Direct:
                    break;
                case BLOB:
                    value = new java.io.ByteArrayInputStream((byte[]) value);
                case Character:
                case Binary:
                case String:
                case Boolean:
                case Integer:
                case Date:
                case Long:
                    return type.cast(value);
                case Java:
                    value = JavaConverter.java_out((byte[]) value);
                case Double:
                default:
                    return type.cast(value);
            }
        } catch (ClassCastException exp) {
            throw new SQLException("type cast exception", exp);
        }
        return null;
    }

    protected void pipeOut(byte[] data) throws IOException {
        if ( value == null ) throw new IOException("pipe not connected");
        OutputStream os = (OutputStream) value;
        os.write(data);
    }

    protected void pipeOut(java.nio.ByteBuffer data) throws IOException {
        if ( value == null ) throw new IOException("pipe not connected");
        if (value instanceof WritableByteChannel) {
            ((WritableByteChannel) value).write(data);
        } else {
            byte[] open = new byte[data.limit()];
            data.get(open);
            ((OutputStream) value).write(open);
        }
    }
}
