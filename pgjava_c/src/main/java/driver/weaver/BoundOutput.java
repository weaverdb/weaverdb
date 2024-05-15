/*
 * BindVar.java
 *
 * Created on February 27, 2003, 8:35 AM
 */
package driver.weaver;

import java.io.IOException;
import java.io.OutputStream;
import java.nio.channels.WritableByteChannel;

/**
 *
 * @author  mscott
 */
public class BoundOutput<T> extends Bound<T> {

    private final BaseWeaverConnection.Statement owner;
    private final int index;
    private String columnName;
    private Object value;

    protected BoundOutput(BaseWeaverConnection.Statement fc, int index, Class<T> type) throws ExecutionException {
        super(type);
        owner = fc;
        this.index = index;
    }

    protected BaseWeaverConnection.Statement getOwner() {
        return owner;
    }

    public void setStream(OutputStream value) {
        this.value = value;
    }

    public void setChannel(java.nio.channels.WritableByteChannel value) {
        this.value = value;
    }
    
    public String getName() {
        return columnName;
    }
    
    public int getIndex() {
        return index;
    }

    public T get() throws ExecutionException {
        if (value == null) {
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
            throw new ExecutionException("type cast exception", exp);
        }
        return null;
    }
    
    public boolean isNull() {
        return value == null;
    }

    private int pipeOut(byte[] data) throws IOException {
        if ( value == null ) throw new IOException("pipe not connected");
        OutputStream os = (OutputStream) value;
        os.write(data);
        return data.length;
    }

    private int pipeOut(java.nio.ByteBuffer data) throws IOException {
        int len = data.remaining();
        switch (value) {
            case null -> throw new IOException("pipe not connected");
            case WritableByteChannel writableByteChannel -> {
                int consumed = 0;
                while (data.hasRemaining()) {
                    int part = writableByteChannel.write(data);
                    if (part >= 0) {
                        consumed += part;
                    } else {
                        return part;
                    }
                }
                return consumed;
            }
            case OutputStream outputStream -> {
                if ( data.hasArray() ) {
                    outputStream.write(data.array(),data.position() + data.arrayOffset(),data.remaining());
                    return len;
                } else {
                    byte[] open = new byte[data.remaining()];
                    data.get(open);
                    outputStream.write(open);
                    return len;
                }
            }
            default -> {
                return -1;
            }
        }
    }
}
