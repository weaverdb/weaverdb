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
    private final Setter<? super T> source;
    private final Class<? super T> type;
    
    Input(BoundInput<? super T> base) {
        source = base::set;
        type = base.getTypeClass();
    }
    
    Input(BoundInputChannel<? super T> base) {
        source = base::put;
        type = null;
    }    
    
    public void value(T value) throws ExecutionException {
        source.value(value);
    }
    
    public void set(T value) throws ExecutionException {
        source.value(value);
    }
    
    @FunctionalInterface
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
