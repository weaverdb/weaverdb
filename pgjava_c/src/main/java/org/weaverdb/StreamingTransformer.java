/*
 * Copyright (c) 2024 Myron Scott <myron@weaverdb.org> All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 */



package org.weaverdb;

import java.util.concurrent.Callable;
import java.util.concurrent.Future;


public interface StreamingTransformer extends AutoCloseable {

    @Override
    public void close();
    
    <T> Future<T> schedule(Callable<T> work);
}
