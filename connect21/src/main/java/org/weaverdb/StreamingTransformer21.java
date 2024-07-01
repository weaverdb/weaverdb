
package org.weaverdb;

import java.util.concurrent.Callable;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;


public class StreamingTransformer21 implements StreamingTransformer {
    
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
