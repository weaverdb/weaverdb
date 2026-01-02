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

package org.weaverdb.direct;

import java.io.IOException;
import java.nio.channels.Pipe;
import java.nio.channels.ReadableByteChannel;
import org.weaverdb.ExecutionException;
import org.weaverdb.Input;
import org.weaverdb.StreamingTransformer;

class DirectInputChannel<T> extends DirectInput<ReadableByteChannel> {
    
    private final StreamingTransformer transformer;
    private final Input.Channel<? super T> type;

    DirectInputChannel(StreamingTransformer engine, String name, Input.Channel<T> type) throws ExecutionException {
        super(name, ReadableByteChannel.class);
        this.transformer = engine;
        this.type = type;
    }
    
    void put(T value) throws ExecutionException {
        try {
            Pipe comms = Pipe.open();
            super.set(comms.source());
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
