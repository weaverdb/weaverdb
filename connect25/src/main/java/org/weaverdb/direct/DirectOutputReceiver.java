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

import java.nio.channels.WritableByteChannel;
import java.util.function.Supplier;
import org.weaverdb.ExecutionException;
import org.weaverdb.Statement;


class DirectOutputReceiver<T extends WritableByteChannel> extends DirectOutput<WritableByteChannel> {
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
        setValue((WritableByteChannel)type.get());
    }
}
