/*
 * Copyright (c) 2024 Myron Scott <myron@weaverdb.org> All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 */


package org.weaverdb;

import java.util.concurrent.Callable;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;

public class StreamingTransformer17 implements StreamingTransformer {
    private final ExecutorService vpool = Executors.newCachedThreadPool();

    @Override
    public void close()  {
        vpool.shutdown();
    }
    
    @Override
    public <T> Future<T> schedule(Callable<T> work) {
        return vpool.submit(work);
    }
}
