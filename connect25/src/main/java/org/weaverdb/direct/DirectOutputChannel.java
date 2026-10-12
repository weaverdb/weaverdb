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
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;
import java.lang.foreign.ValueLayout;
import java.nio.ByteBuffer;
import java.nio.channels.Pipe;
import java.nio.channels.WritableByteChannel;
import java.nio.charset.StandardCharsets;
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
        try {
            WritableByteChannel w = super.get();
            /*
             * The column name arrives as META before the payload. Writing it
             * into the pipe prefixes the value with the column name.
             */
            if (type == DirectWeaverConnection.META) {
                if (varSize > 0 && var != null && !var.equals(MemorySegment.NULL)) {
                    try (Arena arena = Arena.ofConfined()) {
                        MemorySegment name = var.reinterpret(varSize, arena, null);
                        noteColumn(new String(name.toArray(ValueLayout.JAVA_BYTE), StandardCharsets.UTF_8));
                    }
                }
                return varSize > 0 ? varSize : 0;
            }
            /* CLOSE_OP arrives as a negative size on a null pointer. */
            if (varSize < 0 || var == null || var.equals(MemorySegment.NULL)) {
                if (w != null) {
                    try {
                        w.close();
                    } catch (IOException io) {
                    }
                }
                return -1;
            }
            try (Arena arena = Arena.ofConfined()) {
                var = var.reinterpret(varSize, arena, null);
                ByteBuffer bb = var.asByteBuffer();
                int size = 0;
                while (size < varSize) {
                    int wrote = w.write(bb);
                    if (wrote <= 0) {
                        return size > 0 ? size : -1;
                    }
                    size += wrote;
                }
                return size;
            }
        } catch (Throwable ex) {
            return -1;
        }
    }
    
    @Override
    MethodHandle getTransferFunction() {
        return transferOut;
    }
}
