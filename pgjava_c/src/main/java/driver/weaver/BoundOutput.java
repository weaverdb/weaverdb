/*
 * BindVar.java
 *
 * Created on February 27, 2003, 8:35 AM
 */
package driver.weaver;

import java.io.IOException;
import java.io.OutputStream;
import java.nio.channels.Channels;
import java.nio.channels.WritableByteChannel;

/**
 *
 * @author  mscott
 */
class BoundOutput<T> extends Bound<T> {

    private final BaseWeaverConnection.Statement owner;
    private final int index;
    private String columnName;
    private Object value;

    BoundOutput(BaseWeaverConnection.Statement fc, int index, Class<T> type) throws ExecutionException {
        super(type);
        owner = fc;
        this.index = index;
    }

    BaseWeaverConnection.Statement getOwner() {
        return owner;
    }

    void setStream(OutputStream value) {
        this.value = value;
    }

    void setChannel(java.nio.channels.WritableByteChannel value) {
        this.value = value;
    }
    
    String getName() {
        return columnName;
    }
    
    int getIndex() {
        return index;
    }
    
    void reset() throws ExecutionException {
        value = null;
    }

    T get() throws ExecutionException {
        if (value == null) {
            return null;
        }
        try {
            switch (getType()) {
                case Stream:
                case Direct:
                    throw new ExecutionException("streaming type is has no value");
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
    }
    
    boolean isNull() {
        return value == null;
    }

    private int pipeOut(java.nio.ByteBuffer data) throws IOException {
        WritableByteChannel channel;
        switch (value) {
            case null -> {
                return -1;
            }
            case OutputStream os -> channel = Channels.newChannel(os);
            case WritableByteChannel w -> channel = w;
            default -> {
                return -1;
            }
        }

        if (data == null) { // close op
            channel.close();
            return -1;
        } else {
            int consumed = 0;
            while (data.hasRemaining()) {
                int part = channel.write(data);
                if (part >= 0) {
                    consumed += part;
                } else {
                    return part;
                }
            }
            return consumed;
        }
    }
}
