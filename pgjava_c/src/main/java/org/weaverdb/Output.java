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
 * Linked output from statement.
 */
public class Output<T> {
    private final Supplier<String> name;
    private final Getter<? extends T> source;
    private final int index;
    
    public Output(Supplier<String> column, Getter<? extends T> source, int index) {
        this.name = column;
        this.source = source;
        this.index = index;
    }
    
    public String getName() {
        return name.get();
    }
    
    public int getIndex() {
        return index;
    }
    
    public T get() throws ExecutionException {
        return source.value();
    }
    
    @FunctionalInterface
    public static interface Getter<T> {
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
