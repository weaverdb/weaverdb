/*
 * BindVar.java
 *
 * Created on February 27, 2003, 8:35 AM
 */
package driver.weaver;

import java.io.IOException;
import java.io.OutputStream;
import java.nio.channels.WritableByteChannel;
import java.sql.SQLException;
import java.util.Date;

/**
 *
 * @author  mscott
 */
public class BoundOutput<T> extends Bound<T> {

    private BaseWeaverConnection owner;
    private long link;
    private int index;
    private Object value;
    private boolean isnull = true;

    protected BoundOutput(BaseWeaverConnection fc, long link, int index, Class<T> type) throws SQLException {
        owner = fc;
        this.link = link;
        setTypeClass(type);
        this.index = index;
        bind();
        fc.getOutput(link,index,getTypeId(),this);
    } 

    private void bind() throws SQLException {
        Class<T> type = getTypeClass();
        if (type.equals(String.class)) {
           setType(Types.String);
        } else if (type.equals(Double.class)) {
            setType(Types.Double);
        } else if (type.equals(Integer.class)) {
            setType(Types.Integer);
        } else if (type.equals(byte[].class)) {
            setType(Types.Binary);
        } else if (type.equals(java.io.ByteArrayInputStream.class)) {
            setType(Types.BLOB);
        } else if (type.equals(Character.class)) {
            setType(Types.Character);
        } else if (type.equals(Date.class)) {
            setType(Types.Date);
        } else if (type.equals(Long.class)) {
            setType(Types.Long);
        } else if (type.equals(Boolean.class)) {
            setType(Types.Boolean);
        } else if (java.nio.channels.WritableByteChannel.class.isAssignableFrom(type)) {
            setType(Types.Direct);
        } else if (java.io.OutputStream.class.isAssignableFrom(type)) {
            setType(Types.Stream);
        } else {
            setType(Types.Java);
        }
/*        owner.output(index, this, settype.getId());  */
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
//        if ( !isActive() ) {
//            throw new SQLException("output variable is orphaned");
//        }
        if (isnull) {
            return null;
        }
        try {
            switch (getType()) {
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
                    return getTypeClass().cast(value);
                case Java:
                    value = JavaConverter.java_out((byte[]) value);
                case Double:
                default:
                    return getTypeClass().cast(value);
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
            if ( data.hasArray() ) {
                ((OutputStream) value).write(data.array(),data.position() + data.arrayOffset(),data.remaining());
            } else {
                byte[] open = new byte[data.remaining()];
                data.get(open);
                ((OutputStream) value).write(open);
            }
        }
    }
}
