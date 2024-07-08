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
package org.weaverdb;

import java.io.IOException;
import java.io.InputStream;
import java.nio.channels.ReadableByteChannel;
import java.util.function.Supplier;

/**
 *
 */
public class Output<T> {
    private final Supplier<String> name;
    private final Getter<T> source;
    private final Class<? extends T> type;
    private final int index;
    
    Output(BoundOutput<? extends T> base) {
        source = base::get;
        name = base::getName;
        type = base.getTypeClass();
        index = base.getIndex();
    }
    
    Output(BoundOutputChannel<? extends T> base) {
        source = base::value;
        name = base::getName;
        index = base.getIndex();
        type = null;
    }
    
    Output(BoundOutputReceiver<? extends T> base) {
        source = base::value;
        name = base::getName;
        index = base.getIndex();
        type = null;
    }
    
    public String getName() {
        return name.get();
    }
    
    public int getIndex() {
        return index;
    }
    
    public T value() throws ExecutionException {
        return source.value();
    }
    
    public Class<? extends T> getType() {
        return type;
    }
    
    public T get() throws ExecutionException {
        return source.value();
    }
    
    @FunctionalInterface
    private static interface Getter<T> {
        T value() throws ExecutionException;
    }

    @FunctionalInterface
    public static interface Channel<T> {
        T transform(ReadableByteChannel src) throws IOException;
    }
    
    @FunctionalInterface
    public static interface Stream<T> {
        T transform(InputStream src) throws IOException;
    }
}
