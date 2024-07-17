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

package org.weaverdb;

import java.util.concurrent.Callable;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;


class StreamingTransformer21 implements StreamingTransformer {
    
    ExecutorService vpool = Executors.newVirtualThreadPerTaskExecutor();

    @Override
    public void close() {
        vpool.shutdownNow();
    }

    @Override
    public <T> Future<T> schedule(Callable<T> work) {
        return vpool.submit(work);
    }
    
}
