/*

 */
package driver.weaver;

import java.io.IOException;
import java.io.OutputStream;
import java.nio.channels.WritableByteChannel;

/**
 *
 */
public class Input<T> {
    private final Setter<T> source;
    
    Input(BoundInput<T> base) {
        source = base::set;
    }
    
    Input(BoundInputChannel<T> base) {
        source = base::put;
    }    
    
    public void value(T value) throws ExecutionException {
        source.value(value);
    }
    
    public void set(T value) throws ExecutionException {
        source.value(value);
    }
    
    private static interface Setter<T> {
        void value(T value) throws ExecutionException;
    }
    
    @FunctionalInterface
    public static interface Channel<T> {
        void transform(T value, WritableByteChannel c) throws IOException;
    }    
    
    @FunctionalInterface
    public static interface Stream<T> {
        void transform(T value, OutputStream c) throws IOException;
    }  
}
