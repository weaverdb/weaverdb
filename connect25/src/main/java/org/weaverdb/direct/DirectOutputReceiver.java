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
import java.lang.foreign.MemorySegment;
import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;
import java.nio.ByteBuffer;
import java.nio.channels.WritableByteChannel;
import java.util.function.Supplier;
import org.weaverdb.ExecutionException;
import org.weaverdb.Statement;


class DirectOutputReceiver<T extends WritableByteChannel> extends DirectOutput<WritableByteChannel> {
    private final static MethodHandle transfer;
    
    static {
        MethodHandles.Lookup lookup = MethodHandles.lookup();
        MethodHandle temp = null;
        try {
            temp = lookup.findVirtual(DirectOutputReceiver.class, "transfer", MethodType.methodType(int.class, MemorySegment.class, int.class, MemorySegment.class, int.class));
        } catch (IllegalAccessException | NoSuchMethodException ia) {

        }
        transfer = temp;
    }

    private final Supplier<T> type;

    DirectOutputReceiver(Statement fc, int index, Supplier<T> type) throws ExecutionException {
        super(index, WritableByteChannel.class);
        this.type = type;
    }
    
    T transform() throws ExecutionException {
        return (T)get();
    }
    
    @Override
    void reset() throws ExecutionException {
        set(type.get());
    }
    
    private int transfer(MemorySegment user, int type, MemorySegment var, int varSize) {
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
        return transfer;
    }
    
    
}
