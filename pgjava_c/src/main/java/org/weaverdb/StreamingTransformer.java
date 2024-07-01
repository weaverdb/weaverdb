

package org.weaverdb;

import java.util.concurrent.Callable;
import java.util.concurrent.Future;


public interface StreamingTransformer extends AutoCloseable {

    @Override
    public void close();
    
    <T> Future<T> schedule(Callable<T> work);
}
