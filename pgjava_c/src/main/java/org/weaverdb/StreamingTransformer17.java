
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
