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

    private final BaseWeaverConnection.Statement owner;
    private final String name;
    private Object value;

    protected BoundInput(BaseWeaverConnection.Statement fc, String name, Class<T> type) throws ExecutionException {
        super(type);
        owner = fc;
        this.name = name;
    }  
  
    public String getName() {
        return name;
    }

    protected BaseWeaverConnection.Statement getOwner() {
        return owner;
    }

    public void set(T value) throws ExecutionException {        
        if ( value == null ) {
            this.value = null;
            return;
        }
        
        switch (getType()) {
            case BLOB:
                if (value instanceof InputStream is) {
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
                    throw new ExecutionException("invalid type conversion for BLOB from " + value.getClass().getName());
                }
                break;

            case Direct:
            case Stream:
                this.value = value;
                break;
            case Character:
                if (value instanceof Character character) {
                    this.value = character;
                } else {
                    throw new ExecutionException("invalid type conversion for Character from " + value.getClass().getName());
                }
                break;

            case Binary:
                if (value instanceof InputStream is) {
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
                    throw new ExecutionException("invalid type conversion for Binary from " + value.getClass().getName());
                }
                break;

            case String:
                this.value = value;
                break;
            case Boolean:
                if (value instanceof Boolean) {
                    this.value = value;
                } else if (value instanceof Integer sb) {
                    this.value = (sb != 0);
                } else {
                    throw new ExecutionException("invalid type conversion for Boolean from " + value.getClass().getName());
                }
                break;
            case Integer:
                if (value instanceof Boolean sb) {
                    this.value = (sb) ? 1 : 0;
                } else if (value instanceof Integer) {
                    this.value = value;
                } else {
                    throw new ExecutionException("invalid type conversion for Integer from " + value.getClass().getName());
                }
                break;

            case Date:
                if (value instanceof java.util.Date) {
                    this.value = value;
                } else {
                    throw new ExecutionException("invalid type conversion for Date from " + value.getClass().getName());
                }
                break;
            case Long:
                if (value instanceof java.lang.Long) {
                    this.value = value;
                } else {
                    throw new ExecutionException("invalid type conversion for Long from " + value.getClass().getName());
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
                    throw new ExecutionException("invalid type conversion for Date from " + value.getClass().getName());
                }
                break;
            default:
            {
                throw new ExecutionException("invalid type conversion for " + getType().toString() + " from " + value.getClass().getName());
            }
        }
    }

    public void setObject(Object obj) throws ExecutionException {
        try {
            set(getTypeClass().cast(obj));
        } catch (ClassCastException cast) {
            throw new ExecutionException(cast);
        }
    }

    private int pipeIn(byte[] data) throws IOException {
        InputStream os = (InputStream)value;
        return os.read(data);
    }

    private int pipeIn(java.nio.ByteBuffer data) throws IOException {
        switch (value) {
            case ReadableByteChannel readableByteChannel -> {
                return readableByteChannel.read(data);
            }
            case InputStream inputStream -> {
                if ( data.hasArray() ) {
                    return inputStream.read(data.array(),data.arrayOffset() + data.position(),data.remaining());
                } else {
                    byte[] open = new byte[data.limit()];
                    int count = inputStream.read(open);
                    if ( count > 0 ) data.put(open,0,count);
                    return count;
                }
            }
            default -> {
                return -1;
            }
        }
    }
}
