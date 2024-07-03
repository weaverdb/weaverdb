/*
 * Copyright (c) 2024 Myron Scott <myron@weaverdb.org> All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 */


package org.weaverdb;

import java.nio.channels.WritableByteChannel;
import java.util.function.Supplier;


class BoundOutputReceiver<T> extends BoundOutput<WritableByteChannel> {
    private final Supplier<T> type;

    BoundOutputReceiver(Statement fc, int index, Supplier<T> type) throws ExecutionException {
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
