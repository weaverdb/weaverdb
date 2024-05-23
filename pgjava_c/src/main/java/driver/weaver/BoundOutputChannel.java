/*
 */
package driver.weaver;

import java.io.IOException;
import java.nio.channels.Pipe;
import java.nio.channels.WritableByteChannel;
import java.util.concurrent.Future;

/**
 *
 * @author myronscott
 */
class BoundOutputChannel<T> extends BoundOutput<WritableByteChannel> {
    private final StreamingTransformer transformer;
    private final Output.Channel<T> type;
    private Future<T> futurevalue;

    BoundOutputChannel(BaseWeaverConnection.Statement fc, StreamingTransformer engine, int index, Output.Channel<T> type) throws ExecutionException {
        super(fc, index, WritableByteChannel.class);
        this.transformer = engine;
        this.type = type;
    }
    
    @Override
    WritableByteChannel get() throws ExecutionException {
        throw new ExecutionException("use value() instead");
    }
    
    public T value() throws ExecutionException {
        try {
            return futurevalue.get();
        } catch (java.util.concurrent.ExecutionException | InterruptedException ee) {
            throw new ExecutionException(ee);
        }
    }
    
    @Override
    void reset() throws ExecutionException {
        try {
            super.reset();
            Pipe comms = Pipe.open();
            super.setChannel(comms.sink());
            futurevalue = transformer.schedule(()->{
                try {
                    return type.transform(comms.source());
                } finally {
                    if (comms.source().isOpen()) {
                        comms.source().close();
                    }
                }
            });
        } catch (IOException io) {
            throw new ExecutionException(io);
        }
    }
}
