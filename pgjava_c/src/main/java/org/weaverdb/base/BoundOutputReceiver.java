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


package org.weaverdb.base;

import java.nio.channels.WritableByteChannel;
import java.util.function.Supplier;
import org.weaverdb.ExecutionException;


class BoundOutputReceiver<T extends WritableByteChannel> extends BoundOutput<WritableByteChannel> {
    private final Supplier<T> type;

    BoundOutputReceiver(int index, Supplier<T> type) throws ExecutionException {
        super(index, WritableByteChannel.class);
        this.type = type;
    }
    
    T value() throws ExecutionException {
        return (T)get();
    }
    
    @Override
    void reset() throws ExecutionException {
        set(type.get());
    }
}
