/*-------------------------------------------------------------------------
 *
 *
 * Copyright (c) 2000-2024, Myron Scott  <myron@weaverdb.org>
 *
 * All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 *
 *-------------------------------------------------------------------------
 */

/*
 */
package org.weaverdb.direct;

import java.io.IOException;
import java.nio.channels.Pipe;
import java.nio.channels.WritableByteChannel;
import java.util.concurrent.Future;
import org.weaverdb.ExecutionException;
import org.weaverdb.Output;
import org.weaverdb.Statement;
import org.weaverdb.StreamingTransformer;


class DirectOutputChannel<T> extends DirectOutput<WritableByteChannel> {
    private final StreamingTransformer transformer;
    private final Output.Channel<? extends T> type;
    private Future<T> futurevalue;

    DirectOutputChannel(Statement fc, StreamingTransformer engine, int index, Output.Channel<T> type) throws ExecutionException {
        super(index, WritableByteChannel.class);
        this.transformer = engine;
        this.type = type;
    }
    
    public T transform() throws ExecutionException {
        try {
            if (futurevalue == null) {
                throw new ExecutionException("statement has not been fetched");
            }
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
            super.setValue(comms.sink());
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
