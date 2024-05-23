/*
 */
package driver.weaver;

import java.nio.channels.WritableByteChannel;
import java.util.function.Supplier;

/**
 *
 * @author myronscott
 */
class BoundOutputReceiver<T> extends BoundOutput<WritableByteChannel> {
    private final Supplier<T> type;

    BoundOutputReceiver(BaseWeaverConnection.Statement fc, int index, Supplier<T> type) throws ExecutionException {
        super(fc, index, WritableByteChannel.class);
        this.type = type;
    }
    
    T value() throws ExecutionException {
        return (T)get();
    }
    
    @Override
    void reset() throws ExecutionException {
        setChannel((WritableByteChannel)type.get());
    }
}
