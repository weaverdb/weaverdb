/*
 * BindVar.java
 *
 * Created on February 27, 2003, 8:35 AM
 */
package org.weaverdb;

import java.io.IOException;
import java.io.OutputStream;
import java.nio.channels.Channels;
import java.nio.channels.WritableByteChannel;

/**
 *
 * @author  mscott
 */
class BoundOutput<T> extends Bound<T> {

    private final Statement owner;
    private final int index;
    private String columnName;
    private Object value;

    BoundOutput(Statement fc, int index, Class<T> type) throws ExecutionException {
        super(type);
        owner = fc;
        this.index = index;
    }

    Statement getOwner() {
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
                    return getTypeClass().cast(new java.io.ByteArrayInputStream((byte[]) value));
                case Character:
                case Binary:
                case String:
                case Boolean:
                case Integer:
                case Date:
                case Long:
                    return getTypeClass().cast(value);
                case Java:
                    return getTypeClass().cast(JavaConverter.java_out((byte[]) value));
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
        WritableByteChannel channel = null;
        if (value == null) {
            return -1;
        } else if (value instanceof OutputStream os) {
            channel = Channels.newChannel(os);
        } else if (value instanceof WritableByteChannel w) {
            channel = w;
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
