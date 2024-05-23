/*

 */
package driver.weaver;

import java.io.IOException;
import java.io.InputStream;
import java.nio.channels.ReadableByteChannel;
import java.util.function.Supplier;

/**
 *
 */
public class Output<T> {
    private final Supplier<String> name;
    private final Getter<T> source;
    
    Output(BoundOutput<T> base) {
        source = base::get;
        name = base::getName;
    }
    
    Output(BoundOutputChannel<T> base) {
        source = base::value;
        name = base::getName;
    }
    
    Output(BoundOutputReceiver<T> base) {
        source = base::value;
        name = base::getName;
    }
    
    public String getName() {
        return name.get();
    }
    
    public T value() throws ExecutionException {
        return source.value();
    }
    
    public T get() throws ExecutionException {
        return source.value();
    }
    
    private static interface Getter<T> {
        T value() throws ExecutionException;
    }

    public static interface Channel<T> {
        T transform(ReadableByteChannel src) throws IOException;
    }
    
    public static interface Stream<T> {
        T transform(InputStream src) throws IOException;
    }
}
