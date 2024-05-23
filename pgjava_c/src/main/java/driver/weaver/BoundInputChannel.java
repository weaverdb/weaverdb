/*

 */
package driver.weaver;

import java.io.IOException;
import java.nio.channels.Pipe;
import java.nio.channels.ReadableByteChannel;

/**
 *
 * @author myronscott
 */
class BoundInputChannel<T> extends BoundInput<ReadableByteChannel> {
    
    private final StreamingTransformer transformer;
    private final Input.Channel<T> type;

    BoundInputChannel(BaseWeaverConnection.Statement fc, StreamingTransformer engine, String name, Input.Channel<T> type) throws ExecutionException {
        super(fc, name, ReadableByteChannel.class);
        this.transformer = engine;
        this.type = type;
    }

    @Override
    public void set(ReadableByteChannel value) throws ExecutionException {
        throw new ExecutionException("use put instead");
    }
    
    void put(T value) throws ExecutionException {
        try {
            Pipe comms = Pipe.open();
            super.setChannel(comms.source());
            transformer.schedule(()->{
                try {
                    type.transform(value, comms.sink());
                    return null;
                } finally {
                    if (comms.sink().isOpen()) {
                        comms.sink().close();
                    }
                }
            });
        } catch (IOException io) {
            throw new ExecutionException(io);
        }
    }
}
