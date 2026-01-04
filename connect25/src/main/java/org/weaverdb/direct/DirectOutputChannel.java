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
import java.lang.foreign.MemorySegment;
import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;
import java.nio.ByteBuffer;
import java.nio.channels.Pipe;
import java.nio.channels.WritableByteChannel;
import java.util.concurrent.Future;
import org.weaverdb.ExecutionException;
import org.weaverdb.Output;
import org.weaverdb.Statement;
import org.weaverdb.StreamingTransformer;


class DirectOutputChannel<T> extends DirectOutput<WritableByteChannel> {
    private final static MethodHandle transferOut;
    
    static {
        MethodHandles.Lookup lookup = MethodHandles.lookup();
        MethodHandle temp = null;
        try {
            temp = lookup.findVirtual(DirectOutputChannel.class, "transferOut", MethodType.methodType(int.class, MemorySegment.class, int.class, MemorySegment.class, int.class));
        } catch (IllegalAccessException | NoSuchMethodException ia) {

        }
        transferOut = temp;
    }

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
            super.set(comms.sink());
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
    
    private int transferOut(MemorySegment user, int type, MemorySegment var, int varSize) {
        WritableByteChannel w = super.get();
        
        try {
            if (var == MemorySegment.NULL) {
                w.close();
                return -1;
            } else {
                ByteBuffer bb = var.asByteBuffer();
                int size = 0;
                while (size < varSize) {
                    size += w.write(bb);
                }
                return size;
            }
        } catch (IOException io) {
            
        }
        
        return 0;
    }
    
    @Override
    MethodHandle getTransferFunction() {
        return transferOut;
    }
}
